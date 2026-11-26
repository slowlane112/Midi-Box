/*
 * ln_nr.c - Note Reading game logic. See ln_nr.h.
 */
#include <string.h>
#include "ln_nr.h"

ln_nr_t g_nr;

static uint32_t s_seed, s_last_ms, s_shown_ms;
static uint8_t s_lo, s_hi;
static const uint8_t s_nat[7] = { 0, 2, 4, 5, 7, 9, 11 };

/* diatonic number: octave * 7 + letter (C4 = 28) */
#define DN(oct, letter) ((oct) * 7 + (letter))

static int pitch_of(int dn, int acc) { return (dn / 7 + 1) * 12 + s_nat[dn % 7] + acc; }

static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

/* the range of staff positions for a clef and level */
static void range(bool bass, int level, int *lo, int *hi)
{
    if (!bass) {
        *lo = level >= 2 ? DN(3, 5) : DN(4, 2);   /* A3 .. C6, or E4 .. F5 */
        *hi = level >= 2 ? DN(6, 0) : DN(5, 3);
    } else {
        *lo = level >= 2 ? DN(2, 0) : DN(2, 4);   /* C2 .. E4, or G2 .. A3 */
        *hi = level >= 2 ? DN(4, 2) : DN(3, 5);
    }
}

static bool fits(int p) { return (!s_lo && !s_hi) || (p >= s_lo && p <= s_hi); }

static void next_note(void)
{
    nr_item_t prev = g_nr.cur;
    for (int tries = 0; tries < 200; tries++) {
        bool bass = g_nr.clef == NR_BASS || (g_nr.clef == NR_BOTH && (rnd() & 1));
        int lo, hi;
        range(bass, g_nr.level, &lo, &hi);
        int dn = lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
        int acc = 0;
        if (g_nr.level >= 3 && rnd() % 3 == 0) {
            int l = dn % 7;
            /* a sharp or a flat, but no E#, B#, Cb or Fb */
            bool can_sharp = l != 2 && l != 6, can_flat = l != 0 && l != 3;
            acc = can_sharp && (!can_flat || (rnd() & 1)) ? 1 : (can_flat ? -1 : 0);
        }
        int p = pitch_of(dn, acc);
        if (!fits(p) && tries < 190) {
            continue;
        }
        if (p == prev.note && tries < 150) {
            continue;
        }
        g_nr.cur.note = (uint8_t)p;
        g_nr.cur.acc = (int8_t)acc;
        g_nr.cur.bass = bass;
        g_nr.cur.ok = 0;
        g_nr.cur.ms = 0;
        return;
    }
}

void ln_nr_start(int clef, uint8_t kb_lo, uint8_t kb_hi, uint32_t seed, uint32_t now_ms)
{
    memset(&g_nr, 0, sizeof(g_nr));
    g_nr.clef = clef;
    g_nr.level = 1;
    g_nr.lives = NR_LIVES;
    g_nr.left_ms = NR_ROUND_MS;
    g_nr.state = NR_RUN;
    s_seed = seed ? seed : 1;
    s_lo = kb_lo;
    s_hi = kb_hi;
    s_last_ms = s_shown_ms = now_ms;
    next_note();
}

void ln_nr_tick(uint32_t now_ms)
{
    uint32_t dt = now_ms - s_last_ms;
    s_last_ms = now_ms;
    if (g_nr.state != NR_RUN) {
        return;
    }
    g_nr.left_ms = dt >= g_nr.left_ms ? 0 : g_nr.left_ms - dt;
    if (!g_nr.left_ms) {
        g_nr.state = NR_OVER;
    }
}

void ln_nr_pause(uint32_t now_ms)
{
    ln_nr_tick(now_ms);
    if (g_nr.state == NR_RUN) {
        g_nr.state = NR_PAUSED;
    } else if (g_nr.state == NR_PAUSED) {
        g_nr.state = NR_RUN;
        s_shown_ms = now_ms;   /* the answer clock starts again too */
    }
}

int ln_nr_answer(uint8_t key, uint32_t now_ms)
{
    ln_nr_tick(now_ms);
    if (g_nr.state != NR_RUN) {
        return -1;
    }
    uint32_t ms = now_ms - s_shown_ms;
    bool ok = key == g_nr.cur.note;
    g_nr.cur.ok = ok;
    g_nr.cur.ms = (uint16_t)(ms > 60000 ? 60000 : ms);
    g_nr.flash_ms = now_ms;
    if (ok) {
        int speed = ms <= 500 ? 100 : (ms >= 3000 ? 0 : 100 - (int)(ms - 500) / 25);
        int mult = 10 + (g_nr.combo < 30 ? g_nr.combo : 30);
        g_nr.score += (uint32_t)((100 + speed) * mult / 10);
        g_nr.combo++;
        g_nr.best_combo = g_nr.combo > g_nr.best_combo ? g_nr.combo : g_nr.best_combo;
        g_nr.right++;
        g_nr.sum_ms += ms;
        if (!g_nr.fastest_ms || ms < g_nr.fastest_ms) {
            g_nr.fastest_ms = ms ? ms : 1;
        }
        g_nr.level = g_nr.right >= 20 ? 3 : (g_nr.right >= 10 ? 2 : 1);
    } else {
        g_nr.combo = 0;
        g_nr.wrong++;
        g_nr.wrong_key = key;
        if (--g_nr.lives <= 0) {
            g_nr.lives = 0;
            g_nr.out_of_lives = true;
            g_nr.state = NR_OVER;
        }
    }
    memmove(&g_nr.hist[1], &g_nr.hist[0], sizeof(nr_item_t) * (NR_HIST - 1));
    g_nr.hist[0] = g_nr.cur;
    if (g_nr.nhist < NR_HIST) {
        g_nr.nhist++;
    }
    if (g_nr.state == NR_RUN) {
        next_note();
        s_shown_ms = now_ms;
    }
    return ok;
}

int ln_nr_stars(const ln_nr_t *s)
{
    int st = s->right >= 30 ? 3 : (s->right >= 20 ? 2 : (s->right >= 10 ? 1 : 0));
    int total = s->right + s->wrong;
    if (st > 1 && total && s->right * 100 / total < 70) {
        st = 1;
    }
    return st;
}

const char *ln_nr_clef_name(int clef)
{
    static const char *const n[3] = { "Treble clef", "Bass clef", "Both clefs" };
    return clef >= 0 && clef < 3 ? n[clef] : "";
}
