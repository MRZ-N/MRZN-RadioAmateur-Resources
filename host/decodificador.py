#!/usr/bin/env python3
"""
decodificador.py -- reconoce, en cada paquete recibido, la imagen QSL (SSDV)
y la baliza de texto (AX.25).  Todo lo demas se descarta sin guardarlo.

Solo usa la biblioteca estandar de Python.

QSL (SSDV)
----------
La imagen QSL viaja en paquetes SSDV (https://codeberg.org/fsphil/ssdv) sin FEC
de 72 o 128 bytes.  Cada paquete se valida con su propio CRC-32:

    0x55 | tipo (0x67 sin FEC, 0x66 con FEC) | indicativo (4, base 40) |
    imagen | paquete (2) | ancho/16 | alto/16 | banderas | ... | CRC-32 (4)

Baliza de texto (AX.25)
-----------------------
Una trama AX.25 UI (control 0x03, PID 0xF0) con su FCS (CRC-16/X.25).  Puede
llegar como flujo HDLC con relleno de bits, NRZI y aleatorizador G3RUH
(1 + x^12 + x^17), o como la trama en bytes; se prueban las dos formas.  El
campo de informacion empieza con 4 bytes de cabecera que este programa no
usa, seguidos del texto ASCII terminado en 0x00.
"""

from __future__ import annotations

import struct
import zlib

# ============================================================== SSDV (QSL)

SSDV_SYNC = 0x55
SSDV_TIPOS = {0x66: "con FEC", 0x67: "sin FEC"}
SSDV_LONGITUDES = (72, 128, 256)


def ssdv_indicativo(b: bytes) -> str:
    """Indicativo SSDV (entero de 32 bits en base 40)."""
    x, s = struct.unpack(">I", b)[0], ""
    while x:
        x, d = divmod(x, 40)
        s += ("-" if d == 0 else chr(ord("0") + d - 1) if d < 11 else
              "-" if d < 14 else chr(ord("A") + d - 14))
    return s


def ssdv_paquete(raw: bytes) -> dict | None:
    """Datos del paquete SSDV si raw es un paquete entero con el CRC-32 bueno."""
    n = len(raw)
    if n < 20 or raw[0] != SSDV_SYNC or raw[1] not in SSDV_TIPOS:
        return None
    fin = n - 4 if raw[1] == 0x67 else n - 36           # con FEC: 32 bytes de paridad
    if fin <= 15 or zlib.crc32(raw[1:fin]) & 0xFFFFFFFF != struct.unpack(">I", raw[fin:fin + 4])[0]:
        return None
    return {"indicativo": ssdv_indicativo(raw[2:6]), "imagen": raw[6],
            "paquete": (raw[7] << 8) | raw[8], "ancho": raw[9] * 16, "alto": raw[10] * 16,
            "ultimo": bool(raw[11] & 0x04), "longitud": n, "fec": SSDV_TIPOS[raw[1]]}


def ssdv_buscar(datos: bytes):
    """Paquetes SSDV validos dentro de un bloque de bytes: (bytes, info)."""
    i = 0
    while i + 20 <= len(datos):
        if datos[i] == SSDV_SYNC and datos[i + 1] in SSDV_TIPOS:
            for n in SSDV_LONGITUDES:
                if i + n <= len(datos):
                    d = ssdv_paquete(datos[i:i + n])
                    if d:
                        yield bytes(datos[i:i + n]), d
                        i += n
                        break
            else:
                i += 1
            continue
        i += 1


# ======================================================== AX.25 (baliza)

def crc16_x25(data: bytes) -> int:
    """FCS de AX.25: CRC-16/X.25 (0x8408 reflejado, inicial y final 0xFFFF)."""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc ^ 0xFFFF


def _bits(datos: bytes, msb: bool):
    for b in datos:
        for i in range(8):
            yield (b >> (7 - i)) & 1 if msb else (b >> i) & 1


