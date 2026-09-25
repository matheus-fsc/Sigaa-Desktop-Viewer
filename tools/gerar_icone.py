#!/usr/bin/env python3
"""Gera os icones de plataforma (.ico e .icns) a partir do mestre em icones/app/.

POR QUE EXISTE: o icone que aparece no Explorer e na barra de tarefas do
Windows nao vem do .qrc — vem do resource do proprio .exe, que so aceita .ico.
O Qt nunca le esse arquivo; quem le e o Windows, antes de o app subir. No macOS
vale o mesmo com outro nome: o Finder e o Dock leem o .icns declarado no
Info.plist do bundle, tambem antes de o processo existir.

O .icns E ESCRITO AQUI, e nao pelo `iconutil` da Apple, de proposito: o
`iconutil` so roda no macOS, e entao o icone do Mac so poderia ser regravado
num Mac — bem no projeto em que a maquina de desenvolvimento e Linux. O
formato e simples o bastante para nao valer essa dependencia: cabecalho, e um
bloco por tamanho com um PNG dentro.

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

import io
import struct
import sys
from pathlib import Path

from PIL import Image

RAIZ = Path(__file__).resolve().parent.parent
MESTRE = RAIZ / "src" / "ui" / "recursos" / "icones" / "app" / "original-1254.png"
DESTINO = RAIZ / "src" / "ui" / "recursos" / "app.ico"
DESTINO_ICNS = RAIZ / "src" / "ui" / "recursos" / "app.icns"

# Ate 256: o formato .ico nao define nada acima disso, e o Windows usa a maior
# camada que encontrar. Cada tamanho e reduzido do mestre, e nao da camada
# anterior — reduzir em cascata acumula borrao ate o 16 virar mancha.
TAMANHOS = [16, 32, 48, 64, 128, 256]

# Os blocos do .icns, na ordem em que o macOS os prefere. O codigo de quatro
# letras diz o tamanho E a densidade: 'ic11' e "16 pt em tela retina", ou seja
# 32 px de imagem. Sem os pares @2x o Dock de um MacBook escala o icone de 1x e
# o desenho sai macio — o defeito que todo mundo nota e ninguem sabe nomear.
#
# 1024 px sai de um mestre de 1254: e reducao, nunca ampliacao.
BLOCOS_ICNS = [
    ("icp4", 16),
    ("icp5", 32),
    ("ic11", 32),    # 16 @2x
    ("ic12", 64),    # 32 @2x
    ("ic07", 128),
    ("ic13", 256),   # 128 @2x
    ("ic08", 256),
    ("ic14", 512),   # 256 @2x
    ("ic09", 512),
    ("ic10", 1024),  # 512 @2x
]


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
    gerar_icns()


def gerar_icns() -> None:
    base = Image.open(MESTRE).convert("RGBA")

    blocos = []
    for tipo, lado in BLOCOS_ICNS:
        buf = io.BytesIO()
        # Cada tamanho reduzido do MESTRE, como no .ico: reduzir em cascata
        # acumula borrao ate o menor virar mancha.
        base.resize((lado, lado), Image.LANCZOS).save(buf, format="PNG")
        dados = buf.getvalue()
        blocos.append(tipo.encode("ascii")
                      + struct.pack(">I", len(dados) + 8)
                      + dados)

    corpo = b"".join(blocos)
    # O tamanho no cabecalho inclui os proprios 8 bytes. Errar isso produz um
    # arquivo que o Preview abre e o Finder ignora — falha silenciosa, que e a
    # pior de todas para um icone.
    DESTINO_ICNS.write_bytes(b"icns" + struct.pack(">I", len(corpo) + 8) + corpo)
    print(f"{DESTINO_ICNS.relative_to(RAIZ)}  ({DESTINO_ICNS.stat().st_size} bytes, "
          f"{len(BLOCOS_ICNS)} blocos)")


def checar() -> None:
    if not DESTINO.is_file():
        sys.exit(f"faltando: {DESTINO}")
    with Image.open(DESTINO) as img:
        # `.ico` que abre mas nao tem a camada grande passaria despercebido ate
        # alguem ver o icone borrado na barra de tarefas.
        if max(img.size) < 256:
            sys.exit(f"{DESTINO} nao tem camada de 256 px")
    print(f"ok: {DESTINO.relative_to(RAIZ)}")

    if not DESTINO_ICNS.is_file():
        sys.exit(f"faltando: {DESTINO_ICNS}")
    # Abrir de verdade, e nao so conferir a assinatura: o .icns e escrito byte
    # a byte aqui, entao o teste que vale e um leitor independente aceita-lo.
    with Image.open(DESTINO_ICNS) as img:
        if max(img.size) < 512:
            sys.exit(f"{DESTINO_ICNS} nao tem camada grande")
    print(f"ok: {DESTINO_ICNS.relative_to(RAIZ)}")


if __name__ == "__main__":
    checar() if "--check" in sys.argv else gerar()
