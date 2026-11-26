#!/usr/bin/env python3
"""
learn_songgen.py - learn_songs.txt -> main/learn/ln_builtin.c

A song can also come from a MIDI file (import: file | RH track | LH track | full/easy):
"full" keeps every note, "easy" keeps the right-hand melody (no note under a higher one)
and the lowest left-hand note of each beat.

Checks every bar of every voice is exactly one bar long and that all voices of a song
have the same number of bars; stops with the song, voice and bar number otherwise.
Times are in ticks at 48 per quarter note (fits 16ths, triplet 8ths and triplet 16ths).
"""
import os
import re
import sys
from fractions import Fraction

PPQ = 48
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "learn_songs.txt")
DST = os.path.join(HERE, "..", "main", "learn", "ln_builtin.c")
DUR = {"w": Fraction(4), "h": Fraction(2), "q": Fraction(1), "e": Fraction(1, 2), "s": Fraction(1, 4),
       "t": Fraction(1, 3), "x": Fraction(1, 6)}
NOTE_RE = re.compile(r"^([A-G])([#b]?)(-?\d)$")
VEL = {"rh": 84, "lh": 64}


class Bad(Exception):
    pass


def midi(n):
    m = NOTE_RE.match(n)
    if not m:
        raise Bad("bad note '%s'" % n)
    pc = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}[m.group(1)]
    pc += {"#": 1, "b": -1, "": 0}[m.group(2)]
    v = (int(m.group(3)) + 1) * 12 + pc
    if not 21 <= v <= 108:
        raise Bad("note %s outside the piano" % n)
    return v


def voice(tokens, bar_len, where):
    """-> (notes [(start, len, pitch)], bar count)"""
    notes, t, bar_start, bars, dur = [], Fraction(0), Fraction(0), 0, Fraction(1)
    for tok in tokens:
        if tok == "|":
            if t - bar_start != bar_len:
                raise Bad("%s bar %d is %s beats, should be %s" % (where, bars + 1, t - bar_start, bar_len))
            bars += 1
            bar_start = t
            continue
        if ":" in tok:
            body, d = tok.split(":")
            dotted = d.endswith(".")
            d = d.rstrip(".")
            if d not in DUR:
                raise Bad("%s: bad length in '%s'" % (where, tok))
            dur = DUR[d] * (Fraction(3, 2) if dotted else 1)
        else:
            body = tok
        if body != "r":
            for n in body.split("+"):
                notes.append((t, dur, midi(n)))
        t += dur
    if t != bar_start:
        raise Bad("%s: last bar is not closed with |" % where)
    return notes, bars


def read_mid(path):
    """-> (ppq, [[(start, len, pitch)] per track]) from a standard MIDI file."""
    import struct
    d = open(path, "rb").read()
    if d[:4] != b"MThd":
        raise Bad("%s: not a MIDI file" % path)
    _, ntrk, ppq = struct.unpack(">HHH", d[8:14])
    if ppq & 0x8000:
        raise Bad("%s: SMPTE timing not supported" % path)
    i, tracks = 8 + struct.unpack(">I", d[4:8])[0], []
    for _ in range(ntrk):
        if d[i:i + 4] != b"MTrk":
            raise Bad("%s: bad track" % path)
        end = i + 8 + struct.unpack(">I", d[i + 4:i + 8])[0]
        j, t, rs, on, notes = i + 8, 0, 0, {}, []
        def vlq(j):
            v = 0
            while True:
                b = d[j]; j += 1; v = (v << 7) | (b & 0x7F)
                if b < 0x80:
                    return v, j
        while j < end:
            dt, j = vlq(j)
            t += dt
            st = d[j]
            if st == 0xFF:
                ln, k = vlq(j + 2)
                j = k + ln
                continue
            if st in (0xF0, 0xF7):
                ln, k = vlq(j + 1)
                j = k + ln
                continue
            if st & 0x80:
                rs, j = st, j + 1
            c = rs & 0xF0
            a, b = d[j], d[j + 1] if c not in (0xC0, 0xD0) else 0
            j += 1 if c in (0xC0, 0xD0) else 2
            key = (rs & 15, a)
            if c == 0x90 and b:
                if key in on:                       # re-strike: stack them
                    pass
                on.setdefault(key, []).append(t)
            elif c in (0x80, 0x90) and on.get(key):
                t0 = on[key].pop(0)
                notes.append((t0, t - t0, a))
        tracks.append(sorted(notes))
        i = end
    return ppq, tracks


