/*
 * ln_ear.c - Ear Training logic. See ln_ear.h.
 */
#include <string.h>
#include "ln_ear.h"

ln_ear_t g_ear;

#define NOTE_MS 600
#define GAP_MS 150
#define CHORD_MS 1300
#define SHOW_MS 1200

static uint32_t s_seed, s_t0;
static uint8_t s_lo, s_hi;
static ln_ear_sound_t s_snd;
static int s_step;            /* playback progress */
static uint8_t s_on[3];
static int s_non;

static const char *const s_names[EAR_NLEVELS] = { "Same or different", "Up or down", "2nds and 3rds", "4ths and 5ths",
                                                  "All intervals", "Major or minor chord" };

const char *ln_ear_level_name(int level) { return level >= 0 && level < EAR_NLEVELS ? s_names[level] : ""; }
bool ln_ear_is_choice(int level) { return level == EAR_SAME || level == EAR_UPDOWN || level == EAR_CHORD; }

const char *ln_ear_choice(int level, int i)
{
    static const char *const c[3][2] = { { "Same", "Different" }, { "Up", "Down" }, { "Major", "Minor" } };
    int k = level == EAR_SAME ? 0 : (level == EAR_UPDOWN ? 1 : 2);
    return c[k][i ? 1 : 0];
}

const char *ln_ear_interval_name(int s)
{
    static const char *const n[13] = { "unison", "minor 2nd", "major 2nd", "minor 3rd", "major 3rd", "perfect 4th",
                                       "tritone", "perfect 5th", "minor 6th", "major 6th", "minor 7th", "major 7th",
                                       "octave" };
    s = s < 0 ? -s : s;
    return s <= 12 ? n[s] : "";
}

static uint32_t rnd(uint32_t n)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return n ? (s_seed >> 8) % n : 0;
}

static void off_all(void)
{
    for (int i = 0; i < s_non; i++) {
        if (s_snd) {
            s_snd(s_on[i], false);
        }
    }
    s_non = 0;
}

static void on(uint8_t n)
{
    if (s_snd) {
        s_snd(n, true);
    }
    if (s_non < 3) {
        s_on[s_non++] = n;
    }
}

static uint8_t clamp_note(int n)
{
    int lo = s_lo ? s_lo : 21, hi = s_hi ? s_hi : 108;
    while (n < lo) n += 12;
    while (n > hi) n -= 12;
    return (uint8_t)n;
}

static void listen(uint32_t now)
{
    off_all();
    g_ear.state = EQ_LISTEN;
    s_t0 = now;
    s_step = 0;
}

static void new_question(uint32_t now)
{
    g_ear.tries = EAR_TRIES;
    g_ear.hint = false;
    g_ear.last = -1;
    int base = 60 + (int)rnd(8);   /* C4 .. G4 */
    int lv = g_ear.level, d = 0;
    switch (lv) {
    case EAR_SAME:
        g_ear.answer = (int)rnd(2);
        d = g_ear.answer ? 1 + (int)rnd(4) : 0;
        if (d && rnd(2)) d = -d;
        break;
    case EAR_UPDOWN:
        g_ear.answer = (int)rnd(2);
        d = 1 + (int)rnd(12);
        if (g_ear.answer) d = -d;
        break;
    case EAR_SMALL: d = 1 + (int)rnd(4); break;
    case EAR_FIFTHS: d = 1 + (int)rnd(7); break;
    case EAR_ALL:
        d = 1 + (int)rnd(12);
        if (rnd(2)) d = -d;
        break;
    default: {   /* chord */
        g_ear.answer = (int)rnd(2);
        int r = clamp_note(base - 5 + (int)rnd(10));
        g_ear.chord[0] = (uint8_t)r;
        g_ear.chord[1] = (uint8_t)(r + (g_ear.answer ? 3 : 4));
        g_ear.chord[2] = (uint8_t)(r + 7);
        break;
    }
    }
    if (lv != EAR_CHORD) {
        int a = clamp_note(base);
        if (a + d > (s_hi ? s_hi : 108) || a + d < (s_lo ? s_lo : 21)) {
            d = -d;   /* keep both notes on your keyboard */
            if (lv == EAR_UPDOWN) g_ear.answer = !g_ear.answer;
        }
        g_ear.a = (uint8_t)a;
        g_ear.b = (uint8_t)(a + d);
    }
    listen(now);
}

