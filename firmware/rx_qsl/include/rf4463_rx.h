/*
 * rf4463_rx.h -- driver de RECEPCION del modulo RF4463PRO (Si4463).
 * ==================================================================
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Obra derivada del driver RH_RF24 de la biblioteca RadioHead (version 1.145):
 *   RadioHead    Copyright (C) 2011-2022 Mike McCauley
 *   RH_RF24.cpp  Copyright (C) 2011 Mike McCauley
 *   RH_RF24.h    Copyright (C) 2014 Mike McCauley
 *   https://www.airspayce.com/mikem/arduino/RadioHead/
 * RadioHead se distribuye bajo la GNU GPL version 3 (o con licencia
 * comercial); este archivo se distribuye bajo la GNU GPL version 3.
 *
 * Modificado en 2026-10: command(), power_on_reset(), set_props(),
 * get_prop(), configure() y read_fifo() parten de RH_RF24::command(),
 * power_on_reset(), set_properties(), get_properties(), configure() y
 * readNextFragment(), y begin() sigue el orden de RH_RF24::init(); todo
 * reescrito para ArduinoCore-mbed y con los comentarios traducidos.  Se quito
 * la transmision y se anadieron el POWER_UP explicito, el control de limites
 * de configure(), el paquete de longitud variable, la relectura de la
 * configuracion del chip, el vigilante de RX y el RSSI de cada paquete.
 *
 * Este programa es software libre: se puede redistribuir y/o modificar bajo
 * los terminos de la GNU General Public License version 3, publicada por la
 * Free Software Foundation.  Se distribuye con la esperanza de que sea util,
 * pero SIN NINGUNA GARANTIA, ni siquiera la garantia implicita de
 * COMERCIABILIDAD o IDONEIDAD PARA UN PROPOSITO PARTICULAR.  Ver la GNU
 * General Public License (archivo LICENSE) para mas detalles.
 *
 * Este driver solo recibe: no hay ninguna funcion que transmita.
 *
 * De donde sale cada parte
 * ------------------------
 *  1. RadioHead (RH_RF24, https://www.airspayce.com/mikem/arduino/RadioHead/):
 *     la mecanica de bajo nivel del Si446x:
 *       * command(): se emite la orden y despues se sondea CTS con READ_BUF
 *         (0x44), hasta 2500 veces.
 *       * nSEL se baja otra vez antes de subirlo: hay modulos que fallan si
 *         se deselecciona demasiado rapido.
 *       * las lecturas del FIFO no esperan CTS (hacerlo bloquea el chip).
 *       * power_on_reset() segun la hoja de datos (3.3.2).
 *       * el FIFO se vacia por trozos mientras llega el paquete.
 *  2. WDS (Wireless Development Suite, Silicon Labs): la configuracion del
 *     modem en radio_config.h (filtro de canal, AGC, sintetizador).
 *  3. Este proyecto: los parametros del paquete (reescritos y releidos del
 *     chip despues de radio_config.h), el umbral de preambulo, el vigilante
 *     de RX y la lectura del RSSI de cada paquete.
 *
 * El paquete que se recibe
 * ------------------------
 *   preambulo (bits alternados) | sincronia 0x7E | N (1 byte) | N bytes | 2 bytes de cola
 *
 * El chip entrega N y los N bytes; los 2 bytes de cola se descartan.  Este
 * receptor no comprueba nada a este nivel: cada contenido que interesa trae su
 * propia comprobacion (el CRC-32 de SSDV y el FCS de AX.25), y el programa del
 * PC solo acepta lo que la pasa.
 *
 * Antena: en el RF4463PRO los GPIO2/GPIO3 del chip mueven el conmutador de
 * antena del modulo (GPIO2 = recepcion, GPIO3 = transmision; hoja de datos del
 * modulo, pagina 5).  Se configuran con las funciones automaticas RX_STATE /
 * TX_STATE para que el propio chip lo maneje.
 */

#ifndef RF4463_RX_H_
#define RF4463_RX_H_

#include <Arduino.h>
#include <SPI.h>

/* ----------------------------------------------------------------- enlace */
#define ENLACE_MAX_DATOS     128     /* N maximo */
#define ENLACE_COLA          2       /* bytes despues de los datos */
#define ENLACE_MAX_PAQUETE   (1 + ENLACE_MAX_DATOS + ENLACE_COLA)
#define ENLACE_SINCRONIA     0x7E
#define ENLACE_PREAMBULO     5       /* bytes */

