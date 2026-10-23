/*
 * rx_qsl -- receptor del enlace descendente del MRZN con un Raspberry Pi Pico
 * y un modulo RF4463PRO (Si4463).
 * ===========================================================================
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * SOLO RECIBE.  El firmware no tiene ninguna funcion de transmision.
 *
 * Cada paquete que oye lo pasa al PC por USB, tal cual.  El programa del PC
 * (host/consola.py) se queda solo con la imagen QSL (SSDV) y con la baliza de
 * texto (AX.25), y descarta todo lo demas.
 *
 * PLACAS
 * ------
 *   un_modulo     el modulo en el bus SPI0 (placa de un modulo)
 *                   SDO->GP16  SDI->GP19  SCLK->GP18  nSEL->GP17  nIRQ->GP2  SDN->GP3
 *   dos_modulos   la placa de dos modulos: se usa el U2 (bus SPI1) para recibir
 *                   SDO->GP12  SDI->GP11  SCLK->GP10  nSEL->GP13  nIRQ->GP15 SDN->GP14
 *                 y el U1 queda apagado (SDN alto).
 *   En las dos: VCC->3V3 (pin 36), GND->GND, antena de 433 MHz en el modulo.
 *
 * PROTOCOLO USB (SLIP sobre el puerto serie USB)
 * ----------------------------------------------
 *   PC -> Pico   'P'            estado
 *                'H'            autoprueba de las conexiones, cable a cable
 *                'I'            reiniciar la radio
 *                'X' + valor    ajuste fino del cristal (0..127)
 *                'L'            volver a escuchar
 *   Pico -> PC   'R' + rssi + datos   paquete recibido
 *                'V' + texto    informacion / estado
 *                'W' + texto    aviso
 *                'E' + texto    error
 *                'K'            orden terminada
 */

#include <Arduino.h>
#include "rf4463_rx.h"
#include "radio_config.h"

/* ----------------------------------------------------------- configuracion */
#ifndef FW_VERSION
#define FW_VERSION "rx_qsl 1.0"
#endif

#define U1_CS   17
#define U1_SDN   3
#define U1_IRQ   2

#define U2_CS   13
#define U2_SDN  14
#define U2_IRQ  15
#define U2_MISO 12              /* SDO del modulo */
#define U2_MOSI 11              /* SDI del modulo */
#define U2_SCK  10

#ifndef PIN_LED
#define PIN_LED 25
#endif

/* SPI: el Si4463 admite hasta 10 MHz.  Si la radio no arranca a SPI_HZ
 * (cables largos, mala soldadura), se reintenta sola a SPI_HZ_SEGURO. */
#ifndef SPI_HZ
#define SPI_HZ 8000000
#endif
#define SPI_HZ_SEGURO 4000000

/* El regulador de 3V3 del Pico (RT6150) en modo PWM (GPIO23 alto): menos rizado
 * en la alimentacion del modulo (hoja de datos del Pico, 4.4). */
#ifndef RT6150_PWM
#define RT6150_PWM 1
#endif
#define PIN_RT6150_PS 23

/* --------------------------------------------------------------- radio */
#if defined(DOS_MODULOS) && DOS_MODULOS
static arduino::MbedSPI SPI_U2(U2_MISO, U2_MOSI, U2_SCK);          /* SPI1 */
static RF4463 radio(U2_CS, U2_SDN, U2_IRQ, "U2", &SPI_U2);
#define RADIO_CS  U2_CS
#define RADIO_SDN U2_SDN
#define RADIO_IRQ U2_IRQ
#define PLACA "dos_modulos (U2)"
#else
static RF4463 radio(U1_CS, U1_SDN, U1_IRQ, "U1");
#define RADIO_CS  U1_CS
#define RADIO_SDN U1_SDN
#define RADIO_IRQ U1_IRQ
#define PLACA "un_modulo (U1)"
#endif

/* --------------------------------------------------------------- SLIP */
#define SLIP_END      0xC0
#define SLIP_ESC      0xDB
#define SLIP_ESC_END  0xDC
#define SLIP_ESC_ESC  0xDD

/* El mensaje SLIP entero en UNA escritura: en el core Arduino-mbed cada
 * write(b) espera a que el PC recoja el byte anterior, y byte a byte el Pico
 * dejaria de atender la radio mientras tanto. */
