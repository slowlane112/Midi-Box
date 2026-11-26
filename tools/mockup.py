#!/usr/bin/env python3
"""
mockup.py - pixel-exact renders of every MIDI Box screen.

Uses only gfx.py primitives (1:1 with the C renderer), the generated bitmap
fonts and the real .icn icons. Fixed labels that do not fit their box are
reported at the end; the run fails if there are any.

Output: mockups/<screen>_s1.png (320x240) and <screen>_s2.png (428x142), 2x scaled.
"""
import os, random, sys
sys.path.insert(0, os.path.dirname(__file__))
from gfx import Gfx, hexc

OUT = os.path.join(os.path.dirname(__file__), "..", "mockups")
os.makedirs(OUT, exist_ok=True)

# ---- theme (C: ui_theme.h) ----
BG = hexc("#0B0D10")
PANEL = hexc("#161A21")
LINE = hexc("#2A313C")
TXT = hexc("#E8EDF2")
DIM = hexc("#8E99A8")
ACC = hexc("#3DDC84")   # follows the Display Colour setting
ACC_BG = hexc("#13301F")  # selected-row background (accent, darkened)
REC = hexc("#FF5A5F")
AMB = hexc("#FFB547")
WHITE_KEY = hexc("#D9DEE5")
BLACK_KEY = hexc("#11151B")

S1W, S1H, S2W, S2H = 320, 240, 428, 142
STATUS_H = 18
SOFT_Y, SOFT_H = 218, 22
CONTENT_Y = STATUS_H + 1
SOFT_CENTRES = [22, 91, 160, 229, 298]   # x centre of buttons 4..8 (matches current hardware layout)
SOFT_W = 64

ALL_WARN = []


# ---- shared widgets (C: ui_widgets.c) ----
def status_bar(g, title, right="", title_col=ACC):
    g.fill(0, 0, S1W, STATUS_H, PANEL)
    g.hline(0, STATUS_H, S1W, LINE)
    tw = g.text("font_main", 6, 2, title, title_col, maxw=200)
    if right:
        g.text("font_small", 6 + tw + 8, 4, right, DIM, maxw=S1W - (6 + tw + 8) - 6, align="right")


