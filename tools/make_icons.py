"""Draw the new icons in the same 4px-stroke style as the existing set and save as .icn."""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from PIL import Image, ImageDraw
import icn
OUT = os.path.join(os.path.dirname(__file__), "..", "assets")

def save(name, img):
    w, h = img.size
    px = [[1 if img.getpixel((x, y)) else 0 for x in range(w)] for y in range(h)]
    icn.save(os.path.join(OUT, name + ".icn"), w, h, px)

def canvas(s):
    i = Image.new("1", (s, s), 0); return i, ImageDraw.Draw(i)

# 64x64 studio: three track lanes with clips
i, d = canvas(64)
d.rounded_rectangle([4, 6, 59, 57], radius=8, outline=1, width=4)
for y, (a, b) in zip((17, 29, 41), ((12, 34), (24, 52), (12, 44))):
    d.rounded_rectangle([a, y, b, y + 6], radius=2, fill=1)
save("icon_studio_64x64", i)

# 64x64 folder
i, d = canvas(64)
d.rounded_rectangle([4, 12, 59, 53], radius=6, outline=1, width=4)
d.polygon([(6, 14), (6, 10), (10, 6), (24, 6), (30, 12)], fill=1)
d.line([(4, 22), (59, 22)], fill=1, width=4)
save("icon_folder_64x64", i)

# 16x16 eye / eye-off
i, d = canvas(16)
d.ellipse([1, 4, 14, 11], outline=1, width=2); d.ellipse([6, 6, 9, 9], fill=1)
save("icon_eye_16x16", i)
i, d = canvas(16)
d.ellipse([1, 4, 14, 11], outline=1, width=2); d.line([(2, 14), (13, 1)], fill=1, width=2)
save("icon_eye_off_16x16", i)

# 16x16 speaker-muted (speaker with x)
i, d = canvas(16)
d.polygon([(1, 5), (4, 5), (8, 1), (8, 14), (4, 10), (1, 10)], fill=1)
d.line([(10, 5), (14, 10)], fill=1, width=2); d.line([(14, 5), (10, 10)], fill=1, width=2)
save("icon_sound_off_16x16", i)

# 16x16 record dot
i, d = canvas(16)
d.ellipse([2, 2, 13, 13], fill=1)
save("icon_record_16x16", i)
print("icons ok")

# Music clefs for the Staff visualiser (FreeSerif has the real glyphs)
from PIL import ImageFont
def glyph_icon(name, ch, size, ttf="/usr/share/fonts/truetype/freefont/FreeSerif.ttf"):
    f = ImageFont.truetype(ttf, size)
    m = f.getmask(ch, mode="1")
    w, h = m.size
    i = Image.new("1", (w, h), 0)
    for y in range(h):
        for x in range(w):
            if m.getpixel((x, y)):
                i.putpixel((x, y), 1)
    i = i.crop(i.getbbox())
    save(name, i)
    return i.size

print("treble", glyph_icon("icon_clef_treble", "\U0001D11E", 42))
print("bass", glyph_icon("icon_clef_bass", "\U0001D122", 42))

# 64x64 Learn: a keyboard with a star above it (same 4px stroke)
import math
i, d = canvas(64)
d.rounded_rectangle([4, 26, 59, 59], radius=4, outline=1, width=4)
for k in range(1, 7):
    d.line([(4 + k * 8 - 1, 28), (4 + k * 8 - 1, 57)], fill=1, width=2)
for k in (0, 1, 3, 4, 5):
    d.rectangle([4 + k * 8 + 4, 28, 4 + k * 8 + 9, 44], fill=1)
pts = []
for k in range(10):
    a = -math.pi / 2 + k * math.pi / 5
    r = 12 if k % 2 == 0 else 5
    pts.append((32 + r * math.cos(a), 13 + r * math.sin(a)))
d.polygon(pts, fill=1)
save("icon_learn_64x64", i)
print("learn icon ok")