def _hdlc(bits) -> list[bytes]:
    """Desaleatorizador G3RUH + NRZI + HDLC: tramas con el FCS bueno."""
    hist, previo = 0, 1
    unos, datos, tramas = 0, [], []
    for s in bits:
        nivel = s ^ ((hist >> 11) & 1) ^ ((hist >> 16) & 1)       # 1 + x^12 + x^17
        hist = ((hist << 1) | s) & 0x1FFFF
        b = 1 if nivel == previo else 0                            # NRZI
        previo = nivel
        if b:
            unos += 1
            if unos <= 5:
                datos.append(1)
            continue
        if unos == 5:                         # cero de relleno
            unos = 0
            continue
        if unos == 6:                         # bandera 01111110
            cuerpo = datos[:-6]
            if len(cuerpo) >= 8 * 18 and len(cuerpo) % 8 == 0:
                t = bytes(sum(cuerpo[i + k] << k for k in range(8))
                          for i in range(0, len(cuerpo), 8))
                if crc16_x25(t[:-2]) == (t[-2] | (t[-1] << 8)):
                    tramas.append(t)
            datos, unos = [], 0
            continue
        if unos > 6:                          # aborto
            datos, unos = [], 0
            continue
        datos.append(0)
        unos = 0
    return tramas


def ax25_tramas(datos: bytes) -> list[bytes]:
    """Tramas AX.25 (con FCS incluida) dentro de un paquete, en cualquiera de
    las formas en que pueden llegar."""
    for msb in (True, False):
        t = _hdlc(_bits(datos, msb))
        if t:
            return t
    if len(datos) >= 18 and crc16_x25(datos[:-2]) == (datos[-2] | (datos[-1] << 8)):
        return [bytes(datos)]
    return []


def ax25_analizar(t: bytes) -> dict | None:
    """Direcciones, control, PID e informacion de una trama AX.25."""
    if len(t) < 18:
        return None
    cuerpo = t[:-2]
    dirs, i = [], 0
    while i + 7 <= len(cuerpo):
        d = cuerpo[i:i + 7]
        ind = "".join(chr(c >> 1) for c in d[:6]).strip()
        dirs.append((ind, (d[6] >> 1) & 0x0F))
        i += 7
        if d[6] & 1:
            break
    if len(dirs) < 2 or i + 2 > len(cuerpo):
        return None
    fmt = lambda d: d[0] + (f"-{d[1]}" if d[1] else "")
    return {"destino": fmt(dirs[0]), "origen": fmt(dirs[1]),
            "control": cuerpo[i], "pid": cuerpo[i + 1], "info": bytes(cuerpo[i + 2:])}


def _texto_imprimible(b: bytes) -> bool:
    return len(b) >= 1 and all(32 <= c < 127 for c in b)


def baliza_texto(t: bytes) -> dict | None:
    """La baliza de texto: trama UI cuyo campo de informacion es una cabecera
    de 4 bytes y un texto ASCII terminado en 0x00."""
    a = ax25_analizar(t)
    if a is None or a["control"] != 0x03 or a["pid"] != 0xF0:
        return None
    info = a["info"]
    if len(info) < 5:
        return None
    texto = info[4:].split(b"\x00")[0]
    if not _texto_imprimible(texto):
        return None
    return {"origen": a["origen"], "destino": a["destino"], "texto": texto.decode("ascii")}


# ============================================================ clasificar

def clasificar(datos: bytes) -> tuple[str, dict, bytes] | None:
    """("qsl", info, paquete_ssdv) o ("baliza", info, trama), o None si el
    paquete no es ninguna de las dos cosas (se descarta)."""
    datos = bytes(datos)
    s = ssdv_paquete(datos)
    if s:
        return "qsl", s, datos
    if len(datos) > 20:
        hallado = next(ssdv_buscar(datos), None)
        if hallado:
            return "qsl", hallado[1], hallado[0]
    for t in ax25_tramas(datos):
        b = baliza_texto(t)
        if b:
            return "baliza", b, t
    return None