static void slip_send(const uint8_t *d, size_t n)
{
    static uint8_t out[2 * 480 + 2];
    size_t k = 0;
    out[k++] = SLIP_END;
    for (size_t i = 0; i < n && k + 3 <= sizeof(out); i++) {
        uint8_t b = d[i];
        if (b == SLIP_END)      { out[k++] = SLIP_ESC; out[k++] = SLIP_ESC_END; }
        else if (b == SLIP_ESC) { out[k++] = SLIP_ESC; out[k++] = SLIP_ESC_ESC; }
        else                    { out[k++] = b; }
    }
    out[k++] = SLIP_END;
    Serial.write(out, k);
    Serial.flush();
}

static void slip_send_msg(char type, const uint8_t *d, size_t n)
{
    static uint8_t buf[480];
    if (n + 1 > sizeof(buf)) return;
    buf[0] = (uint8_t)type;
    for (size_t i = 0; i < n; i++) buf[1 + i] = d[i];
    slip_send(buf, n + 1);
}

static void slip_text(char type, const char *s)
{
    slip_send_msg(type, (const uint8_t *)s, strlen(s));
}

/* ---------------------------------------------------------------- LED
 * Se enciende al recibir un paquete y se apaga solo a los 40 ms. */
#define LED_PULSO_MS 40
static bool     led_on = false;
static uint32_t led_desde = 0;

static void led_pulso()
{
    digitalWrite(PIN_LED, HIGH);
    led_on = true;
    led_desde = millis();
}

static void led_servicio()
{
    if (led_on && millis() - led_desde >= LED_PULSO_MS) {
        digitalWrite(PIN_LED, LOW);
        led_on = false;
    }
}

/* ------------------------------------------------------------- arranque */
static bool radio_ok = false;
static char estado_radio[120] = "sin arrancar";
static uint16_t arranques = 0;
static uint16_t vigilante = 0;      /* veces que el vigilante rearmo la RX */

static void escuchar()
{
    radio.start_rx();
}

static void reinit()
{
    static const uint8_t cfg[] = RADIO_CONFIGURATION_DATA_ARRAY;
    char m[160];
    arranques++;
    bool ok = radio.begin(cfg, sizeof(cfg), m, sizeof(m), SPI_HZ,
                          RADIO_CONFIGURATION_DATA_RADIO_XO_FREQ);
    if (!ok && SPI_HZ != SPI_HZ_SEGURO) {
        char m2[180];
        snprintf(m2, sizeof(m2), "%.110s -- reintento con el SPI a %u MHz", m,
                 (unsigned)(SPI_HZ_SEGURO / 1000000));
        slip_text('W', m2);
        ok = radio.begin(cfg, sizeof(cfg), m, sizeof(m), SPI_HZ_SEGURO,
                         RADIO_CONFIGURATION_DATA_RADIO_XO_FREQ);
    }
    radio_ok = ok;
    if (ok) snprintf(estado_radio, sizeof(estado_radio), "Si%04X listo", radio.part());
    else snprintf(estado_radio, sizeof(estado_radio), "%.119s", m);
    slip_text(ok ? 'V' : 'E', m);
    if (radio_ok) escuchar();
}

/* ------------------------------------------------------ AUTOPRUEBA ('H')
 * Revisa las conexiones del modulo con el propio chip:
 *   id     PART_INFO = 0x4463 (el chip contesta)
 *   SPI    escribe y relee un registro con 6 patrones: un cable de
 *          SDI/SDO/SCLK/nSEL cambiado, suelto o en corto da bits malos
 *   SDN    subirlo apaga el chip, bajarlo lo vuelve a encender
 *   nIRQ   sin interrupciones el pin esta ALTO; con una provocada, BAJO
 *   10k    si hay resistencia a 3V3 en nSEL y SDN (informativo)
 * Al terminar se reinicia la radio. */
static int con_pulldown(uint8_t pin)
{
    pinMode(pin, INPUT_PULLDOWN);
    delayMicroseconds(300);
    return digitalRead(pin);
}

