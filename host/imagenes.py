#!/usr/bin/env python3
"""
imagenes.py -- junta los paquetes SSDV de cada imagen QSL y la reconstruye en
JPEG con el programa ssdv (https://codeberg.org/fsphil/ssdv).

Los paquetes pueden llegar en cualquier orden, repetidos o con huecos: el
satelite repite la imagen en bucle durante el pase.  Con huecos, ssdv
reconstruye igual y deja esas zonas sin dibujar.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))


def buscar_ssdv() -> str | None:
    """El programa ssdv: el de herramientas/ del repositorio, o el del PATH."""
    nombre = "ssdv.exe" if os.name == "nt" else "ssdv"
    for p in (os.path.join(AQUI, "..", "herramientas", nombre),
              os.path.join(AQUI, "..", "herramientas", "ssdv", nombre)):
        p = os.path.normpath(p)
        if os.path.isfile(p):
            return p
    return shutil.which("ssdv")


class Imagenes:
    """Paquetes SSDV agrupados por (indicativo, imagen)."""

    def __init__(self, carpeta: str):
        self.carpeta = carpeta
        self.paquetes: dict[tuple, dict[int, bytes]] = {}
        self.info: dict[tuple, dict] = {}

    def agregar(self, paquete: bytes, d: dict) -> bool:
        """Guarda el paquete.  True si es nuevo (no repetido)."""
        k = (d["indicativo"], d["imagen"])
        paqs = self.paquetes.setdefault(k, {})
        nuevo = d["paquete"] not in paqs
        paqs[d["paquete"]] = bytes(paquete)
        inf = self.info.setdefault(k, {"longitud": d["longitud"], "ancho": d["ancho"],
                                       "alto": d["alto"], "ultimo": None})
        if d["ultimo"]:
            inf["ultimo"] = d["paquete"]
        return nuevo

    def completa(self, k: tuple) -> bool:
        total = self.info.get(k, {}).get("ultimo")
        return total is not None and len(self.paquetes.get(k, {})) == total + 1

    def progreso(self, k: tuple) -> str:
        paqs = self.paquetes.get(k, {})
        inf = self.info.get(k, {})
        total = inf.get("ultimo")
        if total is not None:
            faltan = [i for i in range(total + 1) if i not in paqs]
            return f"{len(paqs)}/{total + 1} paquetes" + (f" (faltan {len(faltan)})" if faltan else " (COMPLETA)")
        return f"{len(paqs)} paquetes (todavia sin el ultimo)"

    def guardar(self) -> list[str]:
        """Escribe cada imagen en .ssdv y, si esta el programa ssdv, en .jpg."""
        escritos = []
        if not self.paquetes:
            return escritos
        os.makedirs(self.carpeta, exist_ok=True)
        exe = buscar_ssdv()
        for (ind, img), paqs in sorted(self.paquetes.items()):
            base = os.path.join(self.carpeta, f"{ind}_imagen{img:03d}")
            with open(base + ".ssdv", "wb") as f:
                for i in sorted(paqs):
                    f.write(paqs[i])
            escritos.append(base + ".ssdv")
            if exe:
                n = self.info[(ind, img)]["longitud"]
                r = subprocess.run([exe, "-d", "-l", str(n), base + ".ssdv", base + ".jpg"],
                                   capture_output=True, text=True)
                if r.returncode == 0 and os.path.exists(base + ".jpg"):
                    escritos.append(base + ".jpg")
        return escritos

    def resumen(self) -> list[str]:
        return [f"{ind} imagen {img}: {self.progreso((ind, img))}"
                for (ind, img) in sorted(self.paquetes)]


if __name__ == "__main__":
    print("ssdv:", buscar_ssdv() or "no encontrado (ver README, seccion 'El programa ssdv')")
    sys.exit(0)
