#!/usr/bin/env python3
"""Gera src/ui/recursos/app.ico a partir do mestre em icones/app/.

POR QUE EXISTE: o icone que aparece no Explorer e na barra de tarefas do
Windows nao vem do .qrc — vem do resource do proprio .exe, que so aceita .ico.
O Qt nunca le este arquivo; quem le e o Windows, antes de o app subir.

DESDE 23/09/2026 o desenho e um PNG (icones/app/original-1254.png) e nao mais
um vetor redesenhado a mao aqui dentro. As duas versoes deste arquivo viviam
do aviso "MEXEU NUM, MEXA NO OUTRO", que e o tipo de acordo que ninguem
cumpre: agora ha um mestre so, e tudo deriva dele.

    python tools/gerar_icone.py            # regrava o .ico
    python tools/gerar_icone.py --check    # so verifica que existe e abre

Equivalente com ImageMagick, se o Pillow nao estiver a mao — ver o README em
src/ui/recursos/icones/app/.
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image

RAIZ = Path(__file__).resolve().parent.parent
MESTRE = RAIZ / "src" / "ui" / "recursos" / "icones" / "app" / "original-1254.png"
DESTINO = RAIZ / "src" / "ui" / "recursos" / "app.ico"

# Ate 256: o formato .ico nao define nada acima disso, e o Windows usa a maior
# camada que encontrar. Cada tamanho e reduzido do mestre, e nao da camada
# anterior — reduzir em cascata acumula borrao ate o 16 virar mancha.
TAMANHOS = [16, 32, 48, 64, 128, 256]


def gerar() -> None:
    if not MESTRE.is_file():
        sys.exit(f"mestre nao encontrado: {MESTRE}")

    base = Image.open(MESTRE).convert("RGBA")
    # `sizes=` sozinho: o Pillow gera cada camada a partir DESTA imagem. Juntar
    # `append_images` aqui faz as duas vias brigarem e o arquivo sai com uma
    # camada so — 900 bytes em vez de 100 KB, e o Windows escalando um 16x16
    # para o tamanho do Explorer.
    base.save(DESTINO, format="ICO", sizes=[(t, t) for t in TAMANHOS])
    print(f"{DESTINO.relative_to(RAIZ)}  ({DESTINO.stat().st_size} bytes, "
          f"{len(TAMANHOS)} tamanhos)")


def checar() -> None:
    if not DESTINO.is_file():
        sys.exit(f"faltando: {DESTINO}")
    with Image.open(DESTINO) as img:
        # `.ico` que abre mas nao tem a camada grande passaria despercebido ate
        # alguem ver o icone borrado na barra de tarefas.
        if max(img.size) < 256:
            sys.exit(f"{DESTINO} nao tem camada de 256 px")
    print(f"ok: {DESTINO.relative_to(RAIZ)}")


if __name__ == "__main__":
    checar() if "--check" in sys.argv else gerar()