static bool autoprueba(char *resumen, size_t cap)
{
    RF4463 &r = radio;
    const char *nom = radio.tag();
    char m[200];
    uint8_t info[8] = {0};

    bool id_ok = r.command(RF_CMD_PART_INFO, NULL, 0, info, 8);
    uint16_t part = (uint16_t)((info[1] << 8) | info[2]);
    id_ok = id_ok && (part == 0x4463 || part == 0x4464);
    snprintf(m, sizeof(m), "%s chip: %s (0x%04X)", nom, id_ok ? "OK" : "FALLO", part);
    slip_text('V', m);

    static const uint8_t pat[] = {0x00, 0xFF, 0x55, 0xAA, 0x0F, 0xF0};
    uint8_t orig = r.get_prop(RF_P_PREAMBLE_TX_LENGTH);
    int malos = 0;
    uint8_t esp = 0, vis = 0;
    for (uint8_t i = 0; i < sizeof(pat); i++) {
        r.set_prop(RF_P_PREAMBLE_TX_LENGTH, pat[i]);
        uint8_t g = r.get_prop(RF_P_PREAMBLE_TX_LENGTH);
        if (g != pat[i]) { malos++; esp = pat[i]; vis = g; }
    }
    r.set_prop(RF_P_PREAMBLE_TX_LENGTH, orig);
    bool spi_ok = id_ok && malos == 0;
    if (malos)
        snprintf(m, sizeof(m), "%s SPI (SDI/SDO/SCLK/nSEL): FALLO %d/6 patrones "
                 "(esperado 0x%02X, leido 0x%02X)", nom, malos, esp, vis);
    else
        snprintf(m, sizeof(m), "%s SPI (SDI/SDO/SCLK/nSEL, 6 patrones): %s", nom,
                 spi_ok ? "OK" : "FALLO");
    slip_text('V', m);

    digitalWrite(RADIO_SDN, HIGH);
    delay(10);
    bool apaga = !r.command(RF_CMD_PART_INFO, NULL, 0, info, 8);
    r.power_on_reset();
    bool vuelve = r.power_up(RADIO_CONFIGURATION_DATA_RADIO_XO_FREQ) &&
                  r.command(RF_CMD_PART_INFO, NULL, 0, info, 8);
    bool sdn_ok = apaga && vuelve;
    snprintf(m, sizeof(m), "%s SDN (GP%u): %s  [con SDN alto se apaga: %s; al bajarlo vuelve: %s]",
             nom, RADIO_SDN, sdn_ok ? "OK" : "FALLO", apaga ? "si" : "NO", vuelve ? "si" : "NO");
    slip_text('V', m);

    const uint8_t ie_off[4] = {0x00, 0x00, 0x00, 0x00};
    r.set_props(RF_P_INT_CTL_ENABLE, ie_off, 4);
    r.clear_ints();
    delay(3);
    int alto = digitalRead(RADIO_IRQ);
    const uint8_t ie_fifo[4] = {0x01, RF_PH_TX_ALMOST_EMPTY, 0x00, 0x00};
    r.set_props(RF_P_INT_CTL_ENABLE, ie_fifo, 4);
    r.fifo_reset();
    delay(3);
    int bajo = digitalRead(RADIO_IRQ);
    if (bajo != LOW) {                         /* segundo intento: orden invalida al chip */
        const uint8_t ie_err[4] = {0x04, 0x00, 0x00, 0x08};
        r.set_props(RF_P_INT_CTL_ENABLE, ie_err, 4);
        r.command(0x7E);
        delay(3);
        bajo = digitalRead(RADIO_IRQ);
    }
    r.set_props(RF_P_INT_CTL_ENABLE, ie_off, 4);
    r.clear_ints();
    bool irq_ok = (alto == HIGH && bajo == LOW);
    snprintf(m, sizeof(m), "%s nIRQ (GP%u): %s  [sin interrupcion: %s; con interrupcion: %s]",
             nom, RADIO_IRQ, irq_ok ? "OK" : "FALLO", alto == HIGH ? "alto" : "BAJO",
             bajo == LOW ? "bajo" : "ALTO");
    slip_text('V', m);

    int pu_cs = con_pulldown(RADIO_CS);
    pinMode(RADIO_CS, OUTPUT);
    digitalWrite(RADIO_CS, HIGH);
    int pu_sdn = con_pulldown(RADIO_SDN);
    r.power_on_reset();
    r.power_up(RADIO_CONFIGURATION_DATA_RADIO_XO_FREQ);
    snprintf(m, sizeof(m), "%s 10k a 3V3 (informativo): en nSEL GP%u %s, en SDN GP%u %s", nom,
             RADIO_CS, pu_cs ? "SI" : "no", RADIO_SDN, pu_sdn ? "SI" : "no");
    slip_text('V', m);

    bool ok = id_ok && spi_ok && sdn_ok && irq_ok;
    snprintf(resumen, cap, "%s %s (chip %s, SPI %s, SDN %s, nIRQ %s)", nom, ok ? "BIEN" : "MAL",
             id_ok ? "ok" : "MAL", spi_ok ? "ok" : "MAL", sdn_ok ? "ok" : "MAL",
             irq_ok ? "ok" : "MAL");
    return ok;
}

/* --------------------------------------------------------------- ordenes */
static uint8_t rx_msg[64];
static uint16_t rx_len = 0;
static bool rx_esc = false;