/* ------------------------------------------------------------- ordenes del Si4463 */
#define RF_CMD_PART_INFO        0x01
#define RF_CMD_POWER_UP         0x02
#define RF_CMD_SET_PROPERTY     0x11
#define RF_CMD_GET_PROPERTY     0x12
#define RF_CMD_GPIO_PIN_CFG     0x13
#define RF_CMD_FIFO_INFO        0x15
#define RF_CMD_GET_INT_STATUS   0x20
#define RF_CMD_GET_MODEM_STATUS 0x22
#define RF_CMD_START_RX         0x32
#define RF_CMD_REQUEST_DEVICE_STATE 0x33
#define RF_CMD_CHANGE_STATE     0x34
#define RF_CMD_READ_BUF         0x44
#define RF_CMD_RX_FIFO_READ     0x77

#define RF_REPLY_CTS            0xFF
#define RF_CTS_RETRIES          2500

/* estados */
#define RF_STATE_READY          0x03
#define RF_STATE_RX             0x08

/* GET_INT_STATUS: byte 2 = PH_PEND, byte 4 = MODEM_PEND */
#define RF_PH_TX_ALMOST_EMPTY   0x02
#define RF_MODEM_INVALID_SYNC   0x20

/* funciones de GPIO del Si446x */
#define RF_GPIO_NO_CHANGE       0
#define RF_GPIO_TRISTATE        1
#define RF_GPIO_TX_STATE        32
#define RF_GPIO_RX_STATE        33

/* propiedades (grupo << 8 | indice) */
#define RF_P_GLOBAL_XO_TUNE     0x0000
#define RF_P_INT_CTL_ENABLE     0x0100
#define RF_P_PREAMBLE_TX_LENGTH 0x1000
#define RF_P_PREAMBLE_STD_1     0x1001
#define RF_P_PREAMBLE_CONFIG    0x1004
#define RF_P_SYNC_CONFIG        0x1100
#define RF_P_SYNC_BITS_31_24    0x1101
#define RF_P_PKT_CRC_CONFIG     0x1200
#define RF_P_PKT_CONFIG1        0x1206
#define RF_P_PKT_LEN            0x1208
#define RF_P_PKT_LEN_FIELD_SRC  0x1209
#define RF_P_PKT_LEN_ADJUST     0x120A
#define RF_P_PKT_TX_THRESHOLD   0x120B
#define RF_P_PKT_RX_THRESHOLD   0x120C
#define RF_P_PKT_F1_LEN_12_8    0x120D
#define RF_P_PKT_F1_LEN_7_0     0x120E
#define RF_P_PKT_F1_CONFIG      0x120F
#define RF_P_PKT_F1_CRC_CONFIG  0x1210
#define RF_P_PKT_F2_LEN_12_8    0x1211
#define RF_P_PKT_F2_LEN_7_0     0x1212
#define RF_P_PKT_F3_LEN_12_8    0x1215
#define RF_P_PKT_F4_LEN_12_8    0x1219
#define RF_P_PKT_F5_LEN_12_8    0x121D
#define RF_P_MODEM_MOD_TYPE     0x2000
#define RF_P_MODEM_DATA_RATE    0x2003
#define RF_P_MODEM_TX_NCO_MODE  0x2006
#define RF_P_MODEM_FREQ_DEV     0x200A

/* Modulacion del enlace: 2GFSK, 9600 baudios, +/-2400 Hz.
 *   DATA_RATE = 9600 x 40 (sobremuestreo)               = 0x05DC00
 *   TX_NCO    = sobremuestreo 40 | NCO de 30 MHz         = 0x05C9C380
 *   FREQ_DEV  = 2400 x 2^19 x 8 / (2 x 30 MHz)           = 0x0000A8 */
static const uint8_t RF_DATA_RATE[3] = {0x05, 0xDC, 0x00};
static const uint8_t RF_TX_NCO[4]    = {0x05, 0xC9, 0xC3, 0x80};
static const uint8_t RF_FREQ_DEV[3]  = {0x00, 0x00, 0xA8};


