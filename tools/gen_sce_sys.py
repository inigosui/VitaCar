#!/usr/bin/env python3
"""Genera icon0.png y las imágenes de LiveArea en sce_sys/.

La Vita rechaza la instalación si estos PNG no son de paleta (8 bits),
así que se cuantizan al final. Requiere Pillow.
Uso: python3 tools/gen_sce_sys.py
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
SCE_SYS = ROOT / "sce_sys"
FONT_BOLD = ROOT / "assets" / "fonts" / "NotoSans-Bold.ttf"
FONT_REGULAR = ROOT / "assets" / "fonts" / "NotoSans-Regular.ttf"

BG = (12, 14, 18)
LOGO = ROOT / "assets" / "logo.png"  # generado con tools/gen_logo.py


def logo(size):
    """Logo con esquinas transparentes, escalado a size x size."""
    return Image.open(LOGO).convert("RGBA").resize((size, size), Image.LANCZOS)


def save_indexed(img, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    img.convert("RGB").quantize(colors=256, method=Image.Quantize.MEDIANCUT).save(path, optimize=True)
    print("generado", path.relative_to(ROOT))


def icon0():
    img = Image.new("RGBA", (128, 128), BG + (255,))
    img.alpha_composite(logo(128))
    save_indexed(img, SCE_SYS / "icon0.png")


def background():
    w, h = 840, 500
    img = Image.new("RGBA", (w, h), BG + (255,))
    d = ImageDraw.Draw(img)
    for y in range(h):  # degradado vertical suave
        t = y / h
        d.line([(0, y), (w, y)], fill=(int(12 + 14 * t), int(14 + 16 * t), int(18 + 24 * t), 255))
    img.alpha_composite(logo(240), (80, (h - 240) // 2))
    d.text((356, 175), "VitaCar", font=ImageFont.truetype(str(FONT_BOLD), 84), fill=(240, 242, 245))
    d.text((360, 285), "Tu salpicadero en la PS Vita",
           font=ImageFont.truetype(str(FONT_REGULAR), 28), fill=(146, 152, 165))
    save_indexed(img, SCE_SYS / "livearea" / "contents" / "bg.png")


def startup():
    w, h = 280, 158
    img = Image.new("RGBA", (w, h), BG + (255,))
    img.alpha_composite(logo(110), (14, (h - 110) // 2))
    d = ImageDraw.Draw(img)
    d.text((134, 52), "VitaCar", font=ImageFont.truetype(str(FONT_BOLD), 32), fill=(240, 242, 245))
    save_indexed(img, SCE_SYS / "livearea" / "contents" / "startup.png")


if __name__ == "__main__":
    icon0()
    background()
    startup()