static void handle(const uint8_t *m, uint16_t n)
{
    if (n == 0) return;
    switch ((char)m[0]) {
    case 'P': {
        char s[400];
        snprintf(s, sizeof(s),
                 FW_VERSION " | placa=" PLACA " | radio=%s | enlace=%s | escuchando=%s | "
                 "paquetes=%u | parciales=%u | sinc_falsas=%u | rearmes=%u | vigilante=%u | "
                 "rssi_ahora=%u | rssi=%u | xo=%u | spi_khz=%u | arranques=%u",
                 estado_radio, radio_ok ? "verificado" : "NO VERIFICADO",
                 radio.escuchando() ? "si" : "no",
                 radio.paquetes(), radio.parciales(), radio.sinc_falsas(), radio.rearmes(),
                 vigilante, radio_ok ? radio.rssi_actual() : 0, radio.last_rssi(),
                 radio.xo_tune(),
                 (unsigned)(radio.spi_hz() / 1000), arranques);
        slip_text('V', s);
        break;
    }
    case 'H': {
        char res[140] = "";
        bool ok = autoprueba(res, sizeof(res));
        reinit();
        char s[200];
        snprintf(s, sizeof(s), "AUTOPRUEBA %s: %s", ok ? "TODO BIEN" : "HAY FALLOS", res);
        slip_text('V', s);
        slip_send_msg('K', NULL, 0);
        break;
    }
    case 'I':
        reinit();
        slip_send_msg('K', NULL, 0);
        break;
    case 'X':
        if (n >= 2) {
            radio.set_xo_tune(m[1]);
            escuchar();
            char s[48];
            snprintf(s, sizeof(s), "xo=%u", (unsigned)radio.xo_tune());
            slip_text('V', s);
            slip_send_msg('K', NULL, 0);
        }
        break;
    case 'L':
        if (radio_ok) escuchar();
        slip_send_msg('K', NULL, 0);
        break;
    default:
        slip_text('E', "orden desconocida");
        break;
    }
}

/* ------------------------------------------------------------ setup / loop */

void setup()
{
    Serial.begin(115200);
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);
    pinMode(PIN_RT6150_PS, OUTPUT);
    digitalWrite(PIN_RT6150_PS, RT6150_PWM ? HIGH : LOW);

    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 3000) delay(10);

#if defined(DOS_MODULOS) && DOS_MODULOS
    /* el U1 de la placa de dos modulos queda apagado */
    pinMode(U1_CS, OUTPUT);
    digitalWrite(U1_CS, HIGH);
    pinMode(U1_SDN, OUTPUT);
    digitalWrite(U1_SDN, HIGH);
#endif

    reinit();
    slip_text(radio_ok ? 'V' : 'E', radio_ok ? "listo, escuchando" : "la radio no arranco");
}

void loop()
{
    led_servicio();

    /* --- ordenes del PC (SLIP) --- */
    while (Serial.available()) {
        uint8_t b = (uint8_t)Serial.read();
        if (b == SLIP_END) {
            if (rx_len) { handle(rx_msg, rx_len); rx_len = 0; }
            rx_esc = false;
        } else if (rx_esc) {
            uint8_t v = (b == SLIP_ESC_END) ? SLIP_END : (b == SLIP_ESC_ESC) ? SLIP_ESC : b;
            if (rx_len < sizeof(rx_msg)) rx_msg[rx_len++] = v;
            rx_esc = false;
        } else if (b == SLIP_ESC) {
            rx_esc = true;
        } else {
            if (rx_len < sizeof(rx_msg)) rx_msg[rx_len++] = b;
        }
    }

    /* --- vigilante: cada 250 ms se pregunta al chip si de verdad esta en RX.
     * Si se salio por su cuenta, se rearma.  Nunca a mitad de un paquete. */
    if (radio_ok && !radio.ensamblando()) {
        static uint32_t t_vig = 0;
        if (millis() - t_vig > 250) {
            t_vig = millis();
            if (!radio.en_rx_real()) {
                vigilante++;
                escuchar();
            }
        }
    }

    /* --- paquetes recibidos --- */
    if (radio_ok && radio.escuchando()) {
        static uint8_t datos[ENLACE_MAX_DATOS];
        int n = radio.poll_rx(datos, sizeof(datos));
        if (n > 0) {
            static uint8_t out[ENLACE_MAX_DATOS + 1];
            out[0] = radio.last_rssi();
            for (int i = 0; i < n; i++) out[1 + i] = datos[i];
            slip_send_msg('R', out, (size_t)n + 1);
            led_pulso();
        }
    }
}