void ln_ear_start(int level, int max_level, uint8_t kb_lo, uint8_t kb_hi, uint32_t seed, ln_ear_sound_t snd, uint32_t now)
{
    off_all();
    memset(&g_ear, 0, sizeof(g_ear));
    g_ear.level = level;
    g_ear.max_level = max_level < level ? level : max_level;
    s_lo = kb_lo;
    s_hi = kb_hi;
    s_seed = seed ? seed : 1;
    s_snd = snd;
    new_question(now);
}

void ln_ear_tick(uint32_t now)
{
    uint32_t t = now - s_t0;
    if (g_ear.state == EQ_LISTEN) {
        if (g_ear.level == EAR_CHORD) {
            if (s_step == 0) {
                for (int i = 0; i < 3; i++) on(g_ear.chord[i]);
                s_step = 1;
            }
            if (s_step == 1 && t >= CHORD_MS) {
                off_all();
                g_ear.state = EQ_ASK;
            }
            return;
        }
        if (s_step == 0) {
            on(g_ear.a);
            s_step = 1;
        }
        if (s_step == 1 && t >= NOTE_MS) {
            off_all();
            s_step = 2;
        }
        if (s_step == 2 && t >= NOTE_MS + GAP_MS) {
            on(g_ear.b);
            s_step = 3;
        }
        if (s_step == 3 && t >= 2 * NOTE_MS + GAP_MS) {
            off_all();
            g_ear.state = EQ_ASK;
        }
        return;
    }
    if (g_ear.state == EQ_SHOW && t >= SHOW_MS) {
        off_all();
        if (++g_ear.q >= EAR_QUESTIONS) {
            g_ear.state = EQ_DONE;
        } else {
            new_question(now);
        }
    }
}

void ln_ear_replay(uint32_t now)
{
    if (g_ear.state == EQ_ASK || g_ear.state == EQ_LISTEN) {
        listen(now);
    }
}

static void show(uint32_t now)
{
    g_ear.state = EQ_SHOW;
    s_t0 = now;
}

void ln_ear_skip(uint32_t now)
{
    if (g_ear.state != EQ_ASK && g_ear.state != EQ_LISTEN) {
        return;
    }
    off_all();
    g_ear.missed++;
    g_ear.combo = 0;
    g_ear.last = 0;
    g_ear.last_ms = now;
    show(now);
}

void ln_ear_hint(void)
{
    if (g_ear.state == EQ_ASK || g_ear.state == EQ_LISTEN) {
        g_ear.hint = true;
    }
}

static int answered(bool ok, uint32_t now)
{
    g_ear.last = ok;
    g_ear.last_ms = now;
    g_ear.levelled_up = false;
    if (ok) {
        bool first = g_ear.tries == EAR_TRIES;
        int pts = first ? 100 : (g_ear.tries == EAR_TRIES - 1 ? 50 : 25);
        if (g_ear.hint) pts /= 2;
        int mult = 10 + (g_ear.combo < 30 ? g_ear.combo : 30);
        g_ear.score += (uint32_t)(pts * mult / 10);
        if (first && !g_ear.hint) {
            g_ear.first++;
            g_ear.combo++;
            g_ear.best_combo = g_ear.combo > g_ear.best_combo ? g_ear.combo : g_ear.best_combo;
            if (++g_ear.level_right == EAR_LEVEL_UP && g_ear.level == g_ear.max_level && g_ear.max_level < EAR_NLEVELS - 1) {
                g_ear.max_level++;
                g_ear.levelled_up = true;
            }
        } else {
            g_ear.later++;
            g_ear.combo = 0;
        }
        show(now);
        return 1;
    }
    g_ear.combo = 0;
    if (--g_ear.tries <= 0) {
        g_ear.missed++;
        show(now);
        if (!ln_ear_is_choice(g_ear.level)) {
            on(g_ear.b);   /* hear the right answer */
        }
    }
    return 0;
}

int ln_ear_key(uint8_t key, uint32_t now)
{
    if (g_ear.state != EQ_ASK || ln_ear_is_choice(g_ear.level)) {
        return -1;
    }
    if (key == g_ear.a && g_ear.a != g_ear.b) {
        return -1;   /* playing the given note again is fine */
    }
    g_ear.last_key = key;
    return answered(key == g_ear.b, now);
}

int ln_ear_choose(int i, uint32_t now)
{
    if (g_ear.state != EQ_ASK || !ln_ear_is_choice(g_ear.level)) {
        return -1;
    }
    return answered(i == g_ear.answer, now);
}

void ln_ear_stop(void) { off_all(); }

int ln_ear_stars(const ln_ear_t *e)
{
    return e->first >= 19 ? 3 : (e->first >= 16 ? 2 : (e->first >= 12 ? 1 : 0));
}