class RF4463 {
public:
    RF4463(uint8_t cs, uint8_t sdn, uint8_t irq, const char *tag = "radio",
           arduino::MbedSPI *bus = &SPI)
        : _cs(cs), _sdn(sdn), _irq(irq), _tag(tag), _spi(bus) {}

    const char *tag() const { return _tag; }
    uint16_t part() const { return _part; }
    bool escuchando() const { return _mode == RF_STATE_RX; }
    uint32_t spi_hz() const { return _spi_hz; }

    /* ================================================================ SPI */

    /* Reset por SDN (hoja de datos 3.3.2): atarlo a masa no da arranques fiables. */
    void power_on_reset()
    {
        digitalWrite(_sdn, HIGH);
        pinMode(_sdn, OUTPUT);
        digitalWrite(_sdn, HIGH);
        delay(10);
        digitalWrite(_sdn, LOW);
        delay(10);
    }

    /* Obligatorio tras cada POR (hoja de datos 3.3.2): arranca el cristal.
     *   BOOT_OPTIONS = 0x01 (modo funcional), XTAL_OPTIONS = 0x00 (cristal
     *   externo: el RF4463PRO lleva uno de 30 MHz). */
    bool power_up(uint32_t xo_hz = 30000000UL)
    {
        uint8_t a[6] = {0x01, 0x00,
                        (uint8_t)(xo_hz >> 24), (uint8_t)(xo_hz >> 16),
                        (uint8_t)(xo_hz >> 8),  (uint8_t)(xo_hz)};
        bool ok = command(RF_CMD_POWER_UP, a, sizeof(a));
        delay(20);
        return ok;
    }

    /* Emitir la orden y despues sondear CTS con READ_BUF.  false si CTS no
     * llega nunca (chip ausente o colgado). */
    bool command(uint8_t cmd, const uint8_t *wbuf = NULL, uint8_t wlen = 0,
                 uint8_t *rbuf = NULL, uint8_t rlen = 0)
    {
        bool done = false;
        _spi->beginTransaction(SPISettings(_spi_hz, MSBFIRST, SPI_MODE0));
        digitalWrite(_cs, LOW);
        _spi->transfer(cmd);
        while (wlen--) _spi->transfer(*wbuf++);
        digitalWrite(_cs, LOW);         /* margen antes de deseleccionar */
        digitalWrite(_cs, HIGH);

        for (uint16_t n = 0; !done && n < RF_CTS_RETRIES; n++) {
            digitalWrite(_cs, LOW);
            _spi->transfer(RF_CMD_READ_BUF);
            if (_spi->transfer(0) == RF_REPLY_CTS) {
                uint8_t k = rlen;
                uint8_t *p = rbuf;
                while (k--) *p++ = _spi->transfer(0);
                done = true;
            }
            digitalWrite(_cs, LOW);
            digitalWrite(_cs, HIGH);
        }
        _spi->endTransaction();
        return done;
    }

    bool set_prop(uint16_t prop, uint8_t value) { return set_props(prop, &value, 1); }

    bool set_props(uint16_t first, const uint8_t *values, uint8_t count)
    {
        uint8_t buf[15];
        if (count > 12) return false;
        buf[0] = (uint8_t)(first >> 8);
        buf[1] = count;
        buf[2] = (uint8_t)(first & 0xFF);
        for (uint8_t i = 0; i < count; i++) buf[3 + i] = values[i];
        return command(RF_CMD_SET_PROPERTY, buf, (uint8_t)(count + 3));
    }

    uint8_t get_prop(uint16_t prop)
    {
        uint8_t buf[3] = {(uint8_t)(prop >> 8), 1, (uint8_t)(prop & 0xFF)};
        uint8_t r = 0;
        command(RF_CMD_GET_PROPERTY, buf, 3, &r, 1);
        return r;
    }

    /* Las lecturas del FIFO no esperan CTS. */
    void read_fifo(uint8_t *d, uint8_t n)
    {
        _spi->beginTransaction(SPISettings(_spi_hz, MSBFIRST, SPI_MODE0));
        digitalWrite(_cs, LOW);
        _spi->transfer(RF_CMD_RX_FIFO_READ);
        while (n--) *d++ = _spi->transfer(0);
        digitalWrite(_cs, HIGH);
        _spi->endTransaction();
    }

    void fifo_info(uint8_t *rx_count)
    {
        uint8_t r[2] = {0, 0};
        command(RF_CMD_FIFO_INFO, NULL, 0, r, 2);
        if (rx_count) *rx_count = r[0];
    }