def easy_rh(notes):
    """Melody: keep a note only if nothing higher is sounding when it starts."""
    out = []
    for t, d, p in notes:
        if not any(t0 <= t < t0 + d0 and p0 > p for t0, d0, p0 in notes):
            out.append((t, d, p))
    return out


def easy_lh(notes, beat):
    """Bass: the lowest note starting in each beat, held until the next one."""
    best = {}
    for t, d, p in notes:
        b = t // beat
        if b not in best or p < best[b][2] or (p == best[b][2] and t < best[b][0]):
            best[b] = (t, d, p)
    pick = [best[b] for b in sorted(best)]
    out = []
    for k, (t, d, p) in enumerate(pick):
        nxt = pick[k + 1][0] if k + 1 < len(pick) else t + d
        out.append((t, max(min(d, nxt - t), 1), p))
    return out


def import_mid(song, args, bar_len):
    """import: file | right-hand track | left-hand track | full or easy"""
    path = os.path.join(HERE, args[0])
    rt, lt, kind = int(args[1]), int(args[2]), args[3]
    ppq, tracks = read_mid(path)
    q = lambda v: int(round(Fraction(v * PPQ, ppq)))      # to the 48-per-quarter grid
    hands = []
    for tr in (rt, lt):
        ns, seen = [], set()
        for t, d, p in tracks[tr]:
            n = (q(t), max(q(d), 1), p)
            if (n[0], p) not in seen:                         # unison in two voices: once
                seen.add((n[0], p))
                ns.append(n)
        hands.append(sorted(ns))
    if kind == "easy":
        beat = int(bar_len * PPQ) // song["num"] * (3 if song["den"] == 8 and song["num"] % 3 == 0 else 1)
        hands = [easy_rh(hands[0]), easy_lh(hands[1], beat)]
    elif kind != "full":
        raise Bad("%s: import kind must be full or easy" % song["id"])
    for p in (n[2] for h in hands for n in h):
        if not 21 <= p <= 108:
            raise Bad("%s: note %d outside the piano" % (song["id"], p))
    song["imported"] = hands


def bach(chords):
    """Prelude in C figure: LH n1+n2 halves; RH (rest 8th) n3 n4 n5 n3 n4 n5 in 16ths, twice a bar."""
    rh, lh = [], []
    for c in chords:
        n1, n2, n3, n4, n5 = c
        lh.append("%s+%s:h %s+%s:h |" % (n1, n2, n1, n2))
        half = "r:e %s:s %s %s %s %s %s" % (n3, n4, n5, n3, n4, n5)
        rh.append(half + " " + half + " |")
    return " ".join(rh), " ".join(lh)


def parse(text):
    songs, cur = [], None
    for ln, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("song:"):
            f = [x.strip() for x in line[5:].split("|")]
            num, den = (int(x) for x in f[5].split("/"))
            cur = {"id": f[0], "title": f[1], "composer": f[2], "level": int(f[3]), "bpm": int(f[4]),
                   "num": num, "den": den, "voices": {}}
            continue
        if line == "end":
            songs.append(cur)
            cur = None
            continue
        key, _, rest = line.partition(":")
        key = key.strip()
        if key == "pattern bach":
            chords = [c.split() for c in rest.split("|") if c.strip()]
            r, l = bach(chords)
            cur["voices"].setdefault("rh", []).extend(r.split())
            cur["voices"].setdefault("lh", []).extend(l.split())
        elif key == "import":
            cur["import"] = [x.strip() for x in rest.split("|")]
        elif key == "credit":
            cur["credit"] = rest.strip()
        elif key in ("rh", "rh2", "lh", "lh2"):
            cur["voices"].setdefault(key, []).extend(rest.replace("|", " | ").split())
        else:
            raise Bad("line %d: unknown '%s'" % (ln, key))
    return songs


