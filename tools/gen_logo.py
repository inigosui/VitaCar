#!/usr/bin/env python3
"""Genera assets/logo.png (1024x1024): consola portátil genérica con un mapa y una carretera.

Diseño propio, sin logotipos ni símbolos de terceros.
Uso: python3 tools/gen_logo.py
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "logo.png"

SIZE = 1024
SS = 3                      # supermuestreo para bordes suaves
S = SIZE * SS

BG_TOP = (30, 38, 56)
BG_BOTTOM = (10, 12, 18)
BODY = (44, 50, 62)
BODY_EDGE = (78, 86, 102)
SCREEN = (16, 19, 26)
MAP_BLOCK = (30, 35, 46)
ROAD = (70, 78, 94)
ROUTE = (10, 132, 255)
ACCENT = (255, 159, 10)
WHITE = (245, 247, 250)


def s(v):
    return int(v * SS)


def rounded_mask(size, radius):
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, size - 1, size - 1], radius, fill=255)
    return m


def background():
    img = Image.new("RGBA", (S, S))
    d = ImageDraw.Draw(img)
    for y in range(S):
        t = y / S
        c = tuple(int(BG_TOP[i] * (1 - t) + BG_BOTTOM[i] * t) for i in range(3))
        d.line([(0, y), (S, y)], fill=c + (255,))
    return img


def road(d):
    """Carretera en perspectiva que sale por debajo de la consola."""
    d.polygon([(s(300), S), (s(724), S), (s(560), s(700)), (s(464), s(700))], fill=(52, 58, 72, 255))
    for i in range(4):
        t0, t1 = 0.10 + i * 0.24, 0.22 + i * 0.24
        y0, y1 = s(700 + 324 * t0), s(700 + 324 * t1)
        w0, w1 = s(6 + 14 * t0), s(6 + 14 * t1)
        d.polygon([(S // 2 - w0, y0), (S // 2 + w0, y0), (S // 2 + w1, y1), (S // 2 - w1, y1)], fill=ACCENT + (255,))


def console(img):
    d = ImageDraw.Draw(img)
    x0, y0, x1, y1 = s(92), s(300), s(932), s(700)

    # Sombra
    shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle([x0, y0 + s(18), x1, y1 + s(18)], s(200), fill=(0, 0, 0, 140))
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(s(18))))

    d.rounded_rectangle([x0, y0, x1, y1], s(200), fill=BODY_EDGE + (255,))
    d.rounded_rectangle([x0 + s(8), y0 + s(8), x1 - s(8), y1 - s(8)], s(192), fill=BODY + (255,))

    # Pantalla con mapa
    sx0, sy0, sx1, sy1 = s(262), s(348), s(762), s(652)
    d.rounded_rectangle([sx0, sy0, sx1, sy1], s(22), fill=SCREEN + (255,))
    screen = Image.new("RGBA", (sx1 - sx0, sy1 - sy0), SCREEN + (255,))
    sd = ImageDraw.Draw(screen)
    w, h = screen.size
    for bx, by, bw, bh in [(30, 24, 120, 70), (180, 20, 130, 90), (340, 30, 140, 60),
                           (40, 150, 90, 110), (330, 130, 150, 120), (170, 200, 120, 80)]:
        sd.rounded_rectangle([s(bx), s(by), s(bx + bw), s(by + bh)], s(10), fill=MAP_BLOCK + (255,))
    for line in [[(0, 120), (500, 108)], [(150, 0), (160, 304)], [(310, 0), (300, 304)]]:
        sd.line([(s(x), s(y)) for x, y in line], fill=ROAD + (255,), width=s(14))
    route = [(250, 304), (250, 236), (300, 188), (300, 118), (380, 112), (440, 60)]
    sd.line([(s(x), s(y)) for x, y in route], fill=ROUTE + (255,), width=s(18), joint="curve")
    # Destino
    px, py = s(440), s(60)
    sd.ellipse([px - s(18), py - s(18), px + s(18), py + s(18)], fill=ACCENT + (255,))
    sd.ellipse([px - s(7), py - s(7), px + s(7), py + s(7)], fill=SCREEN + (255,))
    # Flecha de navegación
    ax, ay = s(250), s(250)
    arrow = [(0, -40), (30, 32), (0, 16), (-30, 32)]
    sd.polygon([(ax + x * SS * 1.35, ay + y * SS * 1.35) for x, y in arrow], fill=WHITE + (255,))
    sd.polygon([(ax + x * SS, ay + y * SS) for x, y in arrow], fill=ROUTE + (255,))
    mask = rounded_mask(max(w, h), s(22)).crop((0, 0, w, h))
    img.paste(screen, (sx0, sy0), mask)

    # Cruceta (izquierda)
    cx, cy, arm, thick = s(178), s(430), s(46), s(30)
    d.rounded_rectangle([cx - thick // 2, cy - arm, cx + thick // 2, cy + arm], s(8), fill=(24, 28, 36, 255))
    d.rounded_rectangle([cx - arm, cy - thick // 2, cx + arm, cy + thick // 2], s(8), fill=(24, 28, 36, 255))
    # Botones (derecha): cuatro puntos, sin símbolos
    bx, by, gap, r = s(846), s(430), s(40), s(17)
    for dx, dy in [(0, -1), (1, 0), (0, 1), (-1, 0)]:
        x, y = bx + dx * gap, by + dy * gap
        d.ellipse([x - r, y - r, x + r, y + r], fill=(24, 28, 36, 255))
    # Sticks
    for x in (s(178), s(846)):
        y = s(580)
        d.ellipse([x - s(36), y - s(36), x + s(36), y + s(36)], fill=(24, 28, 36, 255))
        d.ellipse([x - s(22), y - s(22), x + s(22), y + s(22)], fill=(60, 66, 80, 255))


def main():
    img = background()
    road(ImageDraw.Draw(img))
    console(img)
    img = img.resize((SIZE, SIZE), Image.LANCZOS)
    img.putalpha(rounded_mask(SIZE, int(SIZE * 0.22)))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    img.save(OUT)
    print("generado", OUT.relative_to(ROOT))


if __name__ == "__main__":
    main()
