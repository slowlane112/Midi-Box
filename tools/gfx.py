"""
gfx.py - Python twin of the firmware renderer (main/ui/gfx.c).

Every primitive here has a 1:1 C equivalent, so a mockup drawn with it shows
exactly what the device will draw: same fonts, same icons, same pixels,
same RGB565 colour rounding.
"""
import json, os
from PIL import Image, ImageDraw
import icn

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_DIR = os.path.join(HERE, "..", "build_fonts")
ICON_DIR = os.path.join(HERE, "..", "assets")

UNI = {"·": 0x80, "…": 0x81, "°": 0x82, "–": 0x83, "▼": 0x84, "▲": 0x85}


def rgb565(c):
    """Round an (r,g,b) colour to what an RGB565 panel shows."""
    r, g, b = c
    r5, g6, b5 = r >> 3, g >> 2, b >> 3
    return ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))


def hexc(h):
    h = h.lstrip("#")
    return rgb565((int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)))


class Font:
    def __init__(self, name):
        d = json.load(open(os.path.join(FONT_DIR, name + ".json")))
        self.name = name
        self.line_h = d["line_h"]
        self.ascent = d["ascent"]
        self.g = {int(k): v for k, v in d["glyphs"].items()}

    def code(self, ch):
        return UNI.get(ch, ord(ch))

    def width(self, s):
        return sum(self.g.get(self.code(c), self.g[ord("?")])["adv"] for c in s)


_fonts = {}


def font(name):
    if name not in _fonts:
        _fonts[name] = Font(name)
    return _fonts[name]


_icons = {}


def icon_data(name):
    if name not in _icons:
        _icons[name] = icn.load(os.path.join(ICON_DIR, name + ".icn"))
    return _icons[name]


class Overflow(Exception):
    pass


class Gfx:
    def __init__(self, w, h, bg, name="screen"):
        self.w, self.h, self.name = w, h, name
        self.img = Image.new("RGB", (w, h), bg)
        self.d = ImageDraw.Draw(self.img)
        self.warnings = []

    # --- primitives (C: gfx_fill, gfx_rect, gfx_hline, gfx_vline, gfx_pixel) ---
    def fill(self, x, y, w, h, c):
        if w <= 0 or h <= 0:
            return
        self.d.rectangle([x, y, x + w - 1, y + h - 1], fill=c)

    def rect(self, x, y, w, h, c):
        self.d.rectangle([x, y, x + w - 1, y + h - 1], outline=c)

    def hline(self, x, y, w, c):
        self.fill(x, y, w, 1, c)

    def vline(self, x, y, h, c):
        self.fill(x, y, 1, h, c)

    def pixel(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.img.putpixel((x, y), c)

    def circle(self, cx, cy, r, c):
        # C: gfx_fill_circle (midpoint, filled)
        for yy in range(-r, r + 1):
            for xx in range(-r, r + 1):
                if xx * xx + yy * yy <= r * r + r:
                    self.pixel(cx + xx, cy + yy, c)

    def tri_right(self, x, y, h, c):
        # filled right-pointing triangle, height h (C: gfx_tri_right)
        for i in range(h):
            half = h // 2
            ww = (i if i <= half else h - 1 - i) + 1
            self.hline(x, y + i, ww, c)

    def tri_left(self, x, y, h, c):
        half = h // 2
        for i in range(h):
            ww = (i if i <= half else h - 1 - i) + 1
            self.hline(x + half + 1 - ww, y + i, ww, c)

    # --- text (C: gfx_text, gfx_text_width, gfx_text_fit) ---
    def text(self, fname, x, y, s, c, maxw=None, strict=True, align="left"):
        """Draw s with its top at y. Returns drawn width.
        maxw: available width. strict=True -> a fixed UI label must fit (reports overflow);
        strict=False -> user data (file names...) is cut with an ellipsis, as on device."""
        f = font(fname)
        wdt = f.width(s)
        if maxw is not None and wdt > maxw:
            if strict:
                self.warnings.append("%s: '%s' is %dpx, box %dpx (%s)" % (self.name, s, wdt, maxw, fname))
            ell = f.g[0x81]["adv"]
            while s and f.width(s) + ell > maxw:
                s = s[:-1]
            s = s + "…"
            wdt = f.width(s)
        if align == "center" and maxw is not None:
            x += (maxw - wdt) // 2
        elif align == "right" and maxw is not None:
            x += maxw - wdt
        elif align == "right":
            x -= wdt
        cx = x
        for ch in s:
            g = f.g.get(f.code(ch), f.g[ord("?")])
            for ry, row in enumerate(g["rows"]):
                for rx, bit in enumerate(row):
                    if bit == "1":
                        self.pixel(cx + g["x"] + rx, y + g["y"] + ry, c)
            cx += g["adv"]
        return wdt

    def width(self, fname, s):
        return font(fname).width(s)

    # --- icons (C: gfx_icon) ---
    def icon(self, name, x, y, c):
        w, h, px = icon_data(name)
        for r in range(h):
            for cc in range(w):
                if px[r][cc]:
                    self.pixel(x + cc, y + r, c)
        return w, h

    def save(self, path, scale=2):
        self.img.resize((self.w * scale, self.h * scale), Image.NEAREST).save(path)