def build(song):
    bar_len = Fraction(song["num"] * 4, song["den"])
    out, nbars = [], None
    if "import" in song:
        import_mid(song, song["import"], bar_len)
        bt = int(bar_len * PPQ)
        for hand, ns in enumerate(song["imported"]):
            for t, d, p in ns:
                out.append((t, min(d, 65535), p, hand, VEL["rh" if hand == 0 else "lh"]))
        out.sort(key=lambda x: (x[0], -x[2]))
        end = max(t + d for t, d, p, h, v in out)
        return out, -(-end // bt), bt
    for v, toks in song["voices"].items():
        notes, bars = voice(toks, bar_len, "%s %s" % (song["id"], v))
        if nbars is None:
            nbars = bars
        elif bars != nbars:
            raise Bad("%s: voice %s has %d bars, others %d" % (song["id"], v, bars, nbars))
        hand = 0 if v.startswith("rh") else 1
        for t, d, p in notes:
            out.append((int(t * PPQ), int(d * PPQ), p, hand, VEL[v[:2]]))
    out.sort(key=lambda x: (x[0], -x[2]))
    return out, nbars, int(bar_len * PPQ)


def write_mid(path, song, notes):
    """Format-1 MIDI: tempo track, right hand (ch 1), left hand (ch 2), piano, 480 PPQ."""
    import struct
    def vlq(v):
        out = [v & 0x7F]
        v >>= 7
        while v:
            out.insert(0, (v & 0x7F) | 0x80)
            v >>= 7
        return bytes(out)
    def track(events):
        data, last = b"", 0
        for t, ev in sorted(events, key=lambda e: (e[0], e[1][0] & 0xF0 == 0x90)):
            data += vlq(t - last) + ev
            last = t
        data += b"\x00\xff\x2f\x00"
        return b"MTrk" + struct.pack(">I", len(data)) + data
    k = 480 // PPQ
    us = 60000000 // song["bpm"]
    den_pow = {1: 0, 2: 1, 4: 2, 8: 3, 16: 4}[song["den"]]
    t0 = [(0, b"\xff\x51\x03" + us.to_bytes(3, "big")), (0, bytes([0xFF, 0x58, 4, song["num"], den_pow, 24, 8])),
          (0, b"\xff\x03" + bytes([len(song["title"].encode("latin-1", "replace"))]) + song["title"].encode("latin-1", "replace"))]
    hands = []
    for h, name in ((0, b"Right hand"), (1, b"Left hand")):
        ev = [(0, b"\xff\x03" + bytes([len(name)]) + name), (0, bytes([0xC0 | h, 0]))]
        for t, d, p, hh, v in notes:
            if hh == h:
                ev.append((t * k, bytes([0x90 | h, p, v])))
                ev.append(((t + d) * k - 1, bytes([0x80 | h, p, 0])))
        hands.append(track(ev))
    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 1, 3, 480) + track(t0) + hands[0] + hands[1])


def main():
    songs = parse(open(SRC).read())
    mid_dir = sys.argv[sys.argv.index("--midi") + 1] if "--midi" in sys.argv else None
    if mid_dir:
        os.makedirs(mid_dir, exist_ok=True)
    lines = ["/* generated by tools/learn_songgen.py from tools/learn_songs.txt - do not edit */",
             '#include <stddef.h>', '#include "ln_builtin.h"', ""]
    table = []
    for s in songs:
        notes, nbars, bar_t = build(s)
        lines.append("static const ln_bnote_t n_%s[%d] = {" % (s["id"], len(notes)))
        for t, d, p, h, v in notes:
            lines.append("    { %d, %d, %d, %d, %d }," % (t, d, p, h, v))
        lines.append("};")
        esc = lambda x: x.replace("\\", "\\\\").replace('"', '\\"')
        title = s["title"].replace("ü", "u").replace("é", "e")
        credit = '"%s"' % esc(s["credit"]) if s.get("credit") else "NULL"
        table.append('    { "%s", "%s", "%s", %d, %d, %d, %d, %d, %d, n_%s, %d, %s },' % (
            s["id"], esc(title), esc(s["composer"]), s["level"], s["bpm"], s["num"], s["den"], bar_t, nbars,
            s["id"], len(notes), credit))
        print("  %-13s %-32s level %d  %2d bars  %4d notes" % (s["id"], title, s["level"], nbars, len(notes)))
        if mid_dir:
            write_mid(os.path.join(mid_dir, "%02d_%s.mid" % (len(table) - 1, s["id"])), dict(s, title=title), notes)
    lines.append("")
    lines.append("const ln_builtin_t ln_builtin[] = {")
    lines += table
    lines.append("};")
    lines.append("const int ln_builtin_count = %d;" % len(songs))
    os.makedirs(os.path.dirname(DST), exist_ok=True)
    open(DST, "w").write("\n".join(lines) + "\n")
    print("wrote %s (%d songs)" % (os.path.relpath(DST), len(songs)))


if __name__ == "__main__":
    try:
        main()
    except Bad as e:
        print("ERROR:", e)
        sys.exit(1)