    void fifo_reset()
    {
        uint8_t a = 0x03;
        command(RF_CMD_FIFO_INFO, &a, 1);
    }

    /* Estado REAL del chip.  escuchando() dice lo que el driver cree; si el
     * Si4463 se sale de RX por su cuenta, solo esto lo detecta. */
    uint8_t device_state()
    {
        uint8_t r[2] = {0, 0};
        command(RF_CMD_REQUEST_DEVICE_STATE, NULL, 0, r, 2);
        return (uint8_t)(r[0] & 0x0F);
    }

    bool en_rx_real() { return device_state() == RF_STATE_RX; }

    /* Lee y limpia las interrupciones, acumulandolas en latches propios. */
    void poll_ints()
    {
        uint8_t w[3] = {0x00, 0x00, 0x00};
        uint8_t r[8] = {0};
        if (command(RF_CMD_GET_INT_STATUS, w, 3, r, 8)) {
            _ph_latch |= r[2];
            _modem_latch |= r[4];
        }
    }

    void clear_ints() { poll_ints(); _ph_latch = 0; _modem_latch = 0; }

    /* GET_MODEM_STATUS: [2] RSSI actual, [3] RSSI enclavado al detectar la
     * sincronia. */
    uint8_t rssi_paquete()
    {
        uint8_t r[8] = {0};
        command(RF_CMD_GET_MODEM_STATUS, NULL, 0, r, 8);
        return r[3] ? r[3] : r[2];
    }

    /* RSSI de este momento (el ruido del canal si no hay senal). */
    uint8_t rssi_actual()
    {
        uint8_t r[8] = {0};
        command(RF_CMD_GET_MODEM_STATUS, NULL, 0, r, 8);
        return r[2];
    }

    /* Ajuste fino del cristal (0..127; subirlo baja la frecuencia). */
    void set_xo_tune(uint8_t v) { _xo_tune = v & 0x7F; set_prop(RF_P_GLOBAL_XO_TUNE, _xo_tune); }
    uint8_t xo_tune() const { return _xo_tune; }

    /* ========================================================== arranque */

    /* false + motivo en err si algo no cuadra.  La radio no se declara lista
     * si la configuracion releida del chip no es la esperada. */
    bool begin(const uint8_t *wds_cfg, size_t wds_len, char *err, size_t err_len,
               uint32_t spi_hz = 4000000, uint32_t xo_hz = 30000000UL)
    {
        _spi_hz = spi_hz;
        pinMode(_cs, OUTPUT);
        digitalWrite(_cs, HIGH);
        pinMode(_irq, INPUT);
        _spi->begin();

        power_on_reset();
        if (!power_up(xo_hz)) {
            snprintf(err, err_len, "%s: POWER_UP sin respuesta "
                     "(revisa nSEL/SDN/SDI/SDO/SCLK y la alimentacion de 3V3)", _tag);
            return false;
        }
        clear_ints();

        uint8_t info[8] = {0};
        if (!command(RF_CMD_PART_INFO, NULL, 0, info, 8)) {
            snprintf(err, err_len, "%s: PART_INFO sin respuesta (revisa nSEL/SDN/SDI/SDO/SCLK)", _tag);
            return false;
        }
        _part = (uint16_t)((info[1] << 8) | info[2]);
        if (_part != 0x4463 && _part != 0x4464) {
            snprintf(err, err_len, "%s: chip 0x%04X inesperado (se esperaba un Si4463)", _tag, _part);
            return false;
        }

        if (!configure(wds_cfg, wds_len)) {
            snprintf(err, err_len, "%s: fallo cargando radio_config.h", _tag);
            return false;
        }

        aplicar_enlace();
        conmutador_antena();
        interrupciones();

        if (!verificar_enlace(err, err_len)) return false;

        idle();
        snprintf(err, err_len, "%s: Si%04X listo -- 2GFSK 9600 baudios +/-2400 Hz, "
                 "sincronia 7E, hasta %d bytes de datos", _tag, _part, ENLACE_MAX_DATOS);
        return true;
    }

