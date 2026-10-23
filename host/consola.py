#!/usr/bin/env python3
"""
consola.py -- consola del receptor de la QSL y la baliza de texto del MRZN.

    python host/consola.py                     consola interactiva
    python host/consola.py --comprobar         comprobar el receptor y salir
    python host/consola.py --escuchar 15       escuchar 15 minutos y salir
    python host/consola.py --puerto COM5 ...   elegir el puerto (si no, se busca el Pico)

Ordenes de la consola:

    puertos               lista los puertos serie
    conectar [PUERTO]     conecta con el Pico (sin puerto, lo busca solo)
    comprobar             Pico conectado, conexiones del modulo, radio configurada
                          y escuchando
    estado                contadores del receptor
    reiniciar             reinicia la radio del modulo
    escuchar [MINUTOS]    recibe la QSL y la baliza (Ctrl+C para parar antes)
    cristal VALOR         ajuste fino del cristal del modulo (0..127, avanzado)
    ayuda                 esta ayuda
    salir

Lo recibido se guarda en recepciones/<fecha_hora>/ (horas en UTC):
    qsl/<INDICATIVO>_imagenNNN.ssdv y .jpg    la imagen QSL
    balizas.txt                               cada baliza de texto recibida
    resumen.txt                               el resumen de la sesion
"""

from __future__ import annotations

import argparse
import os
import sys
import time

AQUI = os.path.dirname(os.path.abspath(__file__))
if AQUI not in sys.path:
    sys.path.insert(0, AQUI)

import decodificador as dec          # noqa: E402
import enlace                        # noqa: E402
from imagenes import Imagenes, buscar_ssdv   # noqa: E402

CARPETA_RECEPCIONES = os.path.normpath(os.path.join(AQUI, "..", "recepciones"))
GUARDAR_CADA_S = 30.0


def hora() -> str:
    return time.strftime("%H:%M:%S", time.gmtime(time.time()))


