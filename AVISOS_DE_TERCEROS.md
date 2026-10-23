# Avisos de terceros

Este repositorio se distribuye bajo la **GPL-3.0-only** (archivo `LICENSE`), salvo `herramientas/ssdv/`, que conserva su propia licencia (GPL-3.0 o posterior). Lo que sigue es todo lo que el repositorio contiene o usa de terceros, con su licencia y lo que exige.

Los textos completos de las licencias están en `licencias/`.

---

## 1. Firmware: código derivado de RadioHead

**Afecta a:** `firmware/rx_qsl/include/rf4463_rx.h` y, ya compilado, a `firmware/uf2/*.uf2`.

El driver del Si4463 parte del driver RH_RF24 de la biblioteca RadioHead (versión de referencia 1.145):

* Copyright (C) 2011-2022 Mike McCauley (RH_RF24.cpp: Copyright (C) 2011; RH_RF24.h: Copyright (C) 2014)
* <https://www.airspayce.com/mikem/arduino/RadioHead/>
* Licencia: GNU GPL versión 3. RadioHead también se ofrece con licencia comercial; aquí se usa bajo la GPL-3.0.
* La cabecera de `rf4463_rx.h` detalla qué se tomó y qué se modificó.

Por esta parte, el firmware se distribuye bajo la GPL-3.0 sin la cláusula "o posterior". RadioHead es una marca de su titular; este proyecto no está afiliado a él.

## 2. Firmware: configuración de radio calculada con WDS

**Afecta a:** `firmware/rx_qsl/include/radio_config.h`.

Los valores de las propiedades del Si4463 (filtro de canal, AGC, sintetizador) se calcularon con el calculador de modem de WDS (Wireless Development Suite) de Silicon Labs. El repositorio no incluye WDS ni ningún archivo, plantilla o código de Silicon Labs: solo los valores numéricos resultantes, en formato propio. Para otra configuración hay que obtener WDS de Silicon Labs y usarlo bajo sus términos. Silicon Labs y EZRadioPRO son marcas de Silicon Laboratories Inc.

## 3. Firmware compilado (`firmware/uf2/*.uf2`)

Además del código de este proyecto, los `.uf2` llevan enlazadas estas bibliotecas, tal como las instala PlatformIO (plataforma `raspberrypi` 1.18.0, `framework-arduino-mbed` 4.4.1, `toolchain-gccarmnoneeabi` 1.90201.191206):

| Componente | Licencia | Texto |
|---|---|---|
| ArduinoCore-mbed 4.4.1 (núcleo Arduino para el RP2040) | LGPL-2.1 o posterior | `licencias/LGPL-2.1.txt` |
| Mbed OS (la versión que trae ArduinoCore-mbed 4.4.1) | Apache-2.0; algunas partes MIT, Copyright (c) 2006-2019 ARM Limited | `licencias/Apache-2.0.txt`, `licencias/MIT_mbed-os.txt` |
| Raspberry Pi Pico SDK (dentro de Mbed OS) | BSD-3-Clause, Copyright (c) 2020-2022 Raspberry Pi (Trading) Ltd. | `licencias/BSD-3-Clause_pico-sdk.txt` |
| newlib 3.1.0 (biblioteca C) | varias licencias permisivas | `licencias/newlib.txt` |
| libgcc / libstdc++ (GCC 9.2.1) | GPL-3.0 con la excepción de biblioteca de ejecución de GCC 3.1 | `licencias/GCC-runtime-exception-3.1.txt` |

Todas son compatibles con distribuir el conjunto bajo la GPL-3.0.

**Fuente correspondiente de los `.uf2`.** El código de este proyecto y su configuración de compilación están en `firmware/rx_qsl/` (`pio run` en esa carpeta genera los dos `.uf2`). El código de las bibliotecas está en:

* ArduinoCore-mbed: <https://github.com/arduino/ArduinoCore-mbed> (etiqueta `4.4.1`), que incluye cómo se compila su Mbed OS (`mbed-os-to-arduino`)
* Mbed OS: <https://github.com/arduino/mbed-os>
* Raspberry Pi Pico SDK: <https://github.com/raspberrypi/pico-sdk>
* GCC, newlib: <https://developer.arm.com/downloads/-/gnu-rm> (GNU Arm Embedded Toolchain 9-2019-q4-major)

## 4. ssdv

**Afecta a:** `herramientas/ssdv/` (código sin modificar) y `herramientas/ssdv.exe`.

* <https://codeberg.org/fsphil/ssdv>
* `main.c`, `ssdv.c`, `ssdv.h`: Copyright 2011-2016 Philip Heron, GPL-3.0 o posterior (texto en `herramientas/ssdv/COPYING`).
* `rs8.c`, `rs8.h`: Copyright 2003 Phil Karn, LGPL; adaptado por el autor de ssdv.
* `ssdv.exe` se compiló sin cambios en el código, con Zig 0.16.0 (`pip install ziglang`), desde `herramientas/`:

  ```
  python -m ziglang cc -O2 -o ssdv.exe ssdv/main.c ssdv/ssdv.c ssdv/rs8.c
  ```

  Para Windows, Zig enlaza partes del runtime de mingw-w64 (ZPL-2.1, Copyright (c) 2009-2013 the mingw-w64 project; texto en `licencias/ZPL-2.1_mingw-w64.txt`).

## 5. Hardware (`hardware/`)

* Los símbolos, huellas y modelos 3D de las bibliotecas oficiales de KiCad tienen licencia CC-BY-SA 4.0 con una excepción que deja libres los diseños que los usan. Los modelos 3D solo se referencian; no van en el repositorio.
* El símbolo y la huella del módulo RF4463PRO-915 (fabricante G-NiceRF) vienen de SnapEDA (SnapMagic), bajo CC BY-SA 4.0 con la "Design Exception 1.0". El autor original y el enlace a la pieza figuran en las propiedades `CREATOR` y `SnapEDA_Link` del propio símbolo y de la huella, que se conservan: <https://www.snapeda.com/parts/RF4463PRO-915/G-nicerf/view-part/?ref=snap>
* **Logotipo.** El logotipo de MORAZAN-SAT que aparece en la serigrafía de las placas no está incluido en la licencia GPL-3.0 ni en ninguna otra licencia de este repositorio. Sus derechos son del proyecto MORAZAN-SAT y no se puede usar fuera de estas placas sin su permiso. Quien fabrique una placa puede quitarlo del diseño.

## 6. Dependencias que no van en el repositorio

* **pyserial** (BSD-3-Clause), para la comunicación USB: se instala aparte con `pip install pyserial`.
* **PlatformIO** y su cadena de compilación, para compilar el firmware.
