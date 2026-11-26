/*
 * Learn > Note Reading: a note appears on the staff (screen 2); play that key.
 *
 * Before a round: 7 clef (treble / bass / both), 8 start, 4 back.
 * Playing:        8 pause, 4 quit.
 * After a round:  5 again, 7 clef, 4 done.
 * Your keys sound through the synth (thru is on while this screen is open).
 */
#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "midi_in.h"
#include "theme.h"
#include "widgets.h"
#include "viz.h"
#include "esp_timer.h"
#include "ln_store.h"
#include "ln_nr.h"
#include "ln_ui.h"
#include "app.h"

static int s_clef = NR_TREBLE;
static uint32_t s_best[3];
static int s_best_stars[3];
static bool s_new_best;
static int s_stars;
static uint32_t s_last_draw;

/* key presses from the MIDI task, read by the UI */
static volatile uint8_t s_q[32];
static volatile uint8_t s_qh, s_qt;

static void listener(const uint8_t *m, uint16_t len, uint8_t source)
{
    (void)source;
    if (len >= 3 && (m[0] & 0xF0) == 0x90 && m[2]) {
        uint8_t h = s_qh, nh = (uint8_t)((h + 1) % sizeof(s_q));
        if (nh != s_qt) {
            s_q[h] = m[1] & 0x7F;
            s_qh = nh;
        }
    }
}

static void best_key(char *k, int n, int clef) { snprintf(k, (size_t)n, "nr:%d|play", clef); }

static void load_best(void)
{
    for (int c = 0; c < 3; c++) {
        char k[24];
        best_key(k, sizeof(k), c);
        if (!ln_best_get(k, &s_best[c], &s_best_stars[c])) {
            s_best[c] = 0;
            s_best_stars[c] = 0;
        }
    }
}

static void enter(void *arg)
{
    (void)arg;
    memset(&g_nr, 0, sizeof(g_nr));
    g_nr.state = NR_READY;
    g_nr.clef = s_clef;
    ln_ui_load_keyboard();
    load_best();
    s_qh = s_qt = 0;
    midi_in_set_thru(true);          /* hear your answers */
    midi_in_set_listener(listener);
    viz_set_active(false);
}

static void leave(void)
{
    midi_in_set_listener(NULL);
    midi_in_set_thru(g_settings.thru);
}

static void start(void)
{
    s_qh = s_qt = 0;
    ln_nr_start(s_clef, g_kb_known ? g_kb_lo : 0, g_kb_known ? g_kb_hi : 0, (uint32_t)esp_timer_get_time() | 1, ui_now());
    s_new_best = false;
}

static void finish(void)
{
    s_stars = ln_nr_stars(&g_nr);
    char k[24];
    best_key(k, sizeof(k), g_nr.clef);
    s_new_best = ln_best_put(k, g_nr.score, s_stars) && g_nr.score;
    ln_prac_combo(g_nr.best_combo);
    ln_prac_flush();
    load_best();
}

static void tick(uint32_t now)
{
    while (s_qt != s_qh) {
        uint8_t key = s_q[s_qt];
        s_qt = (uint8_t)((s_qt + 1) % sizeof(s_q));
        if (g_nr.state == NR_RUN) {
            ln_nr_answer(key, now);
            if (g_nr.state == NR_OVER) {
                finish();
            }
            ui_invalidate();
        }
    }
    if (g_nr.state == NR_RUN) {
        ln_prac_active(now);
        ln_nr_tick(now);
        if (g_nr.state == NR_OVER) {
            finish();
        }
    }
    if (now - s_last_draw >= 100) {
        s_last_draw = now;
        ui_invalidate();
    }
}

static bool key(const btn_event_t *e)
{
    if (e->repeat) {
        return true;
    }
    int st = g_nr.state;
    switch (e->key) {
    case K_S1:
        if (st == NR_RUN || st == NR_PAUSED) {
            g_nr.state = NR_READY;   /* quit the round, no score */
        } else {
            ui_pop();
        }
        return true;
    case K_S2:
        if (st == NR_OVER) {
            start();
        }
        return true;
    case K_S4:
        if (st == NR_READY || st == NR_OVER) {
            s_clef = (s_clef + 1) % 3;
            g_nr.clef = s_clef;
            g_nr.state = NR_READY;
        }
        return true;
    case K_S5:
        if (st == NR_READY) {
            start();
        } else if (st == NR_RUN || st == NR_PAUSED) {
            ln_nr_pause(ui_now());
        }
        return true;
    default:
        return false;
    }
}

static void stats(gfx_t *g, int y)
{
    char v[3][16];
    snprintf(v[0], 16, "%u", g_nr.right);
    snprintf(v[1], 16, "%u", g_nr.wrong);
    if (g_nr.fastest_ms) {
        snprintf(v[2], 16, "%lu.%lu s", (unsigned long)(g_nr.fastest_ms / 1000), (unsigned long)(g_nr.fastest_ms % 1000 / 100));
    } else {
        snprintf(v[2], 16, "-");
    }
    w_stat(g, 10, y, 96, "Right", v[0], C_ACC);
    w_stat(g, 112, y, 96, "Wrong", v[1], C_REC);
    w_stat(g, 214, y, 96, "Fastest", v[2], C_TEXT);
}

