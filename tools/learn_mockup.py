#!/usr/bin/env python3
"""
learn_mockup.py - pixel-exact mockups of the Learn section (piano practice with scoring).
Same primitives, fonts, icons and widgets as mockup.py; output in mockups/learn_*.png.
"""
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(__file__))
from gfx import Gfx, hexc
import mockup as M
from mockup import (BG, PANEL, LINE, TXT, DIM, ACC, ACC_BG, REC, AMB, WHITE_KEY, BLACK_KEY,
                    S1W, S1H, S2W, S2H, CONTENT_Y, status_bar, soft_keys, row, stat_box, progress, header2)

BLUE = hexc("#4DA3FF")       # left hand / "Good"
GOLD = hexc("#FFD24A")       # stars, new best
PERFECT, GOOD, MISS = ACC, BLUE, REC

M.OUT = os.path.join(os.path.dirname(__file__), "..", "mockups")
OUT = M.OUT


def save(g1, g2, name):
    g1.save(os.path.join(OUT, "learn_" + name + "_s1.png"))
    g2.save(os.path.join(OUT, "learn_" + name + "_s2.png"))
    M.ALL_WARN.extend(g1.warnings + g2.warnings)


# ------------------------------------------------------------------ helpers
def star(g, cx, cy, r, c, filled=True):
    """5-point star by scanline fill of the polygon."""
    pts = []
    for i in range(10):
        a = -math.pi / 2 + i * math.pi / 5
        rr = r if i % 2 == 0 else r * 0.45
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    ys = [p[1] for p in pts]
    for y in range(int(min(ys)), int(max(ys)) + 1):
        xs = []
        for i in range(10):
            (x1, y1), (x2, y2) = pts[i], pts[(i + 1) % 10]
            if (y1 <= y + 0.5 < y2) or (y2 <= y + 0.5 < y1):
                xs.append(x1 + (y + 0.5 - y1) * (x2 - x1) / (y2 - y1))
        xs.sort()
        for k in range(0, len(xs) - 1, 2):
            if filled:
                g.hline(int(round(xs[k])), y, int(round(xs[k + 1])) - int(round(xs[k])) + 1, c)
            else:
                g.pixel(int(round(xs[k])), y, c)
                g.pixel(int(round(xs[k + 1])), y, c)


