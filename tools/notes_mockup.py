#!/usr/bin/env python3
"""notes_mockup.py - proposed Studio > Edit > Notes screen (edit single notes, add notes by buttons)."""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from gfx import Gfx, hexc
import mockup as M
from mockup import (BG, PANEL, LINE, TXT, DIM, ACC, ACC_BG, AMB, WHITE_KEY, BLACK_KEY, S1W, S1H, S2W, S2H,
                    CONTENT_Y, status_bar, soft_keys, header2)
import learn_mockup as L

GREY = hexc("#6B7280")
OUT = M.OUT
KEYW, ROWS, ROWPX = 14, 25, 8
PPQ = 4            # sixteenths per beat in this mockup
BAR = 12           # 3/4: 12 sixteenths
LOW = 57           # A3 at the bottom row

# Happy Birthday in C, 3/4 (start, length in 16ths, note)
HB = [(0, 3, 67), (3, 1, 67), (4, 4, 69), (8, 4, 67),
      (12, 4, 72), (16, 8, 71),
      (24, 3, 67), (27, 1, 67), (28, 4, 69), (32, 4, 67),
      (36, 4, 74), (40, 8, 72)]
NAMES = "C C# D D# E F F# G G# A A# B".split()


def nn(n):
    return "%s%d" % (NAMES[n % 12], n // 12 - 1)


def roll(g, notes, view_bar, nbars, cursor_t, cursor_n, cursor_len, picked):
    px_w = S1W - KEYW - 1
    span = nbars * BAR
    for r in range(ROWS):
        note = LOW + ROWS - 1 - r
        y = CONTENT_Y + r * ROWPX
        blk = note % 12 in (1, 3, 6, 8, 10)
        g.fill(0, y, KEYW, ROWPX - 1, BLACK_KEY if blk else WHITE_KEY)
        if note % 12 == 0:
            g.text("font_small", 3, y - 3, str(note // 12 - 1), BG)
        g.fill(KEYW + 1, y, px_w, ROWPX, hexc("#0E1115") if blk else BG)
    x_of = lambda t: KEYW + 1 + (t - view_bar * BAR) * px_w // span
    for t in range(view_bar * BAR, (view_bar + nbars) * BAR + 1):
        x = x_of(t)
        if t % BAR == 0:
            g.vline(x, CONTENT_Y, ROWS * ROWPX, LINE)
            if t < (view_bar + nbars) * BAR:
                g.text("font_small", x + 2, CONTENT_Y + ROWS * ROWPX - 12, str(t // BAR + 1), DIM)
        elif t % PPQ == 0:
            for y in range(CONTENT_Y, CONTENT_Y + ROWS * ROWPX, 3):
                g.pixel(x, y, LINE)
    # cursor column
    cx = x_of(cursor_t)
    for i, (t, ln, n) in enumerate(notes):
        r = LOW + ROWS - 1 - n
        if t + ln <= view_bar * BAR or t >= (view_bar + nbars) * BAR:
            continue
        x0, x1 = x_of(t), x_of(t + ln)
        c = AMB if i == picked else GREY
        g.fill(x0, CONTENT_Y + r * ROWPX + 1, max(x1 - x0 - 1, 2), ROWPX - 2, c)
    g.vline(cx, CONTENT_Y, ROWS * ROWPX, AMB)
    if picked is None:
        r = LOW + ROWS - 1 - cursor_n
        g.rect(cx, CONTENT_Y + r * ROWPX, x_of(cursor_t + cursor_len) - cx, ROWPX, AMB)
        # highlight the row's key
        g.fill(0, CONTENT_Y + r * ROWPX, KEYW, ROWPX - 1, AMB)
    else:
        r = LOW + ROWS - 1 - cursor_n
        g.fill(0, CONTENT_Y + r * ROWPX, KEYW, ROWPX - 1, AMB)


def staff2(h, notes, cursor_t, picked, ghost=None, title=""):
    """notes in their own columns (like printed music), bar lines between bars"""
    header2(h, title, "3/4 " + chr(0x80) + " bars 1" + chr(0x83) + "3")
    L.grand_staff(h, 4, S2W - 4)
    cols = []          # (kind, t, i)
    last_bar = 0
    allv = [(t, i) for i, (t, ln, n) in enumerate(notes)] + ([(ghost[0], -1)] if ghost else [])
    for t, i in sorted(allv):
        if t // BAR != last_bar:
            cols.append(("bar", t, None))
            last_bar = t // BAR
        cols.append(("note", t, i))
    x = 60
    for kind, t, i in cols:
        if kind == "bar":
            h.vline(x - 6, L.y_of(38), L.y_of(18) - L.y_of(38) + 1, LINE)
            x += 8
            continue
        if i == -1:
            L.staff_note(h, x, ghost[1], AMB)
            h.text("font_small", x - 3, S2H - 12, "?", AMB)
            h.vline(x - 10, 16, S2H - 30, AMB)
        else:
            tt, ln, n = notes[i]
            c = AMB if i == picked else TXT
            L.staff_note(h, x, n, c)
            if ln >= 8:   # half note: hollow
                h.fill(x - 2, L.y_of(L.dia(n)) - 1, 5, 3, BG)
            h.text("font_small", x - 3, S2H - 12, nn(n)[:-1], AMB if i == picked else DIM)
            if i == picked:
                h.vline(x - 10, 16, S2H - 30, AMB)
        x += 30


def save(g, h, name):
    g.save(os.path.join(OUT, name + "_s1.png"))
    h.save(os.path.join(OUT, name + "_s2.png"))


def edit_note():
    g = Gfx(S1W, S1H, BG, "notes")
    picked = 5   # B4 in bar 2
    t, ln, n = HB[picked]
    status_bar(g, "Notes", "B4 " + chr(0x80) + " 1/2 " + chr(0x80) + " bar 2 beat 2")
    roll(g, HB, 0, 3, t, n, ln, picked)
    soft_keys(g, ["Back", "Shorter", "Longer", "Delete", "Next"])
    h = Gfx(S2W, S2H, BG, "notes-2")
    staff2(h, HB[:9], t, picked, title="Track 1 " + chr(0x80) + " Melody")
    save(g, h, "notes_edit")


def edit_shift():
    g = Gfx(S1W, S1H, BG, "notes")
    picked = 5
    t, ln, n = HB[picked]
    status_bar(g, "Notes", "B4 " + chr(0x80) + " 1/2 " + chr(0x80) + " bar 2 beat 2")
    roll(g, HB, 0, 3, t, n, ln, picked)
    soft_keys(g, ["Undo", "Earlier", "Later", "Velocity", "Chord"], shift=True)
    h = Gfx(S2W, S2H, BG, "notes-2")
    staff2(h, HB[:9], t, picked, title="Track 1 " + chr(0x80) + " Melody")
    save(g, h, "notes_shift")


def add_note():
    g = Gfx(S1W, S1H, BG, "notes")
    entered = HB[:9]          # up to bar 3 beat 2
    cur_t, cur_n, cur_len = 32, 67, 4
    status_bar(g, "Notes", "add G4 " + chr(0x80) + " 1/4 " + chr(0x80) + " bar 3 beat 3")
    roll(g, entered, 1, 3, cur_t, cur_n, cur_len, None)
    soft_keys(g, ["Back", "Shorter", "Longer", "Rest", "Add"])
    h = Gfx(S2W, S2H, BG, "notes-2")
    staff2(h, entered, cur_t, None, ghost=(cur_t, cur_n), title="Track 1 " + chr(0x80) + " Melody")
    save(g, h, "notes_add")


if __name__ == "__main__":
    edit_note(); edit_shift(); add_note()
    print("ok")