    /* Recorre la tabla de radio_config.h: <n> <cmd> <n-1 bytes> ... 0 */
    bool configure(const uint8_t *cfg, size_t len)
    {
        size_t pos = 0;
        while (pos < len) {
            uint8_t n = cfg[pos];
            if (n == 0) break;
            pos++;
            if (pos + n > len || n > 16) return false;
            if (!command(cfg[pos], &cfg[pos + 1], (uint8_t)(n - 1))) return false;
            pos += n;
        }
        return true;
    }

    /* Todo lo que define el enlace, escrito despues de radio_config.h para que
     * la recepcion no dependa de que esa tabla sea perfecta.  Los coeficientes
     * del filtro de canal y del AGC si vienen de la tabla. */
    void aplicar_enlace()
    {
        set_prop(RF_P_GLOBAL_XO_TUNE, _xo_tune);
        set_prop(RF_P_MODEM_MOD_TYPE, 0x03);            /* 2GFSK desde el FIFO */
        set_props(RF_P_MODEM_DATA_RATE, RF_DATA_RATE, 3);
        set_props(RF_P_MODEM_TX_NCO_MODE, RF_TX_NCO, 4);
        set_props(RF_P_MODEM_FREQ_DEV, RF_FREQ_DEV, 3);

        set_prop(RF_P_PREAMBLE_TX_LENGTH, ENLACE_PREAMBULO);
        set_prop(RF_P_PREAMBLE_CONFIG, 0x32);           /* preambulo estandar 0101 */

        /* Umbral de deteccion del preambulo, en bits.  El preambulo es corto y
         * el chip usa sus primeros bits para ajustar la ganancia; con el valor
         * de fabrica (20) se pierden paquetes.  Medido en banco, 15 paquetes por punto:
         *     20 bits -> 11/15     16 bits -> 13/15
         *     12 bits -> 15/15     8 bits -> 13/15 (empieza a aceptar ruido)
         * La sincronia de un byte filtra las detecciones falsas. */
        set_prop(RF_P_PREAMBLE_STD_1, 0x0C);

        set_prop(RF_P_SYNC_CONFIG, 0x00);               /* sincronia de 1 byte */
        set_prop(RF_P_SYNC_BITS_31_24, ENLACE_SINCRONIA);

        set_prop(RF_P_PKT_CRC_CONFIG, 0x00);            /* sin comprobacion en el chip */
        set_prop(RF_P_PKT_CONFIG1, 0x00);               /* bits MSB primero */

        /* LONGITUD VARIABLE: el primer byte del paquete (N) es el campo 1 y
         * dice cuanto mide el campo 2 (N + los 2 bytes de cola).  Asi el chip
         * termina el paquete en el byte exacto y no sigue demodulando ruido.
         * Con un largo fijo, el chip llenaba el FIFO de ruido, daba el paquete
         * por malo y al rearmar se llevaba los ultimos bytes buenos (medido: de
         * 80 bytes llegaban 76).
         * PKT_LEN = 0x0A: longitud de 1 byte, que tambien entra en el FIFO, y
         * va al campo 2. */
        set_prop(RF_P_PKT_LEN, 0x0A);
        set_prop(RF_P_PKT_LEN_FIELD_SRC, 0x01);
        set_prop(RF_P_PKT_LEN_ADJUST, ENLACE_COLA);
        {
            const uint8_t f1[2] = {0x00, 0x01};
            set_props(RF_P_PKT_F1_LEN_12_8, f1, 2);
        }
        set_prop(RF_P_PKT_F1_CONFIG, 0x00);
        set_prop(RF_P_PKT_F1_CRC_CONFIG, 0x00);
        {
            const uint8_t f2[4] = {0x00, (uint8_t)(ENLACE_MAX_DATOS + ENLACE_COLA), 0x00, 0x00};
            set_props(RF_P_PKT_F2_LEN_12_8, f2, 4);
        }
        const uint8_t cero4[4] = {0, 0, 0, 0};          /* campos 3..5 sin usar */
        set_props(RF_P_PKT_F3_LEN_12_8, cero4, 4);
        set_props(RF_P_PKT_F4_LEN_12_8, cero4, 4);
        set_props(RF_P_PKT_F5_LEN_12_8, cero4, 4);

        set_prop(RF_P_PKT_TX_THRESHOLD, 0x10);
        set_prop(RF_P_PKT_RX_THRESHOLD, 0x10);
    }