def keyboard(g, lo, hi, y, h, lit=None, x0=None, kw=None, dim_out=None):
    """Keyboard from note lo..hi (white-key bounds). lit: {note: colour}. Returns {note: (x, w)}."""
    lit = lit or {}
    whites = [n for n in range(lo, hi + 1) if n % 12 in (0, 2, 4, 5, 7, 9, 11)]
    if kw is None:
        kw = (S2W - 12) // len(whites)
    if x0 is None:
        x0 = (S2W - kw * len(whites)) // 2
    pos = {}
    for i, n in enumerate(whites):
        x = x0 + i * kw
        pos[n] = (x, kw - 1)
        c = lit.get(n, WHITE_KEY)
        if dim_out and not (dim_out[0] <= n <= dim_out[1]) and n not in lit:
            c = hexc("#3A414C")
        g.fill(x, y, kw - 1, h, c)
    bw = max(3, kw * 6 // 10)
    for n in range(lo, hi + 1):
        if n % 12 in (1, 3, 6, 8, 10) and n - 1 in pos:
            x = pos[n - 1][0] + kw - bw // 2 - 1
            pos[n] = (x, bw)
            c = lit.get(n, BLACK_KEY)
            g.fill(x, y, bw, h * 6 // 10, c)
    return pos


def note_name(n):
    return ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][n % 12] + str(n // 12 - 1)


STEP = 4
Y_C4 = 70


def y_of(d):
    return Y_C4 - (d - 28) * STEP


def dia(n):
    return (n // 12 - 1) * 7 + [0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6][n % 12]


def grand_staff(g, x0=4, x1=S2W - 4, col=None, clefs=True):
    col = col or hexc("#5C6675")
    for i in range(5):
        g.hline(x0, y_of(30 + i * 2), x1 - x0, col)
        g.hline(x0, y_of(18 + i * 2), x1 - x0, col)
    g.vline(x0, y_of(38), y_of(18) - y_of(38) + 1, col)
    if clefs:
        g.icon("icon_clef_treble", x0 + 5, y_of(38) - 9, TXT)
        g.icon("icon_clef_bass", x0 + 3, y_of(26) - 1, TXT)


def note_head(g, x, y, c, big=False):
    rows = [0x03C, 0x0FE, 0x1FF, 0x1FF, 0x1FF, 0x0FE, 0x078]
    sc = 2 if big else 1
    for r, bits in enumerate(rows):
        for b in range(9):
            if bits & (0x100 >> b):
                g.fill(x - 4 * sc + b * sc, y - 3 * sc + r * sc, sc, sc, c)


def staff_note(g, x, n, c, big=False, ledger_col=None):
    d = dia(n)
    y = y_of(d)
    lc = ledger_col or hexc("#9AA4B2")
    w = 22 if big else 15
    if d == 28:
        g.hline(x - w // 2, y_of(28), w, lc)
    for l in range(40, d + 1, 2):
        g.hline(x - w // 2, y_of(l), w, lc)
    for l in range(16, d - 1, -2):
        g.hline(x - w // 2, y_of(l), w, lc)
    note_head(g, x, y, c, big)
    if n % 12 in (1, 3, 6, 8, 10):
        g.text("font_large" if big else "font_main", x - (26 if big else 14), y - (10 if big else 8), "#", c)


def learn_icon(g, x, y, c):
    """64x64 Learn icon: keyboard with a star (drawn; would become icon_learn_64x64.icn)."""
    g.rect(x + 4, y + 22, 56, 34, c)
    g.rect(x + 5, y + 23, 54, 32, c)
    for i in range(1, 7):
        g.vline(x + 5 + i * 8, y + 23, 32, c)
    for i in (0, 1, 3, 4, 5):
        g.fill(x + 10 + i * 8, y + 23, 5, 18, c)
    star(g, x + 32, y + 10, 10, c)


def combo_badge(g, x, y, n):
    g.text("font_small", x, y, "COMBO", DIM)
    g.text("font_large", x, y + 11, "x%d" % n, AMB)


# ================================================================== screens
def home():
    g = Gfx(S1W, S1H, BG, "home")
    status_bar(g, "MIDI Box", "DIN on")
    apps = ["Player", "Studio", "Learn", "Live In", "Transfer", "Settings"]
    sel = 2
    for i, n in enumerate(apps):
        y = CONTENT_Y + 4 + i * 32
        if i == sel:
            g.fill(0, y, 140, 30, ACC_BG); g.fill(0, y, 3, 30, ACC)
        g.text("font_large", 12, y + 6, n, ACC if i == sel else TXT, maxw=124)
    g.vline(146, CONTENT_Y + 4, 190, LINE)
    learn_icon(g, 146 + (174 - 64) // 2, 44, ACC)
    g.text("font_large", 150, 122, "Learn", TXT, maxw=166, align="center")
    g.text("font_small", 150, 146, "Piano practice games", DIM, maxw=166, align="center")
    g.text("font_small", 150, 160, "with scores and stars", DIM, maxw=166, align="center")
    soft_keys(g, ["Resume", None, None, None, "Open"])

    h = Gfx(S2W, S2H, BG, "home-2")
    h.text("font_huge", 10, 8, "Learn", ACC)
    h.text("font_small", 12, 44, "Practise, score points, beat your best", DIM)
    h.text("font_small", 12, 72, "THIS WEEK", DIM)
    days = [12, 0, 25, 18, 30, 8, 0]
    for i, m in enumerate(days):
        x = 12 + i * 27
        bh = m * 2
        h.fill(x, 128 - bh, 18, bh, ACC if m else LINE)
        h.text("font_small", x + 5, 131, "MTWTFSS"[i], DIM)
    h.vline(222, 10, 122, LINE)
    rows = [("Practice streak", "4 days", AMB), ("This week", "1 h 33 min", TXT), ("Stars earned", "41", GOLD),
            ("Songs learnt", "6", TXT), ("Best combo", "212", TXT), ("Keyboard", "61 keys", TXT)]
    for i, (k, v, c) in enumerate(rows):
        y = 10 + i * 20
        h.text("font_small", 232, y, k, DIM, maxw=100)
        h.text("font_small", S2W - 8, y, v, c, align="right")
    save(g, h, "home")


def menu():
    g = Gfx(S1W, S1H, BG, "menu")
    status_bar(g, "Learn", "61 keys · C2–C7")
    items = [("Play Along", "Songs with falling notes", "12 songs · 23 stars"),
             ("Exercises", "Scales, chords, arpeggios", "18 of 72 done"),
             ("Note Reading", "Read the staff, fast", "Best 2,340"),
             ("Ear Training", "Hear it, play it back", "Level 4")]
    sel = 0
    for i, (n, d, s) in enumerate(items):
        y = CONTENT_Y + 4 + i * 48
        row(g, y, 46, i == sel)
        g.text("font_large", 12, y + 5, n, ACC if i == sel else TXT, maxw=180)
        g.text("font_small", 12, y + 27, d, DIM, maxw=180)
        g.text("font_small", S1W - 10, y + 10, s, GOLD if "stars" in s else DIM, align="right")
    soft_keys(g, ["Back", None, None, "Keyboard", "Open"])

    h = Gfx(S2W, S2H, BG, "menu-2")
    h.text("font_huge", 10, 8, "Play Along", ACC)
    h.text("font_small", 12, 44, "Notes fall to the keyboard: play them as they land", DIM)
    # mini falling-note preview
    random.seed(4)
    for i in range(14):
        x = 14 + i * 14
        y = 62 + random.randint(0, 30)
        h.fill(x, y, 10, random.randint(8, 22), ACC if i % 3 else BLUE)
    h.hline(12, 108, 200, AMB)
    keyboard(h, 48, 72, 110, 22, x0=12, kw=13)
    h.vline(222, 10, 122, LINE)
    rows = [("Last played", "Fur Elise"), ("Best score", "48,210"), ("Stars", "★★☆"), ("Mode", "Wait"),
            ("Hand", "Right"), ("Speed", "80%")]
    for i, (k, v) in enumerate(rows):
        y = 10 + i * 20
        h.text("font_small", 232, y, k, DIM, maxw=96)
        if k == "Stars":
            for s in range(3):
                star(h, S2W - 44 + s * 14, y + 5, 6, GOLD if s < 2 else LINE)
        else:
            h.text("font_small", S2W - 8, y, v, TXT, align="right")
    save(g, h, "menu")


def keyrange(stage):
    g = Gfx(S1W, S1H, BG, "range")
    status_bar(g, "Your keyboard", "step %d of 2" % (1 if stage == 0 else 2))
    if stage == 0:
        g.text("font_main", 0, 40, "Press the", DIM, maxw=S1W, align="center")
        g.text("font_huge", 0, 58, "LOWEST", ACC, maxw=S1W, align="center")
        g.text("font_main", 0, 94, "key on your keyboard", DIM, maxw=S1W, align="center")
        g.text("font_small", 20, 140, "Every song and exercise is fitted to the keys", DIM, maxw=280, align="center")
        g.text("font_small", 20, 154, "you have, so this only needs doing once.", DIM, maxw=280, align="center")
        soft_keys(g, ["Cancel", None, None, None, None])
    else:
        g.text("font_main", 0, 34, "Your keyboard", DIM, maxw=S1W, align="center")
        g.text("font_huge", 0, 52, "61 keys", ACC, maxw=S1W, align="center")
        g.text("font_large", 0, 92, "C2 – C7", TXT, maxw=S1W, align="center")
        stat_box(g, 30, 128, 80, "Scales", "2 octaves")
        stat_box(g, 120, 128, 80, "Songs", "full range")
        stat_box(g, 210, 128, 80, "Middle C", "C4")
        g.text("font_small", 20, 176, "Using octave buttons? Run this again after", DIM, maxw=280, align="center")
        g.text("font_small", 20, 190, "changing octave so the notes line up.", DIM, maxw=280, align="center")
        soft_keys(g, ["Back", None, None, "Redo", "Save"])

    h = Gfx(S2W, S2H, BG, "range-2")
    if stage == 0:
        header2(h, "88-key piano · press any key to see where it lands")
        keyboard(h, 21, 108, 30, 100, lit={36: ACC}, kw=8)
        h.text("font_main", 0, 14, "", TXT)
    else:
        header2(h, "Your 61 keys on an 88-key piano", "C2 – C7")
        pos = keyboard(h, 21, 108, 30, 90, kw=8, dim_out=(36, 96))
        x0 = pos[36][0]; x1 = pos[96][0] + pos[96][1]
        h.fill(x0, 124, x1 - x0, 3, ACC)
        h.text("font_small", x0, 128, "C2", ACC)
        h.text("font_small", x1, 128, "C7", ACC, align="right")
        h.text("font_small", pos[60][0] - 4, 128, "C4", DIM)
    save(g, h, "range%d" % stage)


def play_setup():
    g = Gfx(S1W, S1H, BG, "setup")
    status_bar(g, "Play Along", "Fur Elise")
    fields = [("Hand", "Right (melody)"), ("Mode", "Wait for me"), ("Speed", "80%"),
              ("Loop", "Bars 1–8"), ("Count-in", "1 bar"), ("Right hand part", "Track 1 · Piano")]
    sel = 0
    for i, (k, v) in enumerate(fields):
        y = CONTENT_Y + 4 + i * 22
        row(g, y, 21, i == sel)
        g.text("font_main", 12, y + 4, k, ACC if i == sel else TXT, maxw=150)
        g.text("font_main", S1W - 12, y + 4, v, ACC if i == sel else DIM, align="right")
    g.text("font_small", 12, 160, "Wait: the song stops until you play the right note.", DIM, maxw=296)
    g.text("font_small", 12, 174, "The box plays the left hand for you.", DIM, maxw=296)
    g.fill(12, 194, 296, 16, PANEL)
    g.text("font_small", 18, 197, "Range E3–A5 · fits your keyboard", ACC, maxw=200)
    for s in range(3):
        star(g, 268 + s * 14, 202, 6, GOLD if s < 2 else LINE)
    soft_keys(g, ["Back", "-", "+", None, "Start"])

    h = Gfx(S2W, S2H, BG, "setup-2")
    header2(h, "Notes in this part", "33 bars · 2:40 at 80%")
    lit = {n: ACC for n in (64, 63, 59, 62, 60, 57, 69, 76, 75)}
    lit.update({n: BLUE for n in (45, 52, 40, 47)})
    pos = keyboard(h, 36, 96, 40, 70, kw=11, dim_out=(52, 81), lit=lit)
    h.fill(pos[52][0], 116, pos[81][0] + pos[81][1] - pos[52][0], 3, ACC)
    h.text("font_small", pos[52][0], 121, "right hand E3–A5", ACC)
    h.fill(12, 22, 10, 8, ACC); h.text("font_small", 26, 21, "you play", DIM)
    h.fill(90, 22, 10, 8, BLUE); h.text("font_small", 104, 21, "the box plays", DIM)
    save(g, h, "setup")


def playing(mode):
    g = Gfx(S1W, S1H, BG, "playing")
    status_bar(g, "Play Along", "Fur Elise · bar 12/33", title_col=ACC)
    # score
    g.text("font_small", 10, 26, "SCORE", DIM)
    g.text("font_huge", 10, 38, "18,450", TXT)
    combo_badge(g, 230, 26, 24 if mode == "play" else 7)
    # grade flash
    if mode == "play":
        g.fill(10, 78, 300, 30, ACC_BG)
        g.text("font_large", 0, 84, "PERFECT!", PERFECT, maxw=S1W, align="center")
    else:
        g.fill(10, 78, 300, 30, PANEL)
        g.text("font_main", 0, 86, "Waiting for  E5  (right hand)", AMB, maxw=S1W, align="center")
    # counts
    stat_box(g, 10, 116, 70, "Perfect", "64", PERFECT)
    stat_box(g, 86, 116, 70, "Good", "17", GOOD)
    stat_box(g, 162, 116, 70, "Miss", "3", MISS)
    stat_box(g, 238, 116, 72, "Accuracy", "96%")
    g.text("font_small", 10, 156, "Progress", DIM)
    progress(g, 10, 168, 300, 6, 12 / 33)
    g.text("font_small", 10, 180, "Speed 80% · Right hand · " + ("Play (scored)" if mode == "play" else "Wait"), DIM, maxw=300)
    for s in range(3):
        star(g, 268 + s * 14, 200, 6, GOLD if s < 2 else LINE)
    g.text("font_small", 10, 196, "On track for", DIM)
    soft_keys(g, ["Stop", ("shape", "rew"), ("shape", "playpause"), "Loop", "Hints"])

    # screen 2: falling notes
    h = Gfx(S2W, S2H, BG, "playing-2")
    lo, hi = 48, 84
    whites = [n for n in range(lo, hi + 1) if n % 12 in (0, 2, 4, 5, 7, 9, 11)]
    kw = 416 // len(whites)
    x0 = (S2W - kw * len(whites)) // 2
    KY = 112
    # lanes (C lines)
    xpos = {}
    for i, n in enumerate(whites):
        xpos[n] = x0 + i * kw
    for n in whites:
        if n % 12 == 0:
            h.vline(xpos[n], 0, KY, hexc("#1A2029"))

    def nx(n):
        if n in xpos:
            return xpos[n] + 1, kw - 3
        return xpos[n - 1] + kw - 3, 5

    seq = [(76, 0), (75, 10), (76, 20), (75, 30), (76, 40), (71, 50), (74, 60), (72, 70), (69, 80),
           (60, 100), (64, 110), (69, 120), (71, 140)]
    t_now = 42 if mode == "play" else 40
    hit_line = KY - 4
    for n, t in seq:
        y_bot = hit_line - (t - t_now) * 3
        hgt = 22
        x, w = nx(n)
        if y_bot < 0 or y_bot - hgt > hit_line:
            continue
        col = ACC
        if t < t_now - 2:
            continue
        top = max(0, y_bot - hgt)
        h.fill(x, top, w, min(y_bot, hit_line) - top, col)
        h.rect(x, top, w, min(y_bot, hit_line) - top, hexc("#8FF0B8"))
    # left-hand notes (box plays) in blue
    for n, t in [(45, 0), (52, 10), (57, 20), (52, 60), (56, 70), (59, 80)]:
        y_bot = hit_line - (t - t_now) * 3
        x, w = nx(n) if n >= lo else (None, None)
        if x is None or y_bot - 22 > hit_line or y_bot < 0:
            continue
        top = max(0, y_bot - 22)
        h.fill(x, top, w, min(y_bot, hit_line) - top, hexc("#20466E"))
    h.hline(0, hit_line, S2W, AMB if mode == "wait" else hexc("#5C6675"))
    lit = {76: ACC} if mode == "play" else {76: AMB}
    keyboard(h, lo, hi, KY, 30, lit=lit, x0=x0, kw=kw)
    if mode == "play":
        x, w = nx(76)
        cx = x + w // 2
        for a in range(8):  # hit sparkle
            dx = int(10 * math.cos(a * math.pi / 4)); dy = int(10 * math.sin(a * math.pi / 4))
            h.fill(cx + dx - 1, hit_line + dy // 2 - 6, 2, 2, GOLD)
        h.text("font_main", cx - 30, hit_line - 34, "PERFECT", PERFECT)
    else:
        x, w = nx(76)
        h.text("font_small", x - 20, hit_line - 14, "play E5", AMB)
    h.text("font_small", 6, 3, "18,450", TXT)
    h.text("font_small", S2W - 6, 3, "x%d" % (24 if mode == "play" else 7), AMB, align="right")
    save(g, h, "play_" + mode)


def results():
    g = Gfx(S1W, S1H, BG, "results")
    status_bar(g, "Results", "Fur Elise · right hand")
    for s in range(3):
        star(g, 120 + s * 40, 44, 16, GOLD)
    g.text("font_huge", 0, 66, "48,210", TXT, maxw=S1W, align="center")
    g.fill(110, 100, 100, 16, GOLD)
    g.text("font_main", 110, 102, "NEW BEST!", BG, maxw=100, align="center")
    stat_box(g, 10, 124, 70, "Perfect", "212", PERFECT)
    stat_box(g, 86, 124, 70, "Good", "31", GOOD)
    stat_box(g, 162, 124, 70, "Miss", "4", MISS)
    stat_box(g, 238, 124, 72, "Accuracy", "97%")
    stat_box(g, 10, 160, 146, "Best combo", "164")
    stat_box(g, 162, 160, 148, "Timing", "a little early")
    soft_keys(g, ["Done", "Retry", None, "Faster", "Next"])

    h = Gfx(S2W, S2H, BG, "results-2")
    random.seed(7)
    for i in range(60):  # confetti
        c = random.choice([ACC, BLUE, GOLD, AMB, REC])
        h.fill(random.randint(0, S2W - 4), random.randint(0, 50), random.choice([2, 3]), random.choice([2, 4]), c)
    h.text("font_huge", 0, 48, "3 stars!", GOLD, maxw=S2W, align="center")
    # timing histogram: early <- 0 -> late
    header2(h, "", "")
    base = 136
    h.text("font_small", 20, 84, "TIMING", DIM)
    vals = [1, 2, 4, 9, 16, 28, 40, 34, 22, 12, 6, 3, 2, 1]
    for i, v in enumerate(vals):
        x = 80 + i * 20
        c = PERFECT if 4 <= i <= 8 else GOOD
        h.fill(x, base - v, 14, v, c)
    h.vline(80 + 7 * 20 - 3, 96, 42, TXT)
    h.text("font_small", 20, 124, "early", DIM)
    h.text("font_small", S2W - 20, 124, "late", DIM, align="right")
    save(g, h, "results")


def exercises():
    g = Gfx(S1W, S1H, BG, "ex")
    status_bar(g, "Exercises", "18 of 72 done")
    cats = [("Major scales", 12, 9), ("Minor scales", 12, 4), ("Chords", 24, 5), ("Arpeggios", 24, 0)]
    for i, (n, tot, done) in enumerate(cats):
        y = CONTENT_Y + 4 + i * 26
        row(g, y, 25, i == 0)
        g.text("font_main", 12, y + 6, n, ACC if i == 0 else TXT, maxw=140)
        progress(g, 170, y + 10, 90, 5, done / tot)
        g.text("font_small", S1W - 10, y + 7, "%d/%d" % (done, tot), DIM, align="right")
    g.hline(0, 130, S1W, LINE)
    g.text("font_small", 12, 136, "KEY", DIM); g.text("font_small", 120, 136, "HAND", DIM); g.text("font_small", 220, 136, "TEMPO", DIM)
    g.text("font_large", 12, 150, "D major", TXT)
    g.text("font_large", 120, 150, "Both", TXT)
    g.text("font_large", 220, 150, "80 bpm", TXT)
    g.text("font_small", 12, 176, "2 octaves up and down · best ", DIM)
    for s in range(3):
        star(g, 170 + s * 14, 181, 6, GOLD if s < 2 else LINE)
    g.text("font_small", 12, 196, "Next: try it with the metronome at 90 bpm", AMB, maxw=296)
    soft_keys(g, ["Back", "Key -", "Key +", "Hand", "Start"])

    h = Gfx(S2W, S2H, BG, "ex-2")
    grand_staff(h, x1=S2W - 6)
    notes = [62, 64, 66, 67, 69, 71, 73, 74]
    for i, n in enumerate(notes):
        staff_note(h, 70 + i * 40, n, TXT if i else ACC)
        f = ["1", "2", "3", "1", "2", "3", "4", "5"][i]
        h.text("font_small", 67 + i * 40, 4, f, AMB)
    h.text("font_small", 6, S2H - 12, "D major · right hand fingering shown above", DIM)
    save(g, h, "ex")


def ex_playing():
    g = Gfx(S1W, S1H, BG, "explay")
    status_bar(g, "D major scale", "up · octave 1 of 2")
    g.text("font_small", 10, 26, "SCORE", DIM)
    g.text("font_huge", 10, 38, "3,120", TXT)
    combo_badge(g, 230, 26, 11)
    g.text("font_small", 10, 82, "STEADINESS", DIM)
    # timing meter: early | target | late
    g.fill(10, 96, 300, 14, PANEL)
    g.fill(130, 96, 60, 14, ACC_BG)
    g.vline(160, 92, 22, TXT)
    g.fill(151, 99, 6, 8, ACC)
    g.text("font_small", 10, 114, "early", DIM); g.text("font_small", 310, 114, "late", DIM, align="right")
    g.text("font_small", 0, 114, "right on the beat", ACC, maxw=S1W, align="center")
    stat_box(g, 10, 136, 96, "Right notes", "11 / 11", PERFECT)
    stat_box(g, 112, 136, 96, "Tempo", "80 bpm")
    stat_box(g, 214, 136, 96, "Evenness", "92%")
    g.text("font_small", 10, 176, "Beat 3 of 4", DIM)
    for b in range(4):
        g.fill(90 + b * 22, 176, 16, 10, ACC if b == 2 else LINE)
    soft_keys(g, ["Stop", "Slower", "Faster", "Click", "Restart"])

    h = Gfx(S2W, S2H, BG, "explay-2")
    grand_staff(h, x1=S2W - 6)
    notes = [62, 64, 66, 67, 69, 71, 73, 74, 76, 78, 79, 81]
    for i, n in enumerate(notes):
        c = PERFECT if i < 5 else (AMB if i == 5 else hexc("#6B7584"))
        if i == 3:
            c = GOOD
        staff_note(h, 60 + i * 30, n, c)
    h.vline(60 + 5 * 30, 10, 120, AMB)
    h.text("font_small", 6, 4, "next: B4", AMB)
    save(g, h, "explay")


def note_reading():
    g = Gfx(S1W, S1H, BG, "nr")
    status_bar(g, "Note Reading", "round 2 · treble clef")
    g.text("font_small", 10, 26, "SCORE", DIM)
    g.text("font_huge", 10, 38, "1,560", TXT)
    combo_badge(g, 230, 26, 9)
    g.text("font_small", 10, 82, "TIME", DIM)
    progress(g, 10, 96, 300, 10, 0.62, AMB)
    g.text("font_small", 310, 82, "37 s", AMB, align="right")
    stat_box(g, 10, 120, 96, "Right", "26", PERFECT)
    stat_box(g, 112, 120, 96, "Wrong", "2", MISS)
    stat_box(g, 214, 120, 96, "Fastest", "0.6 s")
    g.text("font_small", 10, 162, "Lives", DIM)
    for i in range(3):
        g.fill(60 + i * 16, 162, 10, 10, REC if i < 2 else LINE)
    g.text("font_small", 10, 182, "Faster answers score more. Notes get harder", DIM, maxw=300)
    g.text("font_small", 10, 196, "as your streak grows: sharps, ledger lines...", DIM, maxw=300)
    soft_keys(g, ["Quit", None, None, "Clef", "Pause"])

    h = Gfx(S2W, S2H, BG, "nr-2")
    grand_staff(h, x1=S2W - 6)
    staff_note(h, 214, 79, TXT, big=True)
    # previous answers fading to the left
    for i, (n, ok) in enumerate([(72, True), (65, True), (76, False), (69, True)]):
        x = 70 + i * 30
        staff_note(h, x, n, hexc("#2E6B47") if ok else hexc("#7A2F33"))
    h.text("font_small", S2W - 6, 4, "What note is this?", AMB, align="right")
    h.text("font_small", 6, 4, "last: A4 · right · 0.8 s", ACC)
    save(g, h, "nr")


def ear():
    g = Gfx(S1W, S1H, BG, "ear")
    status_bar(g, "Ear Training", "Level 4 · intervals")
    g.text("font_small", 10, 26, "SCORE", DIM)
    g.text("font_huge", 10, 38, "860", TXT)
    combo_badge(g, 230, 26, 5)
    g.fill(10, 80, 300, 54, PANEL)
    g.text("font_main", 0, 86, "Listen, then play both notes back", TXT, maxw=S1W, align="center")
    g.text("font_small", 0, 104, "First note is given: C4", DIM, maxw=S1W, align="center")
    g.text("font_large", 0, 116, "C4  +  ?", AMB, maxw=S1W, align="center")
    stat_box(g, 10, 144, 96, "Right", "14", PERFECT)
    stat_box(g, 112, 144, 96, "Tries left", "2")
    stat_box(g, 214, 144, 96, "Next level", "6 to go")
    soft_keys(g, ["Quit", "Replay", None, "Skip", "Hint"])

    h = Gfx(S2W, S2H, BG, "ear-2")
    h.text("font_large", 12, 10, "Listening…", ACC)
    # sound waves
    for i in range(3):
        r = 18 + i * 14
        for a in range(-40, 41, 4):
            ang = math.radians(a)
            h.pixel(int(70 + r * math.cos(ang)), int(78 + r * math.sin(ang)), ACC if i == 0 else hexc("#2E6B47"))
    h.fill(40, 66, 14, 24, ACC); h.fill(54, 60, 10, 36, ACC)
    h.text("font_small", 140, 40, "LEVELS", DIM)
    lv = ["Same or different", "Up or down", "2nds and 3rds", "4ths and 5ths", "All intervals", "Major or minor chord"]
    for i, t in enumerate(lv):
        y = 54 + i * 13
        c = ACC if i < 3 else (AMB if i == 3 else DIM)
        if i < 3:
            h.fill(140, y + 3, 6, 6, ACC)
        elif i == 3:
            h.tri_right(140, y + 1, 9, AMB)
        h.text("font_small", 152, y, t, c)
    pos = keyboard(h, 60, 72, 100, 36, x0=310, kw=14, lit={60: BLUE})
    h.text("font_small", 310, 88, "your answer", DIM)
    save(g, h, "ear")


if __name__ == "__main__":
    home(); menu(); keyrange(0); keyrange(1); play_setup(); playing("wait"); playing("play"); results()
    exercises(); ex_playing(); note_reading(); ear()
    if M.ALL_WARN:
        print("TEXT DOES NOT FIT:")
        for w in M.ALL_WARN:
            print("  " + w)
        sys.exit(1)
    print("learn mockups ok")
