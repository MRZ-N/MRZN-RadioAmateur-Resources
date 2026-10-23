#!/usr/bin/env python3
"""
enlace.py -- comunicacion con el Pico del receptor por el puerto serie USB.

Mensajes SLIP; el primer byte de cada uno dice que es:

    PC -> Pico   'P' estado   'H' autoprueba   'I' reiniciar la radio
                 'X' + valor  ajuste fino del cristal (0..127)   'L' escuchar
    Pico -> PC   'R' + rssi + datos   paquete recibido
                 'V' texto    'W' aviso    'E' error    'K' orden terminada

Necesita pyserial (pip install pyserial).
"""

from __future__ import annotations

import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:                      # la autoprueba no lo necesita
    serial = None
    list_ports = None

VID_RASPBERRY = 0x2E8A

SLIP_END, SLIP_ESC, SLIP_ESC_END, SLIP_ESC_ESC = 0xC0, 0xDB, 0xDC, 0xDD


def puertos_pico() -> list[tuple[str, str]]:
    """Puertos serie que parecen un Raspberry Pi Pico: (puerto, descripcion)."""
    if list_ports is None:
        return []
    return [(p.device, p.description) for p in list_ports.comports()
            if p.vid == VID_RASPBERRY]


def todos_los_puertos() -> list[tuple[str, str]]:
    if list_ports is None:
        return []
    return [(p.device, p.description) for p in list_ports.comports()]


def slip(datos: bytes) -> bytes:
    out = bytearray([SLIP_END])
    for b in datos:
        if b == SLIP_END:
            out += bytes([SLIP_ESC, SLIP_ESC_END])
        elif b == SLIP_ESC:
            out += bytes([SLIP_ESC, SLIP_ESC_ESC])
        else:
            out.append(b)
    out.append(SLIP_END)
    return bytes(out)


class DecodificadorSlip:
    def __init__(self):
        self.buf, self.esc = bytearray(), False

    def byte(self, b: int) -> bytes | None:
        if b == SLIP_END:
            m = bytes(self.buf)
            self.buf.clear()
            self.esc = False
            return m or None
        if self.esc:
            self.buf.append(SLIP_END if b == SLIP_ESC_END else SLIP_ESC if b == SLIP_ESC_ESC else b)
            self.esc = False
        elif b == SLIP_ESC:
            self.esc = True
        else:
            self.buf.append(b)
        return None


def estado_a_dict(texto: str) -> dict:
    """'rx_qsl 1.0 | placa=... | radio=... | paquetes=12 | ...' -> dict."""
    d = {}
    partes = [p.strip() for p in texto.split("|")]
    if partes:
        d["version"] = partes[0]
    for p in partes[1:]:
        if "=" in p:
            k, v = p.split("=", 1)
            v = v.strip()
            try:
                d[k.strip()] = int(v)
            except ValueError:
                d[k.strip()] = v
    return d


class Receptor:
    """El Pico del receptor.  `serie` permite pasar otro objeto con write/read
    (por ejemplo, en pruebas); si no, se abre el puerto con pyserial."""

    def __init__(self, puerto: str, *, serie=None, log=None):
        self.puerto = puerto
        self._ser = serie
        self._dec = DecodificadorSlip()
        self._pendientes: list[tuple[str, bytes]] = []
        self.log = log or (lambda m: None)
        self.avisos: list[str] = []

    # ------------------------------------------------------------ puerto
    def abrir(self, espera: float = 2.5) -> list[str]:
        """Abre el puerto y recoge los mensajes de arranque."""
        if self._ser is None:
            if serial is None:
                raise RuntimeError("falta pyserial: pip install pyserial")
            self._ser = serial.Serial(self.puerto, 115200, timeout=0.05)
        lineas = []
        for tipo, d in self._leer(espera):
            if tipo in "VWE":
                lineas.append(d.decode("ascii", "replace"))
            elif tipo == "R":
                self._pendientes.append((tipo, d))
        return lineas

    def cerrar(self):
        if self._ser is not None:
            try:
                self._ser.close()
            except Exception:
                pass
            self._ser = None

    def _enviar(self, tipo: str, datos: bytes = b""):
        self._ser.write(slip(tipo.encode("ascii") + datos))

    def _leer(self, segundos: float, hasta_k: bool = False, hasta_v: bool = False):
        """Mensajes que llegan durante `segundos` (o hasta 'K' / el primer 'V')."""
        fin = time.monotonic() + segundos
        out = []
        while time.monotonic() < fin:
            datos = self._ser.read(4096)
            if not datos:
                time.sleep(0.005)
                continue
            for b in datos:
                m = self._dec.byte(b)
                if m:
                    tipo, cuerpo = chr(m[0]), m[1:]
                    out.append((tipo, cuerpo))
                    if (hasta_k and tipo == "K") or (hasta_v and tipo == "V"):
                        return out
        return out

    def _orden(self, tipo: str, datos: bytes = b"", espera: float = 3.0,
               hasta_k: bool = True, hasta_v: bool = False) -> list[tuple[str, str]]:
        self._enviar(tipo, datos)
        textos = []
        for t, d in self._leer(espera, hasta_k=hasta_k, hasta_v=hasta_v):
            if t == "R":
                self._pendientes.append((t, d))
            elif t in "VWE":
                textos.append((t, d.decode("ascii", "replace")))
        return textos

    # ----------------------------------------------------------- ordenes
    def estado(self) -> dict:
        """El estado del receptor ('P'), como diccionario."""
        self._enviar("P")
        fin = time.monotonic() + 2.0
        while time.monotonic() < fin:
            for t, d in self._leer(fin - time.monotonic(), hasta_v=True):
                if t == "R":
                    self._pendientes.append((t, d))
                elif t == "V":
                    s = d.decode("ascii", "replace")
                    if "|" in s:
                        return estado_a_dict(s)
        return {}

    def autoprueba(self) -> tuple[bool, list[str]]:
        """Revisa las conexiones del modulo cable a cable ('H')."""
        textos = self._orden("H", espera=8.0)
        lineas = [s for _t, s in textos]
        ok = any(s.startswith("AUTOPRUEBA TODO BIEN") for s in lineas)
        return ok, lineas

    def reiniciar(self) -> list[str]:
        return [s for _t, s in self._orden("I", espera=5.0)]

    def cristal(self, valor: int) -> list[str]:
        return [s for _t, s in self._orden("X", bytes([max(0, min(127, int(valor)))]))]

    def escuchar(self):
        self._orden("L", espera=1.0)

    # --------------------------------------------------------- recepcion
    def paquetes(self, segundos: float):
        """Genera (rssi, datos) de cada paquete que llega en `segundos`."""
        fin = time.monotonic() + segundos
        while self._pendientes:
            _t, d = self._pendientes.pop(0)
            if len(d) > 1:
                yield self._r(d)
        while time.monotonic() < fin:
            datos = self._ser.read(4096)
            if not datos:
                time.sleep(0.005)
                continue
            for b in datos:
                m = self._dec.byte(b)
                if not m:
                    continue
                tipo, cuerpo = chr(m[0]), m[1:]
                if tipo == "R" and len(cuerpo) > 1:
                    yield self._r(cuerpo)
                elif tipo in "WE":
                    self.avisos.append(cuerpo.decode("ascii", "replace"))

    @staticmethod
    def _r(d: bytes) -> tuple[int, bytes]:
        return d[0], bytes(d[1:])
