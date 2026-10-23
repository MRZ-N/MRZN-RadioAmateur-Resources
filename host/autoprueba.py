#!/usr/bin/env python3
"""
autoprueba.py -- comprueba los decodificadores sin radio ni satelite.

    python host/autoprueba.py

1. La imagen QSL de ejemplo (ejemplos/qsl_ejemplo.ssdv) entra paquete a paquete
   como si llegara por radio, con paquetes perdidos, repetidos y uno corrupto,
   y se reconstruye en JPEG.
2. Una baliza de texto AX.25 en las dos formas en que puede llegar.
3. Lo que no es ni QSL ni baliza se descarta.

Tiene que terminar en "AUTOPRUEBA: TODO BIEN".
"""

from __future__ import annotations

import os
import random
import shutil
import sys
import tempfile

AQUI = os.path.dirname(os.path.abspath(__file__))
if AQUI not in sys.path:
    sys.path.insert(0, AQUI)

import decodificador as dec          # noqa: E402
from imagenes import Imagenes, buscar_ssdv   # noqa: E402

EJEMPLO = os.path.normpath(os.path.join(AQUI, "..", "ejemplos", "qsl_ejemplo.ssdv"))

fallos = 0


def chk(ok: bool, texto: str):
    global fallos
    print(f"  [{'OK   ' if ok else 'FALLO'}] {texto}")
    if not ok:
        fallos += 1


# --------------------------------------------- vectores AX.25 (solo pruebas)
def _direccion(ind: str, ssid: int, ultimo: bool) -> bytes:
    return bytes(ord(c) << 1 for c in ind.ljust(6)[:6]) + bytes([0x60 | (ssid << 1) | (1 if ultimo else 0)])


def trama_ui(dest: str, origen: str, info: bytes) -> bytes:
    t = _direccion(dest, 0, False) + _direccion(origen, 0, True) + b"\x03\xF0" + info
    f = dec.crc16_x25(t)
    return t + bytes([f & 0xFF, f >> 8])


def flujo_hdlc(trama: bytes, msb: bool = True) -> bytes:
    """Banderas + relleno de bits + NRZI + aleatorizador G3RUH, en bytes."""
    bits = [0, 1, 1, 1, 1, 1, 1, 0] * 6
    unos = 0
    for b in trama:
        for k in range(8):
            x = (b >> k) & 1
            bits.append(x)
            unos = unos + 1 if x else 0
            if unos == 5:
                bits.append(0)
                unos = 0
    bits += [0, 1, 1, 1, 1, 1, 1, 0] * 2
    nivel, hist, salida = 1, 0, []
    for x in bits:
        nivel = nivel if x else 1 - nivel                       # NRZI
        s = nivel ^ ((hist >> 11) & 1) ^ ((hist >> 16) & 1)      # G3RUH
        hist = ((hist << 1) | s) & 0x1FFFF
        salida.append(s)
    salida += [0] * (-len(salida) % 8)
    out = bytearray()
    for i in range(0, len(salida), 8):
        byte = 0
        for k in range(8):
            byte |= salida[i + k] << (7 - k if msb else k)
        out.append(byte)
    return bytes(out)


def main() -> int:
    print("1. Imagen QSL de ejemplo")
    with open(EJEMPLO, "rb") as f:
        flujo = f.read()
    largo = next(n for n in dec.SSDV_LONGITUDES if dec.ssdv_paquete(flujo[:n]))
    paquetes = [flujo[i:i + largo] for i in range(0, len(flujo) - largo + 1, largo)]
    chk(len(paquetes) > 10, f"{len(paquetes)} paquetes de {largo} B en el ejemplo")

    rng = random.Random(7)
    perdidos = set(rng.sample(range(len(paquetes)), len(paquetes) // 10))
    llegan = [p for i, p in enumerate(paquetes) if i not in perdidos]
    llegan += llegan[:5]                                        # repetidos (el bucle del pase)
    rng.shuffle(llegan)
    malo = bytearray(paquetes[3])
    malo[20] ^= 0x40
    llegan.append(bytes(malo))

    tmp = tempfile.mkdtemp(prefix="autoprueba_")
    try:
        imgs = Imagenes(tmp)
        reconocidos = 0
        for p in llegan:
            r = dec.clasificar(p)
            if r and r[0] == "qsl":
                imgs.agregar(r[2], r[1])
                reconocidos += 1
        chk(reconocidos == len(llegan) - 1, f"{reconocidos} de {len(llegan)} reconocidos "
                                            "(el corrupto, descartado por su CRC-32)")
        k = next(iter(imgs.paquetes))
        chk(len(imgs.paquetes[k]) == len(paquetes) - len(perdidos),
            f"imagen {k[0]} {k[1]}: {imgs.progreso(k)}")
        r = dec.clasificar(b"\x01\x02" + paquetes[5] + b"\x03")
        chk(r is not None and r[0] == "qsl" and r[2] == paquetes[5],
            "paquete SSDV con bytes de mas alrededor: se encuentra y se recorta")
        escritos = imgs.guardar()
        if buscar_ssdv():
            jpg = [a for a in escritos if a.endswith(".jpg")]
            chk(bool(jpg) and os.path.getsize(jpg[0]) > 1000,
                f"JPEG reconstruido con ssdv ({os.path.getsize(jpg[0]) if jpg else 0} B)")
        else:
            print("  [AVISO] no esta el programa ssdv: no se prueba la conversion a JPEG")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print("2. Baliza de texto (AX.25)")
    t = trama_ui("CQ", "PRUEBA", b"\x00\x00\x00\x00" + b"HOLA DESDE LA AUTOPRUEBA\x00")
    for nombre, datos in (("HDLC + NRZI + G3RUH, MSB primero", flujo_hdlc(t, True)),
                          ("HDLC + NRZI + G3RUH, LSB primero", flujo_hdlc(t, False)),
                          ("trama en bytes", t)):
        r = dec.clasificar(datos)
        chk(r is not None and r[0] == "baliza" and r[1]["texto"] == "HOLA DESDE LA AUTOPRUEBA"
            and r[1]["origen"] == "PRUEBA", f"{nombre}: {r[1] if r else None}")
    roto = bytearray(t)
    roto[18] ^= 0x01
    chk(dec.clasificar(bytes(roto)) is None, "trama con el FCS malo: descartada")

    print("3. Lo que no es QSL ni baliza se descarta")
    binaria = trama_ui("CQ", "PRUEBA", bytes(range(1, 60)))
    chk(dec.clasificar(flujo_hdlc(binaria)) is None, "AX.25 con datos binarios: descartada")
    chk(dec.clasificar(b"TEXTO SUELTO SIN FORMATO") is None, "texto suelto: descartado")
    rng = random.Random(3)
    ruido = [bytes(rng.randrange(256) for _ in range(rng.randrange(1, 129))) for _ in range(300)]
    chk(all(dec.clasificar(x) is None for x in ruido), "300 paquetes de bytes al azar: descartados")

    print("AUTOPRUEBA: " + ("TODO BIEN" if fallos == 0 else f"{fallos} FALLOS"))
    return 0 if fallos == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