class Consola:
    def __init__(self, puerto: str | None = None, carpeta: str = CARPETA_RECEPCIONES,
                 serie=None, salida=print):
        self.puerto = puerto
        self.carpeta = carpeta
        self.serie = serie               # para pruebas: un puerto que no es pyserial
        self.rx: enlace.Receptor | None = None
        self.p = salida

    # ------------------------------------------------------------ conexion
    def conectar(self, puerto: str | None = None) -> bool:
        if self.rx is not None:
            self.rx.cerrar()
            self.rx = None
        puerto = puerto or self.puerto
        if puerto is None:
            picos = enlace.puertos_pico()
            if len(picos) == 1:
                puerto = picos[0][0]
            elif not picos:
                self.p("No se encuentra ningun Raspberry Pi Pico conectado por USB.")
                self.p("Conecta el receptor, o indica el puerto: conectar COMx")
                return False
            else:
                self.p("Hay varios Pico conectados; elige uno: "
                       + ", ".join(p for p, _d in picos))
                return False
        try:
            self.rx = enlace.Receptor(puerto, serie=self.serie)
            arranque = self.rx.abrir()
        except Exception as e:
            self.p(f"No se pudo abrir {puerto}: {e}")
            self.rx = None
            return False
        self.puerto = puerto
        est = self.rx.estado()
        if not est:
            self.p(f"{puerto} abierto, pero el Pico no contesta.  Puede que no tenga el "
                   "firmware rx_qsl: python host/flashear.py")
            for s in arranque:
                self.p("   " + s)
            return False
        self.p(f"Conectado a {puerto}: {est.get('version', '?')}, placa {est.get('placa', '?')}")
        return True

    def _hace_falta_conexion(self) -> bool:
        if self.rx is None and not self.conectar():
            return False
        return True

    # ------------------------------------------------------------ ordenes
    def comprobar(self) -> bool:
        """Todas las comprobaciones, con OK / FALLO en cada una."""
        res = []

        def linea(nombre, ok, detalle=""):
            if ok is not None:
                res.append(ok)
            marca = "INFO " if ok is None else ("OK   " if ok else "FALLO")
            self.p(f"  [{marca}] {nombre}" + (f"  -- {detalle}" if detalle else ""))

        self.p("Comprobando el receptor...")
        conectado = self._hace_falta_conexion()
        linea("Pico conectado por USB con el firmware rx_qsl", conectado, self.puerto or "")
        if not conectado:
            return False

        ok, lineas = self.rx.autoprueba()
        detalle = next((s for s in lineas if s.startswith("AUTOPRUEBA")), "sin respuesta")
        linea("Modulo RF4463PRO bien conectado (chip, SPI, SDN, nIRQ)", ok, detalle)
        if not ok:
            for s in lineas:
                self.p("      " + s)

        est = self.rx.estado()
        linea("Radio configurada para el enlace (2GFSK 9600, 435.500 MHz)",
              est.get("enlace") == "verificado", str(est.get("radio", "")))
        linea("Radio escuchando", est.get("escuchando") == "si")

        f0, p0 = est.get("sinc_falsas", 0), est.get("paquetes", 0)
        time.sleep(3.0)
        est2 = self.rx.estado()
        actividad = (est2.get("sinc_falsas", 0) - f0) + (est2.get("paquetes", 0) - p0)
        linea("Nivel de senal en el canal ahora mismo", None,
              f"RSSI {est2.get('rssi_ahora', '?')} (unidades del chip); "
              f"{actividad} detecciones en 3 s")
        todo = all(res)
        self.p("Resultado: " + ("TODO BIEN, listo para recibir." if todo else
                                "hay fallos (ver arriba y la seccion 'Problemas' del README)."))
        if not buscar_ssdv():
            self.p("Aviso: no se encontro el programa ssdv; las imagenes se guardaran en .ssdv"
                   " sin convertir a JPEG (ver README).")
        return todo

    def estado(self):
        if not self._hace_falta_conexion():
            return
        est = self.rx.estado()
        for k, v in est.items():
            self.p(f"  {k:12} {v}")

    def reiniciar(self):
        if not self._hace_falta_conexion():
            return
        for s in self.rx.reiniciar():
            self.p("  " + s)

    def cristal(self, valor: int):
        if not self._hace_falta_conexion():
            return
        for s in self.rx.cristal(valor):
            self.p("  " + s)

    def escuchar(self, minutos: float | None = None) -> dict:
        """Recibe hasta `minutos` (o hasta Ctrl+C).  Devuelve el resumen."""
        if not self._hace_falta_conexion():
            return {}
        sesion = time.strftime("%Y-%m-%d_%H%M%S", time.gmtime(time.time()))
        carpeta = os.path.join(self.carpeta, sesion)
        os.makedirs(carpeta, exist_ok=True)
        imgs = Imagenes(os.path.join(carpeta, "qsl"))
        balizas_txt = os.path.join(carpeta, "balizas.txt")
        n_balizas, n_qsl, nuevos_qsl = 0, 0, 0
        self.p(f"Escuchando{f' {minutos:.1f} min' if minutos else ''} (Ctrl+C para parar; "
               f"horas en UTC).  Se guarda en {carpeta}")
        self.rx.escuchar()
        fin = time.monotonic() + (minutos * 60.0 if minutos else 10 ** 9)
        ultimo_guardado = time.monotonic()
        try:
            while time.monotonic() < fin:
                for rssi, datos in self.rx.paquetes(min(1.0, fin - time.monotonic())):
                    r = dec.clasificar(datos)
                    if r is None:
                        continue                         # no es QSL ni baliza
                    tipo, d, contenido = r
                    if tipo == "baliza":
                        n_balizas += 1
                        txt = (f"{time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime(time.time()))} UTC  "
                               f"{d['origen']}>{d['destino']}  \"{d['texto']}\"  "
                               f"RSSI {rssi}")
                        with open(balizas_txt, "a", encoding="utf-8") as f:
                            f.write(txt + "\n")
                        self.p(f"[{hora()}] BALIZA  {d['origen']}>{d['destino']}  \"{d['texto']}\""
                               f"  (RSSI {rssi})")
                    else:
                        n_qsl += 1
                        k = (d["indicativo"], d["imagen"])
                        primero = k not in imgs.paquetes
                        nuevo = imgs.agregar(contenido, d)
                        if nuevo:
                            nuevos_qsl += 1
                        if primero:
                            self.p(f"[{hora()}] QSL     {d['indicativo']} imagen {d['imagen']}: "
                                   f"{d['ancho']}x{d['alto']}, paquetes de {d['longitud']} B")
                        if nuevo and (primero or d["ultimo"] or imgs.completa(k)
                                      or len(imgs.paquetes[k]) % 25 == 0):
                            self.p(f"[{hora()}] QSL     {d['indicativo']} imagen {d['imagen']}: "
                                   f"{imgs.progreso(k)}  (RSSI {rssi})")
                    if time.monotonic() - ultimo_guardado > GUARDAR_CADA_S:
                        imgs.guardar()
                        ultimo_guardado = time.monotonic()
        except KeyboardInterrupt:
            self.p("\nParado.")
        escritos = imgs.guardar()
        resumen = {"carpeta": carpeta, "balizas": n_balizas, "paquetes_qsl": n_qsl,
                   "paquetes_qsl_distintos": nuevos_qsl, "imagenes": imgs.resumen(),
                   "archivos": escritos}
        lineas = [f"Sesion {sesion} (UTC)", f"Balizas de texto: {n_balizas}",
                  f"Paquetes de la QSL: {n_qsl} ({nuevos_qsl} distintos)"]
        lineas += ["  " + s for s in imgs.resumen()]
        lineas += ["Archivos:"] + ["  " + os.path.relpath(a, carpeta) for a in escritos]
        with open(os.path.join(carpeta, "resumen.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(lineas) + "\n")
        self.p("\n".join(lineas))
        return resumen

    # ------------------------------------------------------------ bucle
    def interactiva(self):
        self.p(__doc__.split("Ordenes de la consola:")[0].strip().splitlines()[0])
        self.p("Escribe 'ayuda' para ver las ordenes.\n")
        while True:
            try:
                linea = input("receptor> ").strip()
            except (EOFError, KeyboardInterrupt):
                self.p("")
                break
            if not linea:
                continue
            orden, *args = linea.split()
            orden = orden.lower()
            try:
                if orden in ("salir", "exit", "quit"):
                    break
                elif orden == "ayuda":
                    self.p("Ordenes de la consola:" + __doc__.split("Ordenes de la consola:")[1]
                           .split("Lo recibido")[0])
                elif orden == "puertos":
                    picos = {p for p, _d in enlace.puertos_pico()}
                    for p, d in enlace.todos_los_puertos():
                        self.p(f"  {p:10} {d}" + ("   <- Raspberry Pi Pico" if p in picos else ""))
                elif orden == "conectar":
                    self.conectar(args[0] if args else None)
                elif orden == "comprobar":
                    self.comprobar()
                elif orden == "estado":
                    self.estado()
                elif orden == "reiniciar":
                    self.reiniciar()
                elif orden == "escuchar":
                    self.escuchar(float(args[0]) if args else None)
                elif orden == "cristal" and args:
                    self.cristal(int(args[0]))
                else:
                    self.p("Orden desconocida.  Escribe 'ayuda'.")
            except Exception as e:
                self.p(f"Error: {e}")
        if self.rx is not None:
            self.rx.cerrar()


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Receptor de la QSL y la baliza de texto del MRZN")
    ap.add_argument("--puerto", help="puerto serie del Pico (si no, se busca solo)")
    ap.add_argument("--comprobar", action="store_true", help="comprobar el receptor y salir")
    ap.add_argument("--escuchar", type=float, metavar="MINUTOS", help="escuchar y salir")
    ap.add_argument("--carpeta", default=CARPETA_RECEPCIONES, help="donde guardar lo recibido")
    a = ap.parse_args(argv)
    c = Consola(a.puerto, a.carpeta)
    if a.comprobar or a.escuchar is not None:
        ok = True
        if a.comprobar:
            ok = c.comprobar()
        if a.escuchar is not None:
            c.escuchar(a.escuchar)
        if c.rx is not None:
            c.rx.cerrar()
        return 0 if ok else 1
    c.interactiva()
    return 0


if __name__ == "__main__":
    sys.exit(main())
