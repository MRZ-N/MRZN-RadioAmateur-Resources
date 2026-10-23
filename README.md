# MRZN-QSL-Receiver
### Receptor de la imagen QSL y la baliza de texto del satélite MRZN, con un Raspberry Pi Pico y un módulo RF4463PRO

Receptor de código y hardware abiertos para radioaficionados. Recibe dos emisiones del satélite MRZN:

* **La imagen QSL**, transmitida en paquetes SSDV, que se reconstruye en un archivo JPEG.
* **La baliza de texto**, una trama AX.25 con el indicativo del satélite.

El receptor **solo recibe**: el firmware no tiene ninguna función de transmisión.

Contenido del repositorio:

* `hardware/`: las placas, en KiCad, con los archivos de fabricación.
* `firmware/`: el firmware del Pico, el código fuente y los `.uf2` listos para cargar.
* `host/`: los programas del PC, en Python: consola, decodificador, carga del firmware y autoprueba.

---

## 1. Qué transmite el satélite

| Emisión | Formato | Cuándo |
|---|---|---|
| Baliza de texto | Trama AX.25 UI con el texto `HR0UNA` (indicativo actual del satélite) | Cada 5 s durante los pases en que el satélite emite su baliza |
| Imagen QSL | Paquetes SSDV de 72 o 128 bytes; la imagen se repite en bucle durante el pase | Durante los pases QSL que programa el equipo del MRZN |

Durante un pase QSL el satélite emite solo la imagen. La baliza de texto sale en otros pases.

### Parámetros del enlace descendente

| | |
|---|---|
| Frecuencia | 435.500 MHz (fija en el firmware) |
| Modulación | 2GFSK, 9600 baudios, desviación ±2400 Hz |
| Paquete | preámbulo de bits alternados · sincronía `0x7E` · longitud N (1 byte) · N bytes de datos · 2 bytes de cola |

Los N bytes de datos son, según la emisión, un paquete SSDV o la trama AX.25. Cada uno trae su propia comprobación: el CRC-32 de SSDV y el FCS de AX.25. El receptor solo acepta lo que la supera.

---

## 2. Hardware

### Placas

| Carpeta | Placa | Firmware |
|---|---|---|
| `hardware/placa_un_modulo/` | Un Pico con un módulo RF4463PRO | `rx_qsl_un_modulo.uf2` |
| `hardware/placa_dos_modulos/` | Un Pico con dos módulos; para recibir se usa el U2 | `rx_qsl_dos_modulos.uf2` |

Para solo recibir basta la placa de un módulo. Cada carpeta tiene el proyecto de KiCad (`RF4463PRO.kicad_pro`) y, en `production/`, los Gerber (`.zip`), la lista de materiales (`_bom.csv`) y las posiciones de montaje (`_positions.csv`).

### Componentes (placa de un módulo)

| Ref. | Componente |
|---|---|
| A1 | Raspberry Pi Pico |
| U1 | Módulo **RF4463PRO de 433 MHz** (la huella de la placa se llama `RF4463PRO-915`; es la misma huella, pero el módulo tiene que ser el de 433 MHz) |
| J1 | Conector SMA de borde, para la antena |
| R1, R2 | 10 kΩ (0805), de nSEL y SDN a 3V3 |
| C1 | 47 µF electrolítico |
| J2..J5 | Tiras de pines para acceso a señales del Pico |

### Conexiones

Placa de un módulo (bus SPI0 del Pico):

| RF4463PRO | SDO | SDI | SCLK | nSEL | nIRQ | SDN | VCC | GND |
|---|---|---|---|---|---|---|---|---|
| Pico | GP16 | GP19 | GP18 | GP17 | GP2 | GP3 | 3V3 (pin 36) | GND |

Placa de dos módulos: se recibe con el U2, en el bus SPI1. El U1 queda apagado.

| U2 | SDO | SDI | SCLK | nSEL | nIRQ | SDN |
|---|---|---|---|---|---|---|
| Pico | GP12 | GP11 | GP10 | GP13 | GP15 | GP14 |

También se puede montar sin placa, con cables, siguiendo la tabla de un módulo.

### Antena

Una antena de 70 cm (430–440 MHz) conectada al SMA. Una Yagi apuntada al satélite da el mejor resultado. Un preamplificador (LNA) de 435 MHz junto a la antena mejora la recepción, sobre todo con cables largos.

---

## 3. Instalación del software

Requisitos: **Python 3.9 o superior** y **pyserial**.

```bash
pip install -r host/requirements.txt
```

### El programa ssdv