def soft_keys(g, keys, shift=False):
    """keys: 5 entries, each None, a text label, or ('shape', name). Same as C ui_softkeys()."""
    g.fill(0, SOFT_Y, S1W, SOFT_H, ACC if shift else PANEL)
    g.hline(0, SOFT_Y, S1W, LINE)
    col = BG if shift else TXT
    for i, k in enumerate(keys):
        if not k:
            continue
        cx = SOFT_CENTRES[i]
        x0 = max(0, cx - SOFT_W // 2)
        x0 = min(x0, S1W - SOFT_W)
        if isinstance(k, tuple):
            shape(g, k[1], cx, SOFT_Y + 6, REC if k[1] == "rec" else col)
        else:
            c = REC if k.startswith("Rec") and not shift else col
            g.text("font_main", x0, SOFT_Y + 4, k, c, maxw=SOFT_W, align="center")


def shape(g, name, cx, y, c):
    """Transport glyphs, 11px tall, centred on cx."""
    h = 11
    if name == "play":
        g.tri_right(cx - 3, y, h, c)
    elif name == "pause":
        g.fill(cx - 5, y, 3, h, c); g.fill(cx + 2, y, 3, h, c)
    elif name == "playpause":
        g.tri_right(cx - 9, y, h, c); g.fill(cx + 2, y, 3, h, c); g.fill(cx + 7, y, 3, h, c)
    elif name == "stop":
        g.fill(cx - 5, y, 10, h - 1, c)
    elif name == "ff":
        g.tri_right(cx - 7, y, h, c); g.tri_right(cx, y, h, c)
    elif name == "rew":
        g.tri_left(cx - 6, y, h, c); g.tri_left(cx + 1, y, h, c)
    elif name == "rec":
        g.circle(cx, y + 5, 5, c)


def row(g, y, h, selected):
    if selected:
        g.fill(0, y, S1W, h, ACC_BG)
        g.fill(0, y, 3, h, ACC)


def stat_box(g, x, y, w, label, value, vcol=TXT):
    g.fill(x, y, w, 30, PANEL)
    g.rect(x, y, w, 30, LINE)
    g.text("font_small", x + 5, y + 2, label, DIM, maxw=w - 8)
    g.text("font_main", x + 5, y + 14, value, vcol, maxw=w - 8)


def progress(g, x, y, w, h, frac, c=ACC):
    g.fill(x, y, w, h, LINE)
    g.fill(x, y, int(w * frac), h, c)


def header2(g, left, right="", col=ACC):
    g.text("font_small", 6, 3, left, col, maxw=260)
    if right:
        g.text("font_small", S2W - 6, 3, right, DIM, align="right")


def piano2(g, lit, y=22, h=114):
    """Screen-2 piano visualiser: 52 white keys x 8px = 416px, 88-key range A0..C8."""
    x0 = 6
    whites = [n for n in range(21, 109) if n % 12 in (0, 2, 4, 5, 7, 9, 11)]
    pos = {}
    for i, n in enumerate(whites):
        pos[n] = x0 + i * 8
        g.fill(pos[n], y, 7, h, ACC if n in lit else WHITE_KEY)
    for n in range(21, 109):
        if n % 12 in (1, 3, 6, 8, 10):
            x = pos[n - 1] + 5
            g.fill(x, y, 5, h * 6 // 10, ACC if n in lit else BLACK_KEY)


def spectrum2(g, seed=9):
    import math
    random.seed(seed)
    for i in range(52):
        hh = int(8 + 100 * abs(math.sin(i / 7.0)) * random.uniform(0.45, 1.0))
        g.fill(6 + i * 8, 136 - hh, 6, hh, ACC)


def save(g1, g2, name):
    g1.save(os.path.join(OUT, name + "_s1.png"))
    g2.save(os.path.join(OUT, name + "_s2.png"))
    ALL_WARN.extend(g1.warnings + g2.warnings)


# =========================== SCREENS ===========================

def home():
    g = Gfx(S1W, S1H, BG, "home")
    status_bar(g, "MIDI Box", "BT · DIN · Vol 14")
    apps = [("Player", "Play songs from the SD card", "icon_boombox_64x64"),
            ("Studio", "Record and edit your own songs", "icon_studio_64x64"),
            ("Live In", "Play DIN or Bluetooth MIDI", "icon_mididin_64x64"),
            ("Files", "Copy, move, rename, delete", "icon_folder_64x64"),
            ("Transfer", "Send files from your PC", "icon_file_transfer_64x64"),
            ("Settings", "Display, MIDI and sound", "icon_gear_64x64")]
    sel = 1
    for i, (n, _, _) in enumerate(apps):
        y = CONTENT_Y + 4 + i * 32
        if i == sel:
            g.fill(0, y, 140, 30, ACC_BG); g.fill(0, y, 3, 30, ACC)
        g.text("font_large", 12, y + 6, n, ACC if i == sel else TXT, maxw=124)
    g.vline(146, CONTENT_Y + 4, 190, LINE)
    n, desc, ic = apps[sel]
    g.icon(ic, 146 + (174 - 64) // 2, 44, ACC)
    g.text("font_large", 150, 122, n, TXT, maxw=166, align="center")
    g.text("font_small", 150, 146, "Record and edit", DIM, maxw=166, align="center")
    g.text("font_small", 150, 160, "your own songs", DIM, maxw=166, align="center")
    soft_keys(g, ["Resume", None, None, None, "Open"])

    h = Gfx(S2W, S2H, BG, "home-2")
    h.text("font_huge", 10, 8, "MIDI Box", ACC)
    h.text("font_small", 12, 44, "v2.0 · SAM2695", DIM)
    h.text("font_small", 12, 72, "RESUME", DIM)
    h.text("font_main", 12, 86, "canon_in_d.mid", TXT, maxw=190, strict=False)
    progress(h, 12, 106, 190, 4, 0.36)
    h.text("font_small", 12, 114, "1:24 / 3:51", DIM)
    h.vline(222, 10, 122, LINE)
    rows = [("SD card", "14.2 GB free", TXT), ("Bluetooth", "A-49 connected", ACC), ("DIN In 1", "idle", TXT),
            ("MIDI thru", "on", TXT), ("Sound bank", "General MIDI", TXT), ("Last project", "my_song_03", TXT)]
    for i, (k, v, c) in enumerate(rows):
        y = 10 + i * 20
        h.text("font_small", 232, y, k, DIM, maxw=86)
        h.text("font_small", S2W - 8, y, v, c, align="right")
    save(g, h, "home")


def player(shift=False, name="player"):
    g = Gfx(S1W, S1H, BG, name)
    status_bar(g, "Player", "GM · Repeat · Vol 14")
    g.text("font_small", 6, 22, "4 of 27 · /songs/classical", DIM, maxw=308, strict=False)
    g.text("font_large", 6, 36, "canon_in_d.mid", TXT, maxw=308, strict=False)
    g.text("font_small", 6, 57, "Pachelbel – Canon in D · 9 tracks", DIM, maxw=308, strict=False)
    progress(g, 6, 76, 308, 6, 0.36)
    g.text("font_small", 6, 85, "1:24", TXT)
    g.text("font_small", 314, 85, "-2:27", DIM, align="right")
    for i, (k, v) in enumerate([("Tempo", "120 +0%"), ("Transpose", "0"), ("Sound bank", "GM")]):
        stat_box(g, 6 + i * 104, 102, 100, k, v)
    levels = [40, 62, 20, 0, 55, 30, 0, 0, 12, 58, 0, 0, 0, 0, 0, 0]
    for c in range(16):
        x = 6 + c * 19
        g.fill(x, 140, 16, 50, PANEL)
        lv = levels[c] * 50 // 64
        g.fill(x, 190 - lv, 16, lv, ACC)
        g.text("font_small", x, 194, str(c + 1), DIM, maxw=16, align="center")
    if shift:
        soft_keys(g, ["Mode", "Tempo", "Transp", "Bank", "Tracks"], shift=True)
    else:
        soft_keys(g, ["Back", ("s", "rew"), ("s", "playpause"), ("s", "stop"), ("s", "ff")])
    h = Gfx(S2W, S2H, BG, name + "-2")
    header2(h, "Piano", "1/7")
    piano2(h, {43, 48, 55, 60, 64, 67, 72, 79})
    save(g, h, name)


def player_tracks():
    g = Gfx(S1W, S1H, BG, "tracks")
    status_bar(g, "Tracks", "6 of 9 playing")
    trk = [("1", "Melody", 1, "on", 44, True), ("2", "Harmony", 2, "on", 30, True), ("3", "Cello", 3, "on", 52, True),
           ("4", "Strings", 4, "on", 38, True), ("5", "Continuo", 5, "mute", 0, True), ("6", "Bass", 6, "on", 26, False),
           ("7", "Drums", 10, "solo", 58, True), ("8", "Timpani", 11, "on", 8, True)]
    for i, (n, nm, ch, st, lv, vis) in enumerate(trk):
        y = CONTENT_Y + i * 22
        sel = i == 3
        row(g, y, 22, sel)
        muted = st == "mute"
        tc = DIM if muted else (ACC if sel else TXT)
        g.text("font_small", 8, y + 6, n, DIM)
        g.text("font_main", 22, y + 4, nm, tc, maxw=96, strict=False)
        g.text("font_small", 122, y + 6, "ch %d" % ch, DIM)
        g.fill(160, y + 8, 80, 6, LINE)
        g.fill(160, y + 8, 0 if muted else lv * 80 // 64, 6, ACC)
        if st == "solo":
            g.text("font_small", 245, y + 6, "SOLO", AMB)
        if muted:
            g.text("font_small", 245, y + 6, "MUTE", REC)
        g.icon("icon_sound_off_16x16" if muted else "icon_sound_16x16", 276, y + 3, REC if muted else tc)
        g.icon("icon_eye_16x16" if vis else "icon_eye_off_16x16", 298, y + 3, tc if vis else DIM)
    g.text("font_small", 8, 196, "ch 4 · String Ensemble 1 · 4/9", DIM, maxw=300)
    soft_keys(g, ["Back", "Sound", "Solo", "Visual", "Next"])
    h = Gfx(S2W, S2H, BG, "tracks-2")
    header2(h, "Piano · 7 of 9 tracks shown", "1/7")
    piano2(h, {55, 62, 67, 74, 43, 50})
    save(g, h, "player_tracks")


def studio():
    g = Gfx(S1W, S1H, BG, "studio")
    status_bar(g, "Studio", "my_song_03 · 120 bpm · 4/4")
    HEADW = 70
    g.fill(0, CONTENT_Y, S1W, 12, PANEL)
    BARW = 25
    for b in range(10):
        g.text("font_small", HEADW + b * BARW + 2, CONTENT_Y, str(b + 5), DIM)
    tracks = [("Piano", 1), ("Bass", 2), ("Drums", 10), ("Strings", 4), ("Lead", 5), ("Pad", 6), ("FX", 7), ("Choir", 8)]
    clips = {0: [(0, 4), (5, 10)], 1: [(0, 10)], 2: [(0, 2), (2, 6), (6, 10)], 3: [(3, 10)], 4: [(4, 7)], 6: [(8, 10)]}
    random.seed(4)
    top = CONTENT_Y + 13
    for i, (n, ch) in enumerate(tracks):
        y = top + i * 23
        sel = i == 3
        row(g, y, 22, sel)
        g.hline(0, y + 22, S1W, PANEL)
        g.text("font_main", 8, y + 1, n, ACC if sel else TXT, maxw=58, strict=False)
        g.text("font_small", 8, y + 12, "ch %d" % ch + (" · rec" if sel else ""), REC if sel else DIM, maxw=60)
        for a, b in clips.get(i, []):
            x = HEADW + a * BARW
            w = (b - a) * BARW - 2
            g.fill(x, y + 3, w, 16, hexc("#1F4A33") if not sel else hexc("#2B6646"))
            g.rect(x, y + 3, w, 16, ACC)
            for k in range(w // 5):
                g.fill(x + 2 + k * 5, y + 5 + random.randint(0, 11), 3, 1, TXT)
    g.vline(HEADW + 4 * BARW, CONTENT_Y, SOFT_Y - CONTENT_Y, AMB)
    g.vline(HEADW - 2, CONTENT_Y, SOFT_Y - CONTENT_Y, LINE)
    soft_keys(g, ["Back", "Rec", ("s", "play"), "Edit", "+Track"])
    h = Gfx(S2W, S2H, BG, "studio-2")
    header2(h, "Overview · 16 tracks · 32 bars", "2:14")
    random.seed(7)
    for t in range(16):
        y = 18 + t * 7
        h.text("font_small", 4, y - 3, "", DIM)
        pos = 0
        if t < 8:
            while pos < 32:
                ln = random.randint(2, 8); gap = random.randint(0, 3)
                ln = min(ln, 32 - pos)
                h.fill(14 + pos * 13, y, ln * 13 - 1, 5, ACC if t == 3 else hexc("#4B5563"))
                pos += ln + gap
        else:
            h.hline(14, y + 2, 416, PANEL)
    h.rect(14 + 4 * 13, 16, 10 * 13, 16 * 7 + 3, AMB)
    h.vline(14 + 8 * 13, 16, 115, AMB)
    save(g, h, "studio")


def record():
    g = Gfx(S1W, S1H, BG, "record")
    status_bar(g, "Recording", "track 4 · Strings · DIN 1", title_col=REC)
    g.circle(14, 40, 6, REC)
    g.text("font_large", 26, 32, "REC", REC)
    g.text("font_small", 314, 34, "loop bars 9–16 · take 2", DIM, align="right")
    g.text("font_huge", 6, 54, "12.3", TXT)
    g.text("font_small", 6, 88, "bar . beat", DIM)
    for b in range(4):
        x = 170 + b * 36
        on = b == 2
        g.fill(x, 56, 32, 32, ACC if on else PANEL)
        g.rect(x, 56, 32, 32, ACC if b == 0 else LINE)
        g.text("font_main", x, 65, str(b + 1), BG if on else DIM, maxw=32, align="center")
    for i, (k, v) in enumerate([("Notes", "142"), ("Click", "On"), ("Count-in", "1 bar"), ("Mode", "Overdub")]):
        stat_box(g, 6 + i * 78, 106, 74, k, v)
    g.text("font_small", 6, 144, "Input", DIM)
    progress(g, 6, 158, 308, 6, 0.62)
    g.text("font_small", 6, 172, "Loop 9–16", DIM)
    g.rect(6, 186, 308, 12, LINE)
    g.fill(7, 187, 259, 10, hexc("#3A1D20"))
    g.fill(266, 184, 2, 16, REC)
    soft_keys(g, [("s", "stop"), "Click", "Overdub", "Undo", "Keep"])
    h = Gfx(S2W, S2H, BG, "record-2")
    header2(h, "Live notes · track 4", "bars 9–16", col=REC)
    for i in range(9):
        h.vline(10 + i * 51, 18, 120, LINE)
    random.seed(2)
    x = 12
    while x < 370:
        p = random.randint(0, 11)
        w = random.choice([6, 8, 14, 18])
        h.fill(x, 22 + p * 9, w, 7, ACC)
        x += random.choice([6, 9, 12])
    h.fill(372, 18, 2, 120, REC)
    save(g, h, "record")


def edit():
    g = Gfx(S1W, S1H, BG, "edit")
    status_bar(g, "Edit", "2 Bass · bars 5–8 selected")
    KEYW = 14
    for i in range(25):
        y = CONTENT_Y + i * 8
        blk = (60 - i) % 12 in (1, 3, 6, 8, 10)
        g.fill(0, y, KEYW, 7, BLACK_KEY if blk else WHITE_KEY)
        g.fill(KEYW + 1, y, S1W - KEYW - 1, 8, hexc("#0E1115") if blk else BG)
    for b in range(5):
        g.vline(KEYW + 1 + b * 76, CONTENT_Y, 199, LINE)
    random.seed(3)
    notes = []
    for k in range(30):
        x = KEYW + 3 + k * 10
        notes.append((x, random.randint(3, 22), random.choice([8, 9, 17])))
    sel_w = 228
    g.rect(KEYW + 1, CONTENT_Y, sel_w, 199, ACC)
    for x, p, w in notes:
        g.fill(x, CONTENT_Y + p * 8 + 1, min(w, S1W - x), 6, ACC if x < KEYW + sel_w else hexc("#6B7280"))
    # quantise dialog (C: ui_dialog)
    dx, dy, dw, dh = 64, 40, 192, 132
    g.fill(dx, dy, dw, dh, PANEL)
    g.rect(dx, dy, dw, dh, ACC)
    g.text("font_main", dx + 8, dy + 6, "Quantise selection", ACC, maxw=dw - 16)
    opts = [("Grid", "1/16"), ("Strength", "75%"), ("Swing", "0%"), ("Note ends", "Keep")]
    for i, (k, v) in enumerate(opts):
        y = dy + 26 + i * 20
        if i == 0:
            g.fill(dx + 4, y - 2, dw - 8, 18, ACC_BG)
        g.text("font_main", dx + 10, y, k, TXT, maxw=90)
        g.text("font_main", dx + dw - 10, y, ("< %s >" % v) if i == 0 else v, ACC if i == 0 else TXT, align="right")
    g.text("font_small", dx + 10, dy + dh - 20, "38 notes move, at most 41 ms", DIM, maxw=dw - 20)
    soft_keys(g, ["Cancel", "-", "+", "Next", "Apply"])
    h = Gfx(S2W, S2H, BG, "edit-2")
    header2(h, "Track 2 · Bass · 32 bars", "Clipboard: 4 bars")
    for b in range(0, 32, 4):
        h.text("font_small", 6 + b * 13, 18, str(b + 1), DIM)
        h.vline(6 + b * 13, 30, 106, LINE)
    random.seed(5)
    for k in range(104):
        x = 6 + k * 4
        h.fill(x, 34 + random.randint(0, 96), 3, 3, ACC if 58 <= x < 110 else hexc("#6B7280"))
    h.rect(58, 31, 52, 106, ACC)
    save(g, h, "edit")


def files():
    g = Gfx(S1W, S1H, BG, "files")
    status_bar(g, "Files", "/songs · 14.2 GB free")
    rows = [("d", "..", ""), ("d", "classical", ""), ("d", "games", ""), ("f", "canon_in_d.mid", "24 KB"),
            ("f", "doom_e1m1.mid", "41 KB"), ("f", "my_song_03.mid", "9 KB"), ("f", "fur_elise.mid", "12 KB"),
            ("f", "a_very_long_file_name_that_will_not_fit.mid", "18 KB"), ("f", "zelda.mid", "6 KB")]
    for i, (t, n, sz) in enumerate(rows):
        y = CONTENT_Y + i * 22
        sel = i == 3
        row(g, y, 22, sel)
        g.icon("icon_folder_16x16" if t == "d" else "icon_file_16x16", 8, y + 3, AMB if t == "d" else DIM)
        szw = g.width("font_small", sz) if sz else 0
        g.text("font_main", 30, y + 4, n, ACC if sel else TXT, maxw=314 - 30 - szw - 8, strict=False)
        if sz:
            g.text("font_small", 314, y + 6, sz, DIM, align="right")
    # action menu
    mx, my, mw = 178, 52, 136
    items = ["Play", "Copy", "Move", "Rename", "Delete", "New folder"]
    mh = 8 + len(items) * 20
    g.fill(mx, my, mw, mh, PANEL)
    g.rect(mx, my, mw, mh, ACC)
    for i, it in enumerate(items):
        y = my + 4 + i * 20
        if i == 2:
            g.fill(mx + 3, y, mw - 6, 19, ACC)
        c = BG if i == 2 else (REC if it == "Delete" else TXT)
        g.text("font_main", mx + 10, y + 3, it, c, maxw=mw - 20)
    soft_keys(g, ["Close", None, None, None, "Choose"])
    h = Gfx(S2W, S2H, BG, "files-2")
    header2(h, "Selected file", "", col=DIM)
    h.text("font_large", 6, 18, "canon_in_d.mid", TXT, maxw=416, strict=False)
    for i, (k, v) in enumerate([("Type", "MIDI 1"), ("Tracks", "9"), ("Length", "3:51"), ("Size", "24 KB")]):
        x = 6 + i * 105
        h.fill(x, 44, 100, 30, PANEL); h.rect(x, 44, 100, 30, LINE)
        h.text("font_small", x + 5, 46, k, DIM, maxw=90)
        h.text("font_main", x + 5, 58, v, TXT, maxw=90)
    h.text("font_small", 6, 84, "Move to:", DIM)
    h.text("font_main", 60, 82, "pick a folder, then press Paste", AMB, maxw=360)
    progress(h, 6, 112, 416, 5, 0.11)
    h.text("font_small", 6, 122, "SD card · 1.7 GB used of 15.9 GB", DIM)
    save(g, h, "files")


def live_in():
    g = Gfx(S1W, S1H, BG, "livein")
    status_bar(g, "Live In", "Thru on · Vol 14")
    srcs = [("DIN 1 (back)", True, "receiving"), ("DIN 2 (front)", False, "off"), ("Bluetooth", True, "Roland A-49")]
    for i, (n, on, st) in enumerate(srcs):
        y = CONTENT_Y + 4 + i * 26
        sel = i == 2
        g.fill(6, y, 308, 23, ACC_BG if sel else PANEL)
        g.rect(6, y, 308, 23, ACC if sel else LINE)
        g.circle(18, y + 11, 4, ACC if on else LINE)
        g.text("font_main", 30, y + 5, n, TXT, maxw=140)
        g.text("font_main", 306, y + 5, st, ACC if on else DIM, align="right")
    stat_box(g, 6, 106, 150, "Sound bank", "General MIDI")
    stat_box(g, 162, 106, 152, "Program", "001 Grand Piano")
    g.text("font_small", 6, 144, "Channel activity", DIM)
    for c in range(16):
        on = c in (0, 1, 9)
        x = 6 + c * 19
        g.fill(x, 158, 16, 16, ACC if on else PANEL)
        g.rect(x, 158, 16, 16, LINE)
        g.text("font_small", x, 160, str(c + 1), BG if on else DIM, maxw=16, align="center")
    g.text("font_small", 6, 182, "Last: Note On C4 vel 96 · ch 1 · BT", DIM, maxw=308)
    soft_keys(g, ["Back", "On/Off", "Bank", "Program", "Rec"])
    h = Gfx(S2W, S2H, BG, "livein-2")
    header2(h, "Spectrum", "3/7")
    spectrum2(h)
    save(g, h, "live_in")


if __name__ == "__main__":
    home(); player(); player(shift=True, name="player_shift"); player_tracks(); studio(); record(); edit(); files(); live_in()
    if ALL_WARN:
        print("TEXT THAT DOES NOT FIT:")
        for w in ALL_WARN:
            print("  " + w)
        sys.exit(2)
    print("all screens rendered, every fixed label fits")
