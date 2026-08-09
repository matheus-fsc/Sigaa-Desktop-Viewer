#!/usr/bin/env python3
"""Gera src/ui/recursos/app.ico a partir do desenho de app.svg.

POR QUE EXISTE: o icone que aparece no Explorer e na barra de tarefas do
Windows nao vem do .qrc — vem do resource do proprio .exe, que so aceita .ico.
O Qt nunca le este arquivo; quem le e o Windows, antes de o app subir.

POR QUE NAO CONVERTE O SVG: converter exigiria cairosvg/rsvg no ambiente de
build so para gerar um arquivo que muda uma vez por ano. O desenho abaixo e
uma reproducao deliberada de src/ui/recursos/icones/app.svg — MEXEU NUM,
MEXA NO OUTRO. Rode com --check no pre-commit para saber se o .ico sumiu.

    python tools/gerar_icone.py            # regrava o .ico
    python tools/gerar_icone.py --check    # so verifica que existe e abre
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw

RAIZ = Path(__file__).resolve().parent.parent
DESTINO = RAIZ / "src" / "ui" / "recursos" / "app.ico"

# Desenhamos grande e reduzimos: a suavizacao do LANCZOS num 1024 sai melhor
# que qualquer antialiasing que o ImageDraw faria direto no 16x16.
LADO = 1024
S = LADO / 64.0  # o SVG usa viewBox 0 0 64 64

AZUL = (0x1F, 0x6F, 0xB2, 255)
AZUL_ESCURO = (0x0F, 0x4E, 0x80, 255)
BRANCO = (0xFF, 0xFF, 0xFF, 255)
AZUL_CLARO = (0x9C, 0xC4, 0xE4, 255)
VERDE = (0x2E, 0x9E, 0x5B, 255)

TAMANHOS = [(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)]


def e(*v: float) -> tuple[float, ...]:
    """Converte coordenadas do viewBox do SVG para pixels da tela grande."""
    return tuple(x * S for x in v)


def desenhar() -> Image.Image:
    img = Image.new("RGBA", (LADO, LADO), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    d.rounded_rectangle(e(2, 2, 62, 62), radius=14 * S, fill=AZUL)

    # Espiral do caderno: fica atras da folha, aparecendo so no topo.
    d.rounded_rectangle(e(21, 9, 25, 19), radius=2 * S, fill=AZUL_ESCURO)
    d.rounded_rectangle(e(39, 9, 43, 19), radius=2 * S, fill=AZUL_ESCURO)

    d.rounded_rectangle(e(15, 15, 49, 53), radius=4 * S, fill=BRANCO)

    for y in (26, 34):
        d.line(e(21, y, 31, y), fill=AZUL_CLARO, width=int(3 * S))

    d.line(
        [e(31, 39), e(36, 44), e(46, 31)],
        fill=VERDE,
        width=int(5 * S),
        joint="curve",
    )
    return img


def main() -> int:
    if "--check" in sys.argv:
        if not DESTINO.exists():
            print(f"FALTA: {DESTINO} — rode 'python tools/gerar_icone.py'")
            return 1
        with Image.open(DESTINO) as img:
            img.verify()
        print(f"ok: {DESTINO.relative_to(RAIZ)}")
        return 0

    DESTINO.parent.mkdir(parents=True, exist_ok=True)
    desenhar().save(DESTINO, format="ICO", sizes=TAMANHOS)
    print(f"gravado: {DESTINO.relative_to(RAIZ)} ({DESTINO.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