La imagen QSL se reconstruye con [ssdv](https://codeberg.org/fsphil/ssdv), un programa libre para codificar y decodificar imágenes SSDV. Su código está en `herramientas/ssdv/`.

* **Windows:** `herramientas/ssdv.exe` ya viene compilado.
* **Linux / macOS:** compilarlo una vez:

  ```bash
  cd herramientas/ssdv
  make
  ```

Sin ssdv el receptor funciona igual y guarda la imagen en `.ssdv`, pero no la convierte a JPEG.

---

## 4. Cargar el firmware en el Pico

Con el Pico conectado por USB:

```bash
python host/flashear.py                        # placa de un módulo
python host/flashear.py --placa dos_modulos    # placa de dos módulos
```

El programa pone el Pico en modo de carga sin pulsar BOOTSEL, copia el `.uf2` y comprueba que arranca. Tiene que terminar en `LISTO`.

**Si el Pico es nuevo** (sin firmware con puerto serie), el modo de carga automático no funciona:

1. Desconectar el Pico.
2. Conectarlo con el botón **BOOTSEL** pulsado; aparece una unidad USB llamada `RPI-RP2`.
3. Ejecutar otra vez `python host/flashear.py`, o copiar a mano `firmware/uf2/rx_qsl_un_modulo.uf2` a esa unidad.

### Compilar el firmware

Con [PlatformIO](https://platformio.org/):

```bash
cd firmware/rx_qsl
pio run
```

Los `.uf2` quedan en `.pio/build/un_modulo/` y `.pio/build/dos_modulos/`.

---

## 5. Comprobar el receptor

```bash
python host/consola.py --comprobar
```

Salida de un receptor correcto:

```
Conectado a COM5: rx_qsl 1.0, placa un_modulo (U1)
Comprobando el receptor...
  [OK   ] Pico conectado por USB con el firmware rx_qsl  -- COM5
  [OK   ] Modulo RF4463PRO bien conectado (chip, SPI, SDN, nIRQ)  -- AUTOPRUEBA TODO BIEN: U1 BIEN (chip ok, SPI ok, SDN ok, nIRQ ok)
  [OK   ] Radio configurada para el enlace (2GFSK 9600, 435.500 MHz)  -- Si4463 listo
  [OK   ] Radio escuchando
  [INFO ] Nivel de senal en el canal ahora mismo  -- RSSI 15 (unidades del chip); 1 detecciones en 3 s
Resultado: TODO BIEN, listo para recibir.
```

| Comprobación | Qué revisa |
|---|---|
| Pico conectado | Que el puerto USB responde con el firmware `rx_qsl` |
| Módulo bien conectado | Cable a cable, con el propio chip: que contesta, que el SPI lee y escribe bien 6 patrones, que SDN lo apaga y lo enciende y que nIRQ cambia de nivel |
| Radio configurada | Que la configuración leída del chip es la del enlace (modulación, sincronía, paquete) |
| Radio escuchando | Que el chip está en recepción |
| Nivel de señal | Informativo: el RSSI del canal y las detecciones de los últimos 3 s |

---

## 6. Recibir

```bash
python host/consola.py
```

Dentro de la consola:

| Orden | Qué hace |
|---|---|
| `puertos` | Lista los puertos serie y marca el Pico |
| `conectar [PUERTO]` | Conecta con el Pico (sin puerto, lo busca solo) |
| `comprobar` | Las comprobaciones de la sección 5 |
| `escuchar [MINUTOS]` | Recibe la QSL y la baliza; Ctrl+C para parar antes |
| `estado` | Contadores del receptor |
| `reiniciar` | Reinicia la radio del módulo |
| `cristal VALOR` | Ajuste fino del cristal del módulo (0..127; avanzado) |
| `salir` | Cerrar |

También sin entrar en la consola: `python host/consola.py --escuchar 15` (15 minutos).

Ejemplo de un pase:

```
[13:46:45] BALIZA  MRZN>HR0UNA  "HR0UNA"  (RSSI 104)
[13:46:49] BALIZA  MRZN>HR0UNA  "HR0UNA"  (RSSI 106)
...
[13:48:07] QSL     MRZN imagen 2: 640x480, paquetes de 72 B
[13:48:07] QSL     MRZN imagen 2: 1 paquetes (todavia sin el ultimo)  (RSSI 105)
[13:48:12] QSL     MRZN imagen 2: 25 paquetes (todavia sin el ultimo)  (RSSI 102)
...
[13:48:55] QSL     MRZN imagen 2: 200 paquetes (todavia sin el ultimo)  (RSSI 100)
[13:48:59] QSL     MRZN imagen 2: 234/234 paquetes (COMPLETA)  (RSSI 100)
```

Si se pierden paquetes, la línea del último paquete indica cuántos faltan (por ejemplo `219/234 paquetes (faltan 15)`), y la cuenta sigue subiendo mientras la imagen se repite.

Lo recibido se guarda en `recepciones/<fecha_hora UTC>/`:

| Archivo | Contenido |
|---|---|
| `qsl/MRZN_imagen002.jpg` | La imagen QSL reconstruida |
| `qsl/MRZN_imagen002.ssdv` | Los paquetes SSDV recibidos, en orden |
| `balizas.txt` | Cada baliza recibida: hora UTC, origen>destino, texto y RSSI |
| `resumen.txt` | El resumen de la sesión |

Las imágenes se guardan cada 30 s durante la escucha y al terminar. Una imagen incompleta se reconstruye igual, con las zonas que faltan sin dibujar; como el satélite repite la imagen en bucle, los huecos se van llenando durante el pase.

---

## 7. Cómo se decodifica

```
antena ─► RF4463PRO (Si4463)                      ─► USB ─► PC (host/)
          · demodula 2GFSK 9600                            · ¿paquete SSDV con CRC-32 bueno?  ─► imagen QSL
          · detecta preámbulo y sincronía 0x7E             · ¿trama AX.25 UI con FCS bueno?   ─► baliza de texto
          · entrega los N bytes de datos                   · cualquier otra cosa              ─► se descarta
```

### En el Pico (`firmware/rx_qsl`)

* El Si4463 hace la demodulación, la detección del preámbulo y de la sincronía, y la extracción del paquete por longitud variable: el primer byte (N) dice cuántos datos vienen.
* El firmware vacía el FIFO del chip por trozos mientras llega el paquete, de modo que caben los 128 bytes aunque el FIFO sea de 64.
* Cada paquete pasa al PC por USB con su RSSI.
* Un vigilante comprueba cada 250 ms que el chip sigue de verdad en recepción y lo rearma si no.

### En el PC (`host/decodificador.py`)

**Imagen QSL (SSDV).** Cada paquete SSDV sin FEC tiene esta forma:

| Bytes | Campo |
|---|---|
| 0 | Sincronía `0x55` |
| 1 | Tipo: `0x67` sin FEC (`0x66` con FEC) |
| 2–5 | Indicativo, en base 40 |
| 6 | Número de imagen |
| 7–8 | Número de paquete |
| 9 / 10 | Ancho / alto de la imagen, en bloques de 16 píxeles |
| 11 | Banderas (bit 2: último paquete de la imagen) |
| ... | Datos JPEG |
| últimos 4 | CRC-32 sobre los bytes 1 a L−5 (L = longitud del paquete SSDV) |

`host/imagenes.py` junta los paquetes por indicativo y número de imagen, los ordena y llama a `ssdv -d` para obtener el JPEG.

**Baliza de texto (AX.25).** La trama AX.25 puede llegar como flujo HDLC (banderas, relleno de bits, NRZI y aleatorizador G3RUH 1 + x¹² + x¹⁷) o directamente en bytes; el decodificador prueba las dos formas. Se acepta solo si:

1. el FCS (CRC-16/X.25) es correcto;
2. es una trama UI (control `0x03`, PID `0xF0`);
3. el campo de información es una cabecera de 4 bytes seguida de un texto ASCII terminado en `0x00`.

Se muestran el origen, el destino y el texto.

---

## 8. Autoprueba sin radio

```bash
python host/autoprueba.py
```

Comprueba los decodificadores con la imagen de ejemplo `ejemplos/qsl_ejemplo.ssdv`:

* paquetes perdidos, repetidos, desordenados y uno corrupto;
* la reconstrucción en JPEG;
* una baliza AX.25 en sus dos formas;
* que se descarta lo que no es QSL ni baliza.

Tiene que terminar en `AUTOPRUEBA: TODO BIEN`.

---

## 9. Protocolo USB del firmware

Para quien quiera escribir su propio programa: mensajes SLIP sobre el puerto serie USB (115200 baudios). El primer byte indica el tipo.

| Sentido | Mensaje | Significado |
|---|---|---|
| PC → Pico | `P` | Estado |
| | `H` | Autoprueba de las conexiones |
| | `I` | Reiniciar la radio |
| | `X` + valor | Ajuste fino del cristal (0..127) |
| | `L` | Volver a escuchar |
| Pico → PC | `R` + RSSI (1 byte) + datos | Paquete recibido |
| | `V` + texto | Información o estado |
| | `W` + texto | Aviso |
| | `E` + texto | Error |
| | `K` | Orden terminada |

El estado (`P`) es una línea de campos `nombre=valor` separados por `|`: `paquetes`, `parciales`, `sinc_falsas`, `rearmes`, `vigilante`, `rssi_ahora`, `rssi`, `xo`, `arranques`. El RSSI está en unidades del chip.

---

## 10. Problemas frecuentes

| Síntoma | Qué hacer |
|---|---|
| `No se encuentra ningun Raspberry Pi Pico` | Revisar el cable USB (que sea de datos, no solo de carga) y cargar el firmware (sección 4) |
| `could not open port` | Otro programa tiene el puerto abierto: cerrarlo |
| La autoprueba marca `SPI` o `chip` con FALLO | Revisar SDI, SDO, SCLK y nSEL, y la alimentación de 3V3 del módulo |
| La autoprueba marca `SDN` o `nIRQ` con FALLO | Revisar ese cable concreto (GP3 / GP2 en la placa de un módulo) |
| `Radio configurada` con FALLO | Reiniciar la radio (orden `reiniciar` en la consola) o desconectar y conectar el Pico |
| Todo OK pero no llega nada | Comprobar la antena y su orientación, que el satélite esté sobre el horizonte y que sea un pase en que transmite; un LNA ayuda |
| Llegan balizas pero muy pocas | Señal débil: mejor antena, LNA, o esperar a un pase más alto |
| La imagen sale en `.ssdv` pero no en `.jpg` | Falta el programa ssdv (sección 3) |

---

## 11. Estructura del repositorio

```
MRZN-QSL-Receiver/
├── firmware/
│   ├── rx_qsl/                 proyecto PlatformIO
│   │   ├── src/main.cpp        recepción, USB, autoprueba, vigilante
│   │   ├── include/rf4463_rx.h driver de recepción del Si4463
│   │   └── include/radio_config.h  configuración de radio del Si4463 (WDS)
│   └── uf2/                    firmware compilado, listo para cargar
├── host/
│   ├── consola.py              la consola del receptor
│   ├── enlace.py               comunicación USB con el Pico
│   ├── decodificador.py        SSDV y AX.25
│   ├── imagenes.py             reconstrucción de la imagen QSL
│   ├── flashear.py             carga del firmware
│   ├── autoprueba.py           prueba de los decodificadores sin radio
│   └── requirements.txt
├── hardware/
│   ├── placa_un_modulo/        KiCad + fabricación
│   └── placa_dos_modulos/      KiCad + fabricación
├── herramientas/
│   ├── ssdv/                   código de ssdv (GPL-3.0)
│   └── ssdv.exe                ssdv compilado para Windows
├── ejemplos/
│   └── qsl_ejemplo.ssdv        imagen de prueba para la autoprueba
├── licencias/                  textos de las licencias de terceros
├── AVISOS_DE_TERCEROS.md       qué se usa de terceros y con qué licencia
└── LICENSE                     GPL-3.0
```

---

## 12. Licencia y créditos

* Este proyecto se distribuye bajo la licencia **GPL-3.0-only** (archivo `LICENSE`), sin la cláusula "o posterior", porque el firmware contiene código derivado de RadioHead, que es GPL-3.0.
* El driver del Si4463 (`rf4463_rx.h`) es obra derivada del driver RH_RF24 de la biblioteca **RadioHead** (<https://www.airspayce.com/mikem/arduino/RadioHead/>): el envío de órdenes con espera de CTS, el reinicio por SDN y la lectura del FIFO por trozos. Su cabecera conserva el aviso de copyright de RadioHead y detalla qué se modificó.
* La configuración del modem (`radio_config.h`) se calculó con **WDS** (Wireless Development Suite) de Silicon Labs; el repositorio solo contiene los valores resultantes. Para usar otra frecuencia hay que recalcularla con WDS.
* **ssdv**: <https://codeberg.org/fsphil/ssdv> (antes en GitHub), GPL-3.0 o posterior. Su código va sin modificar y conserva sus avisos de copyright originales.
* El símbolo y la huella del RF4463PRO vienen de SnapEDA (SnapMagic), bajo CC BY-SA 4.0 con su "Design Exception 1.0".
* El logotipo de MORAZAN-SAT de la serigrafía no está incluido en la licencia: no se puede usar fuera de estas placas sin permiso del proyecto, y quien fabrique una placa puede quitarlo.
* Los `.uf2` llevan además el núcleo Arduino para el RP2040 (ArduinoCore-mbed), Mbed OS, el SDK del Pico y las bibliotecas de GCC. Sus licencias, la fuente de cada uno y cómo se compiló `ssdv.exe` están en [`AVISOS_DE_TERCEROS.md`](AVISOS_DE_TERCEROS.md), y los textos completos en `licencias/`.