static void draw(gfx_t *g)
{
    char r[40];
    if (g_nr.state == NR_RUN || g_nr.state == NR_PAUSED) {
        snprintf(r, sizeof(r), "level %d " UI_MIDDOT " %s", g_nr.level, ln_nr_clef_name(g_nr.clef));
    } else if (g_nr.state == NR_OVER) {
        snprintf(r, sizeof(r), "%s " UI_MIDDOT " %s", g_nr.out_of_lives ? "Out of lives" : "Time's up", ln_nr_clef_name(g_nr.clef));
    } else {
        snprintf(r, sizeof(r), "%s", ln_nr_clef_name(s_clef));
    }
    w_status(g, "Note Reading", r, C_ACC);

    if (g_nr.state == NR_READY) {
        gfx_text(g, &font_large, 12, 30, "Find the key, fast", C_TEXT);
        gfx_text(g, &font_small, 12, 56, "A note appears on screen 2. Play that key", C_DIM);
        gfx_text(g, &font_small, 12, 70, "on your keyboard (the exact octave).", C_DIM);
        gfx_text(g, &font_small, 12, 90, "One minute, 3 lives. Quick answers and", C_DIM);
        gfx_text(g, &font_small, 12, 104, "streaks score more. Notes get harder: ledger", C_DIM);
        gfx_text(g, &font_small, 12, 118, "lines after 10 right, sharps and flats after 20.", C_DIM);
        gfx_hline(g, 0, 140, g->w, C_LINE);
        gfx_text(g, &font_small, 12, 148, "BEST", C_DIM);
        for (int c = 0; c < 3; c++) {
            int y = 162 + c * 14;
            gfx_text(g, &font_small, 12, y, ln_nr_clef_name(c), c == s_clef ? C_ACC : C_TEXT);
            if (s_best[c]) {
                char s[16];
                snprintf(s, sizeof(s), "%lu", (unsigned long)s_best[c]);
                gfx_text_right(g, &font_small, 210, y, s, C_DIM);
            }
            w_stars(g, 230, y + 5, 5, 3, s_best_stars[c], C_GOLD, C_LINE);
        }
        static const char *const keys[5] = { "Back", NULL, NULL, "Clef", "Start" };
        w_softkeys(g, keys, false);
        return;
    }

    if (g_nr.state == NR_OVER) {
        w_stars(g, 104, 40, 16, 3, s_stars, C_GOLD, C_LINE);
        char sc[16];
        snprintf(sc, sizeof(sc), "%lu", (unsigned long)g_nr.score);
        gfx_text_box(g, &font_huge, 0, 62, g->w, ALIGN_CENTER, sc, C_TEXT);
        if (s_new_best) {
            gfx_fill(g, 110, 96, 100, 16, C_GOLD);
            gfx_text_box(g, &font_main, 110, 98, 100, ALIGN_CENTER, "NEW BEST!", C_BG);
        }
        stats(g, 120);
        char avg[16] = "-";
        if (g_nr.right) {
            uint32_t a = g_nr.sum_ms / g_nr.right;
            snprintf(avg, sizeof(avg), "%lu.%lu s", (unsigned long)(a / 1000), (unsigned long)(a % 1000 / 100));
        }
        char bc[12];
        snprintf(bc, sizeof(bc), "%u", g_nr.best_combo);
        w_stat(g, 10, 158, 146, "Best streak", bc, C_TEXT);
        w_stat(g, 162, 158, 148, "Average answer", avg, C_TEXT);
        static const char *const keys[5] = { "Done", "Again", NULL, "Clef", NULL };
        w_softkeys(g, keys, false);
        return;
    }

    char sc[16];
    snprintf(sc, sizeof(sc), "%lu", (unsigned long)g_nr.score);
    gfx_text(g, &font_small, 10, 26, "SCORE", C_DIM);
    gfx_text(g, &font_huge, 10, 38, sc, C_TEXT);
    char cb[12];
    snprintf(cb, sizeof(cb), "x%u", g_nr.combo);
    gfx_text(g, &font_small, 230, 26, "COMBO", C_DIM);
    gfx_text(g, &font_large, 230, 37, cb, C_AMBER);
    gfx_text(g, &font_small, 10, 82, "TIME", C_DIM);
    char tl[12];
    snprintf(tl, sizeof(tl), "%lu s", (unsigned long)((g_nr.left_ms + 999) / 1000));
    uint16_t tc = g_nr.left_ms < 10000 ? C_REC : C_AMBER;
    gfx_text_right(g, &font_small, 310, 82, tl, tc);
    w_progress(g, 10, 96, 300, 10, g_nr.left_ms, NR_ROUND_MS, tc);
    stats(g, 120);
    gfx_text(g, &font_small, 10, 162, "Lives", C_DIM);
    for (int i = 0; i < NR_LIVES; i++) {
        gfx_fill(g, 60 + i * 16, 162, 10, 10, i < g_nr.lives ? C_REC : C_LINE);
    }
    if (g_nr.state == NR_PAUSED) {
        gfx_fill(g, 10, 180, 300, 30, C_PANEL);
        gfx_text_box(g, &font_large, 0, 186, g->w, ALIGN_CENTER, "Paused", C_DIM);
    } else {
        gfx_text_box(g, &font_small, 10, 182, 300, ALIGN_LEFT, "Faster answers score more. Notes get harder", C_DIM);
        gfx_text_box(g, &font_small, 10, 196, 300, ALIGN_LEFT, "as you go: ledger lines, then sharps and flats.", C_DIM);
    }
    static const char *const keys[5] = { "Quit", NULL, NULL, NULL, "@playpause" };
    w_softkeys(g, keys, false);
}