    /* El conmutador de antena del modulo lo maneja el propio chip. */
    void conmutador_antena()
    {
        uint8_t gpio[7] = {
            RF_GPIO_TRISTATE,       /* GPIO0 -- sin conectar */
            RF_GPIO_TRISTATE,       /* GPIO1 -- sin conectar */
            RF_GPIO_RX_STATE,       /* GPIO2 -- conmutador: recepcion */
            RF_GPIO_TX_STATE,       /* GPIO3 -- conmutador: transmision (nunca se usa) */
            RF_GPIO_NO_CHANGE,      /* nIRQ */
            RF_GPIO_NO_CHANGE,      /* SDO */
            0x00
        };
        command(RF_CMD_GPIO_PIN_CFG, gpio, sizeof(gpio));
    }

    void interrupciones()
    {
        const uint8_t ie[4] = {0x03, 0xFF, 0xFF, 0x00};
        set_props(RF_P_INT_CTL_ENABLE, ie, 4);
    }

    /* Relee del chip lo que quedo grabado: no se da por hecho. */
    bool verificar_enlace(char *msg, size_t msg_len)
    {
        struct { uint16_t p; uint8_t want; const char *name; } chk[] = {
            {RF_P_MODEM_MOD_TYPE,     0x03,                  "modulacion 2GFSK"},
            {RF_P_PREAMBLE_STD_1,     0x0C,                  "umbral de preambulo"},
            {RF_P_PREAMBLE_CONFIG,    0x32,                  "tipo de preambulo"},
            {RF_P_SYNC_CONFIG,        0x00,                  "sincronia de 1 byte"},
            {RF_P_SYNC_BITS_31_24,    ENLACE_SINCRONIA,      "sincronia 0x7E"},
            {RF_P_PKT_CRC_CONFIG,     0x00,                  "sin comprobacion en el chip"},
            {RF_P_PKT_CONFIG1,        0x00,                  "orden de bits"},
            {RF_P_PKT_F1_LEN_7_0,     0x01,                  "campo de longitud"},
            {RF_P_PKT_LEN,            0x0A,                  "longitud variable"},
            {RF_P_PKT_LEN_FIELD_SRC,  0x01,                  "origen de la longitud"},
            {RF_P_PKT_LEN_ADJUST,     ENLACE_COLA,           "bytes de cola"},
            {RF_P_PKT_F2_LEN_7_0, (uint8_t)(ENLACE_MAX_DATOS + ENLACE_COLA), "tamano maximo"},
        };
        for (unsigned i = 0; i < sizeof(chk) / sizeof(chk[0]); i++) {
            uint8_t got = get_prop(chk[i].p);
            if (got != chk[i].want) {
                snprintf(msg, msg_len, "%s: %s -> el chip devuelve 0x%02X, esperado 0x%02X",
                         _tag, chk[i].name, got, chk[i].want);
                return false;
            }
        }
        for (int i = 0; i < 3; i++) {
            if (get_prop((uint16_t)(RF_P_MODEM_DATA_RATE + i)) != RF_DATA_RATE[i]) {
                snprintf(msg, msg_len, "%s: la velocidad no es 9600 baudios", _tag);
                return false;
            }
        }
        return true;
    }

    /* ============================================================ estados */

    void idle()
    {
        uint8_t s = RF_STATE_READY;
        command(RF_CMD_CHANGE_STATE, &s, 1);
        _mode = RF_STATE_READY;
    }

    /* =============================================================== RX */

    void start_rx()
    {
        idle();
        fifo_reset();
        clear_ints();
        /* canal 0, arranque inmediato; al terminar o fallar, volver a RX */
        uint8_t rx[7] = {0x00, 0x00, 0x00, 0x00, RF_STATE_RX, RF_STATE_RX, RF_STATE_RX};
        command(RF_CMD_START_RX, rx, sizeof(rx));
        _mode = RF_STATE_RX;
        _have = 0;
        _want = 0;
        _t0 = 0;
    }

