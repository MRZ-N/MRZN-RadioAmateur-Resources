#!/usr/bin/env python3
"""
flashear.py -- carga el firmware del receptor en el Raspberry Pi Pico, sin
tener que pulsar BOOTSEL, y comprueba que arranca.

    python host/flashear.py                       placa de un modulo
    python host/flashear.py --placa dos_modulos   placa de dos modulos
    python host/flashear.py --puerto COM5         si hay varios Pico conectados
    python host/flashear.py --uf2 otro.uf2        otro archivo

Como lo hace:
  1. abre el puerto serie del Pico a 1200 baudios y lo cierra: el Pico entra en
     su modo de carga y aparece como una unidad USB llamada RPI-RP2;
  2. copia el .uf2 a esa unidad; el Pico se reinicia solo con el firmware nuevo;
  3. espera a que vuelva el puerto serie y le pide su estado.

Si el Pico no tiene todavia un firmware con puerto serie (uno nuevo de fabrica),
el paso 1 no funciona: desconectalo, conectalo con el boton BOOTSEL pulsado y
vuelve a ejecutar este programa (o copia el .uf2 a mano a la unidad RPI-RP2).
"""

from __future__ import annotations

import argparse
import glob
import os
import shutil
import string
import sys
import time

AQUI = os.path.dirname(os.path.abspath(__file__))
if AQUI not in sys.path:
    sys.path.insert(0, AQUI)

import enlace                        # noqa: E402

UF2_DIR = os.path.normpath(os.path.join(AQUI, "..", "firmware", "uf2"))


def unidad_rpi_rp2() -> str | None:
    """La unidad de carga del Pico (contiene INFO_UF2.TXT)."""
    if os.name == "nt":
        import ctypes
        mascara = ctypes.windll.kernel32.GetLogicalDrives()
        candidatos = [f"{l}:\\" for i, l in enumerate(string.ascii_uppercase) if mascara >> i & 1]
    else:
        usuario = os.environ.get("USER", "")
        candidatos = (glob.glob("/Volumes/RPI-RP2*") + glob.glob(f"/media/{usuario}/RPI-RP2*")
                      + glob.glob(f"/run/media/{usuario}/RPI-RP2*") + glob.glob("/media/RPI-RP2*"))
    for c in candidatos:
        try:
            if os.path.isfile(os.path.join(c, "INFO_UF2.TXT")):
                return c
        except OSError:
            pass
    return None


def a_modo_carga(puerto: str) -> None:
    if enlace.serial is None:
        raise RuntimeError("falta pyserial: pip install pyserial")
    try:
        s = enlace.serial.Serial(puerto, 1200)
        time.sleep(0.2)
        s.close()
    except Exception:
        pass                         # el puerto desaparece justo al cerrarlo: es normal


def esperar(funcion, segundos: float):
    fin = time.monotonic() + segundos
    while time.monotonic() < fin:
        r = funcion()
        if r:
            return r
        time.sleep(0.3)
    return None


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Cargar el firmware del receptor en el Pico")
    ap.add_argument("--placa", choices=("un_modulo", "dos_modulos"), default="un_modulo")
    ap.add_argument("--uf2", help="archivo .uf2 (por defecto, el de la placa elegida)")
    ap.add_argument("--puerto", help="puerto serie del Pico (si hay varios)")
    a = ap.parse_args(argv)

    uf2 = a.uf2 or os.path.join(UF2_DIR, f"rx_qsl_{a.placa}.uf2")
    if not os.path.isfile(uf2):
        print(f"No existe {uf2}")
        return 1

    unidad = unidad_rpi_rp2()
    if unidad is None:
        puerto = a.puerto
        if puerto is None:
            picos = enlace.puertos_pico()
            if not picos:
                print("No se encuentra ningun Pico: ni por puerto serie ni como unidad RPI-RP2.")
                print("Conectalo con el boton BOOTSEL pulsado y vuelve a intentarlo.")
                return 1
            if len(picos) > 1:
                print("Hay varios Pico; elige uno con --puerto: " + ", ".join(p for p, _ in picos))
                return 1
            puerto = picos[0][0]
        print(f"1. {puerto} -> modo de carga (toque a 1200 baudios)")
        a_modo_carga(puerto)
        unidad = esperar(unidad_rpi_rp2, 15.0)
        if unidad is None:
            print("   No aparecio la unidad RPI-RP2.  Conecta el Pico con BOOTSEL pulsado y repite.")
            return 1
    print(f"2. Copiando {os.path.basename(uf2)} a {unidad}")
    shutil.copyfile(uf2, os.path.join(unidad, os.path.basename(uf2)))

    print("3. Esperando a que el Pico arranque...")
    time.sleep(2.0)
    picos = esperar(enlace.puertos_pico, 15.0)
    if not picos:
        print("   El Pico no volvio a aparecer por USB.  Desconectalo y conectalo de nuevo.")
        return 1
    puerto = a.puerto if a.puerto in [p for p, _ in picos] else picos[0][0]
    rx = enlace.Receptor(puerto)
    try:
        arranque = rx.abrir(espera=3.0)
        est = rx.estado()
    finally:
        rx.cerrar()
    for s in arranque:
        print("   " + s)
    if est.get("enlace") == "verificado":
        print(f"LISTO: {est.get('version')} en {puerto}, placa {est.get('placa')}, radio "
              f"{est.get('radio')}.")
        print("Siguiente paso: python host/consola.py --comprobar")
        return 0
    print(f"El firmware arranco pero la radio no: {est.get('radio', 'sin respuesta')}")
    print("Revisa las conexiones del modulo (python host/consola.py --comprobar).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