static void draw2(gfx_t *g)
{
    gfx_clear(g, C_BG);
    if (g_nr.state == NR_OVER) {
        static const char *const head[4] = { "Keep practising", "1 star", "2 stars!", "3 stars!" };
        const uint16_t conf[5] = { C_ACC, C_BLUE, C_GOLD, C_AMBER, C_REC };
        if (s_stars >= 2) {
            uint32_t seed = 7 + ui_frame2() / 6;   /* confetti drifts down */
            for (int i = 0; i < 60; i++) {
                seed = seed * 1103515245u + 12345u;
                int x = (int)((seed >> 8) % (uint32_t)g->w);
                int y = (int)(((seed >> 20) % 50 + ui_frame2() / 3 + (uint32_t)i * 7) % 60);
                gfx_fill(g, x, y, 2 + (int)(seed & 1), 2 + (int)((seed >> 3) & 2), conf[i % 5]);
            }
        }
        gfx_text_box(g, &font_huge, 0, 40, g->w, ALIGN_CENTER, head[s_stars], s_stars ? C_GOLD : C_DIM);
        char t[64];
        snprintf(t, sizeof(t), "%u right of %u " UI_MIDDOT " reached level %d", g_nr.right, g_nr.right + g_nr.wrong, g_nr.level);
        gfx_text_box(g, &font_main, 0, 90, g->w, ALIGN_CENTER, t, C_TEXT);
        gfx_text_box(g, &font_small, 0, 112, g->w, ALIGN_CENTER, "10 right = 1 star, 20 = 2, 30 = 3", C_DIM);
        return;
    }
    bool tr = s_clef != NR_BASS, bs = s_clef != NR_TREBLE;
    ln_ui_staff(g, 4, g->w - 6, tr ? LN_STAFF_LINE : C_LINE, bs ? LN_STAFF_LINE : C_LINE);
    if (g_nr.state == NR_READY) {
        gfx_text_right(g, &font_small, g->w - 6, 4, "Press Start", C_AMBER);
        return;
    }
    /* earlier answers fade off to the left, oldest first */
    for (int i = g_nr.nhist - 1; i >= 0; i--) {
        const nr_item_t *h = &g_nr.hist[i];
        int x = 70 + (NR_HIST - 1 - i) * 30;
        ln_ui_staff_note(g, x, h->note, h->acc, h->ok ? RGB(0x2E, 0x6B, 0x47) : RGB(0x7A, 0x2F, 0x33), false);
    }
    if (g_nr.state == NR_RUN) {
        ln_ui_staff_note(g, 250, g_nr.cur.note, g_nr.cur.acc, C_TEXT, true);
        gfx_text_right(g, &font_small, g->w - 6, 4, "What note is this?", C_AMBER);
    } else {
        gfx_text_right(g, &font_small, g->w - 6, 4, "Paused", C_DIM);
    }
    if (g_nr.nhist) {
        const nr_item_t *h = &g_nr.hist[0];
        char nm[8], t[48];
        ln_ui_note_name_acc(nm, sizeof(nm), h->note, h->acc);
        if (h->ok) {
            snprintf(t, sizeof(t), "last: %s " UI_MIDDOT " right " UI_MIDDOT " %u.%u s", nm, h->ms / 1000, h->ms % 1000 / 100);
        } else {
            char wk[8];
            ln_ui_note_name(wk, sizeof(wk), g_nr.wrong_key);
            snprintf(t, sizeof(t), "last: %s " UI_MIDDOT " you played %s", nm, wk);
        }
        gfx_text(g, &font_small, 6, 4, t, h->ok ? C_ACC : C_REC);
    }
}

const screen_t scr_lnr = {
    .name = "lnr", .enter = enter, .leave = leave, .tick = tick, .key = key, .draw = draw, .draw2 = draw2,
};