    /* Bombea la recepcion.  Devuelve N (> 0) y copia los N bytes de datos en
     * out cuando hay un paquete completo; 0 si aun no hay nada; -1 si se
     * descarto algo incompleto (la RX ya se rearmo). */
    int poll_rx(uint8_t *out, uint16_t out_max)
    {
        /* Sincronia invalida: no rearmar a la ligera.  Con una sincronia de un
         * byte las detecciones falsas son constantes, y cada rearme vacia el
         * FIFO: si en ese momento entraba un paquete bueno, se pierde.  Solo se
         * rearma tras una racha larga sin recibir nada, y nunca a mitad de un
         * paquete. */
        poll_ints();
        if (_modem_latch & RF_MODEM_INVALID_SYNC) {
            _modem_latch &= (uint8_t)~RF_MODEM_INVALID_SYNC;
            _bad_sync++;
            if (_have == 0 && ++_sync_run >= SYNC_RUN_MAX) {
                _sync_run = 0;
                _resets++;
                start_rx();
                return -1;
            }
        }

        uint8_t avail = 0;
        fifo_info(&avail);
        if (avail == 0) {
            if (_have > 0 && _t0 && (millis() - _t0) > rx_gap_ms) {
                _partial++;
                start_rx();                     /* paquete a medias: rearmar */
                return -1;
            }
            return 0;
        }
        if (_have == 0) _t0_ini = millis();
        _t0 = millis();

        /* Un paquete de N bytes no puede durar mas de N x 0.83 ms; si se pasa
         * del doble es una sincronia falsa enganchada: abandonar. */
        if (_want && (millis() - _t0_ini) > (uint32_t)(_want * 2) + 30) {
            _partial++;
            start_rx();
            return -1;
        }

        while (avail > 0) {
            uint8_t take = avail > 32 ? 32 : avail;
            if (_have + take > (uint16_t)sizeof(_buf)) take = (uint8_t)(sizeof(_buf) - _have);
            if (take == 0) { start_rx(); return -1; }
            read_fifo(&_buf[_have], take);
            _have += take;
            avail -= take;
            _t0 = millis();

            if (_want == 0 && _have >= 1) {
                uint8_t n = _buf[0];
                if (n == 0 || n > ENLACE_MAX_DATOS) { start_rx(); return -1; }
                _want = (uint16_t)(1 + n + ENLACE_COLA);
            }
            if (_want && _have >= _want) {
                uint16_t n = (uint16_t)_buf[0];
                uint8_t tmp[ENLACE_MAX_DATOS];
                for (uint16_t i = 0; i < n; i++) tmp[i] = _buf[1 + i];
                /* RSSI ANTES de rearmar: START_RX borra el RSSI enclavado */
                _last_rssi = rssi_paquete();
                start_rx();
                if (n > out_max) return -1;
                for (uint16_t i = 0; i < n; i++) out[i] = tmp[i];
                _rx_good++;
                _sync_run = 0;
                return (int)n;
            }
        }
        return 0;
    }

    /* a mitad de un paquete (el vigilante no debe rearmar aqui) */
    bool ensamblando() const   { return _have > 0; }
    uint16_t paquetes() const  { return _rx_good; }
    uint16_t sinc_falsas() const { return _bad_sync; }
    uint16_t parciales() const { return _partial; }
    uint16_t rearmes() const   { return _resets; }
    uint8_t  last_rssi() const { return _last_rssi; }

    /* Silencio (ms) tras el ultimo byte antes de dar un paquete por cortado:
     * a 9600 baudios, 14 ms son unos 17 bytes. */
    uint32_t rx_gap_ms = 14;

private:
    static const uint8_t SYNC_RUN_MAX = 12;

    uint8_t  _cs, _sdn, _irq;
    const char *_tag;
    arduino::MbedSPI *_spi;
    uint32_t _spi_hz = 4000000;
    uint16_t _part = 0;
    uint8_t  _mode = 0xFF;
    uint8_t  _ph_latch = 0;
    uint8_t  _modem_latch = 0;
    uint8_t  _buf[ENLACE_MAX_PAQUETE];
    uint16_t _have = 0;
    uint16_t _want = 0;
    uint32_t _t0 = 0;
    uint32_t _t0_ini = 0;
    uint16_t _rx_good = 0, _bad_sync = 0, _partial = 0, _resets = 0;
    uint8_t  _sync_run = 0;
    /* Ajuste del cristal.  Es POR MODULO: con el valor por defecto (82) el
     * modulo de referencia quedaba +3 kHz alto; con 90, +78 Hz.  Se puede
     * cambiar desde el PC (orden 'X') sin reflashear. */
    uint8_t  _xo_tune = 90;
    uint8_t  _last_rssi = 0;
};

#endif /* RF4463_RX_H_ */
