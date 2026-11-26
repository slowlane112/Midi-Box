/*
 * Learn > Ear Training: the box plays, you answer. 20 questions a round.
 *
 * Before a round: 2/3 level (any level you've unlocked), 8 start, 4 back.
 * Playing:  choice levels (same/different, up/down, major/minor): 6 / 7 answer.
 *           note levels: play the second note on your keyboard.
 *           5 replay, 7 skip (note levels), 8 hint (note levels: shows the key, half points), 4 quit.
 * After:    5 again, 8 next level (if unlocked), 4 done.
 */
#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "midi_in.h"
#include "midi_out.h"
#include "theme.h"
#include "widgets.h"
#include "viz.h"
#include "esp_timer.h"
#include "ln_store.h"
#include "ln_ear.h"
#include "ln_ui.h"
#include "app.h"

#define EAR_CH 0
static int s_level;
static int s_max;
static uint32_t s_best[EAR_NLEVELS];
static int s_best_stars[EAR_NLEVELS];
static bool s_new_best;
static int s_stars;
static uint32_t s_last_draw;

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

static void sound(uint8_t note, bool on) { midi_out_note(EAR_CH, note, on ? 96 : 0, on); }

static void best_key(char *k, int n, int level) { snprintf(k, (size_t)n, "ear:%d|play", level); }

static void load_best(void)
{
    for (int l = 0; l < EAR_NLEVELS; l++) {
        char k[24];
        best_key(k, sizeof(k), l);
        if (!ln_best_get(k, &s_best[l], &s_best_stars[l])) {
            s_best[l] = 0;
            s_best_stars[l] = 0;
        }
    }
}

static void enter(void *arg)
{
    (void)arg;
    memset(&g_ear, 0, sizeof(g_ear));
    g_ear.state = EQ_READY;
    s_max = ln_ear_max_load();
    s_level = s_max;
    ln_ui_load_keyboard();
    load_best();
    s_qh = s_qt = 0;
    midi_out_program(EAR_CH, 0);
    midi_in_set_thru(true);
    midi_in_set_listener(listener);
    viz_set_active(false);
}

static void leave(void)
{
    ln_ear_stop();
    midi_in_set_listener(NULL);
    midi_in_set_thru(g_settings.thru);
}

static void start(void)
{
    s_qh = s_qt = 0;
    s_new_best = false;
    ln_ear_start(s_level, s_max, g_kb_known ? g_kb_lo : 0, g_kb_known ? g_kb_hi : 0,
                 (uint32_t)esp_timer_get_time() | 1, sound, ui_now());
}

static void finish(void)
{
    s_stars = ln_ear_stars(&g_ear);
    char k[24];
    best_key(k, sizeof(k), g_ear.level);
    s_new_best = ln_best_put(k, g_ear.score, s_stars) && g_ear.score;
    if (g_ear.max_level > s_max) {
        s_max = g_ear.max_level;
        ln_ear_max_save(s_max);
    }
    ln_prac_combo(g_ear.best_combo);
    ln_prac_flush();
    load_best();
}

static bool playing(void) { return g_ear.state == EQ_LISTEN || g_ear.state == EQ_ASK || g_ear.state == EQ_SHOW; }

static void tick(uint32_t now)
{
    while (s_qt != s_qh) {
        uint8_t key = s_q[s_qt];
        s_qt = (uint8_t)((s_qt + 1) % sizeof(s_q));
        if (ln_ear_key(key, now) >= 0) {
            ui_invalidate();
        }
    }
    if (playing()) {
        ln_prac_active(now);
        int up = g_ear.max_level;
        ln_ear_tick(now);
        if (g_ear.max_level != up && g_ear.max_level > s_max) {
            ln_ear_max_save(g_ear.max_level);   /* keep it even if you quit */
            s_max = g_ear.max_level;
        }
        if (g_ear.state == EQ_DONE) {
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
    bool choice = ln_ear_is_choice(g_ear.level);
    if (g_ear.state == EQ_READY) {
        switch (e->key) {
        case K_UP: s_level = s_level > 0 ? s_level - 1 : s_max; return true;
        case K_DOWN: s_level = s_level < s_max ? s_level + 1 : 0; return true;
        case K_S1: if (!e->repeat) ui_pop(); return true;
        case K_S5: if (!e->repeat) start(); return true;
        default: return false;
        }
    }
    if (e->repeat) {
        return true;
    }
    if (g_ear.state == EQ_DONE) {
        switch (e->key) {
        case K_S1: g_ear.state = EQ_READY; return true;
        case K_S2: start(); return true;
        case K_S5:
            if (s_level < s_max) {
                s_level++;
                start();
            }
            return true;
        default: return false;
        }
    }
    uint32_t now = ui_now();
    switch (e->key) {
    case K_S1:
        ln_ear_stop();
        g_ear.state = EQ_READY;
        return true;
    case K_S2: ln_ear_replay(now); return true;
    case K_S3:
        if (choice) ln_ear_choose(0, now);
        return true;
    case K_S4:
        if (choice) {
            ln_ear_choose(1, now);
        } else {
            ln_ear_skip(now);
        }
        return true;
    case K_S5:
        if (!choice) ln_ear_hint();
        return true;
    default:
        return false;
    }
}

static void prompt(char *big, int nb, char *small, int ns, uint16_t *bc)
{
    char a[8], b[8];
    ln_ui_note_name(a, sizeof(a), g_ear.a);
    ln_ui_note_name(b, sizeof(b), g_ear.b);
    *bc = C_AMBER;
    small[0] = 0;
    if (g_ear.state == EQ_LISTEN) {
        snprintf(big, (size_t)nb, "Listen" UI_ELLIPSIS);
        *bc = C_DIM;
        return;
    }
    if (g_ear.state == EQ_SHOW) {
        bool ok = g_ear.last == 1;
        *bc = ok ? C_ACC : C_REC;
        if (g_ear.level == EAR_CHORD) {
            snprintf(big, (size_t)nb, "%s " UI_ENDASH " %s", ok ? "Right!" : "It was", g_ear.answer ? "minor" : "major");
        } else if (ln_ear_is_choice(g_ear.level)) {
            snprintf(big, (size_t)nb, "%s " UI_ENDASH " %s", ok ? "Right!" : "It was", ln_ear_choice(g_ear.level, g_ear.answer));
        } else {
            snprintf(big, (size_t)nb, "%s %s + %s", ok ? "Right!" : "It was", a, b);
        }
        if (!ln_ear_is_choice(g_ear.level) || g_ear.level == EAR_UPDOWN) {
            snprintf(small, (size_t)ns, "%s %s", ln_ear_interval_name(g_ear.b - g_ear.a), g_ear.b > g_ear.a ? "up" : (g_ear.b < g_ear.a ? "down" : ""));
        }
        if (g_ear.levelled_up) {
            snprintf(small, (size_t)ns, "New level unlocked: %s", ln_ear_level_name(g_ear.max_level));
        }
        return;
    }
    switch (g_ear.level) {
    case EAR_SAME: snprintf(big, (size_t)nb, "Same or different?"); break;
    case EAR_UPDOWN: snprintf(big, (size_t)nb, "Up or down?"); break;
    case EAR_CHORD: snprintf(big, (size_t)nb, "Major or minor?"); break;
    default:
        snprintf(big, (size_t)nb, "%s  +  %s", a, g_ear.hint ? b : "?");
        snprintf(small, (size_t)ns, "First note is given: %s. Play the second.", a);
        break;
    }
    if (g_ear.last == 0) {
        char w[8];
        ln_ui_note_name(w, sizeof(w), g_ear.last_key);
        if (ln_ear_is_choice(g_ear.level)) {
            snprintf(small, (size_t)ns, "Not quite " UI_ENDASH " try again (5 plays it again)");
        } else {
            snprintf(small, (size_t)ns, "Not %s " UI_ENDASH " try again (5 plays it again)", w);
        }
    }
}

static void draw(gfx_t *g)
{
    char r[48];
    if (g_ear.state == EQ_READY) {
        w_status(g, "Ear Training", "choose a level", C_ACC);
        for (int l = 0; l < EAR_NLEVELS; l++) {
            int y = CONTENT_Y + 2 + l * 28;
            bool sel = l == s_level, locked = l > s_max;
            w_row(g, y, 27, sel);
            char t[32];
            snprintf(t, sizeof(t), "%d  %s", l + 1, ln_ear_level_name(l));
            gfx_text(g, &font_main, 12, y + 7, t, locked ? C_LINE : (sel ? C_ACC : C_TEXT));
            if (locked) {
                gfx_text_right(g, &font_small, g->w - 10, y + 8, "locked", C_LINE);
            } else {
                w_stars(g, g->w - 52, y + 13, 6, 3, s_best_stars[l], C_GOLD, C_LINE);
            }
        }
        static const char *const keys[5] = { "Back", NULL, NULL, NULL, "Start" };
        w_softkeys(g, keys, false);
        return;
    }
    if (g_ear.state == EQ_DONE) {
        snprintf(r, sizeof(r), "Level %d " UI_MIDDOT " %s", g_ear.level + 1, ln_ear_level_name(g_ear.level));
        w_status(g, "Results", r, C_ACC);
        w_stars(g, 104, 44, 16, 3, s_stars, C_GOLD, C_LINE);
        char sc[16];
        snprintf(sc, sizeof(sc), "%lu", (unsigned long)g_ear.score);
        gfx_text_box(g, &font_huge, 0, 66, g->w, ALIGN_CENTER, sc, C_TEXT);
        if (s_new_best) {
            gfx_fill(g, 110, 100, 100, 16, C_GOLD);
            gfx_text_box(g, &font_main, 110, 102, 100, ALIGN_CENTER, "NEW BEST!", C_BG);
        }
        char v[3][12];
        snprintf(v[0], 12, "%d", g_ear.first);
        snprintf(v[1], 12, "%d", g_ear.later);
        snprintf(v[2], 12, "%d", g_ear.missed);
        w_stat(g, 10, 124, 96, "First try", v[0], C_ACC);
        w_stat(g, 112, 124, 96, "Later try", v[1], C_BLUE);
        w_stat(g, 214, 124, 96, "Missed", v[2], C_REC);
        char bc[12];
        snprintf(bc, sizeof(bc), "%u", g_ear.best_combo);
        w_stat(g, 10, 160, 146, "Best streak", bc, C_TEXT);
        w_stat(g, 162, 160, 148, "Unlocked", ln_ear_level_name(s_max), C_TEXT);
        const char *keys[5] = { "Done", "Again", NULL, NULL, s_level < s_max ? "Next" : NULL };
        w_softkeys(g, keys, false);
        return;
    }
    snprintf(r, sizeof(r), "Level %d " UI_MIDDOT " %d of %d", g_ear.level + 1, g_ear.q + 1, EAR_QUESTIONS);
    w_status(g, "Ear Training", r, C_ACC);
    char sc[16];
    snprintf(sc, sizeof(sc), "%lu", (unsigned long)g_ear.score);
    gfx_text(g, &font_small, 10, 26, "SCORE", C_DIM);
    gfx_text(g, &font_huge, 10, 38, sc, C_TEXT);
    char cb[12];
    snprintf(cb, sizeof(cb), "x%u", g_ear.combo);
    gfx_text(g, &font_small, 230, 26, "COMBO", C_DIM);
    gfx_text(g, &font_large, 230, 37, cb, C_AMBER);
    gfx_fill(g, 10, 80, 300, 54, C_PANEL);
    char big[48], small[64];
    uint16_t bc2;
    prompt(big, sizeof(big), small, sizeof(small), &bc2);
    gfx_text_box(g, &font_large, 0, 88, g->w, ALIGN_CENTER, big, bc2);
    gfx_text_box(g, &font_small, 12, 114, 296, ALIGN_CENTER, small, C_DIM);
    char v[3][24];
    snprintf(v[0], 24, "%d", g_ear.first + g_ear.later);
    snprintf(v[1], 24, "%d", g_ear.state == EQ_SHOW ? 0 : g_ear.tries);
    if (g_ear.max_level > g_ear.level || g_ear.max_level == EAR_NLEVELS - 1) {
        snprintf(v[2], 24, "unlocked");
    } else {
        snprintf(v[2], 24, "%d to go", EAR_LEVEL_UP - g_ear.level_right);
    }
    w_stat(g, 10, 144, 96, "Right", v[0], C_ACC);
    w_stat(g, 112, 144, 96, "Tries left", v[1], C_TEXT);
    w_stat(g, 214, 144, 96, "Next level", v[2], C_TEXT);
    bool choice = ln_ear_is_choice(g_ear.level);
    const char *keys[5] = { "Quit", "Replay", choice ? ln_ear_choice(g_ear.level, 0) : NULL,
                            choice ? ln_ear_choice(g_ear.level, 1) : "Skip", choice ? NULL : "Hint" };
    w_softkeys(g, keys, false);
}

static void draw2(gfx_t *g)
{
    gfx_clear(g, C_BG);
    if (g_ear.state == EQ_DONE) {
        static const char *const head[4] = { "Keep practising", "1 star", "2 stars!", "3 stars!" };
        gfx_text_box(g, &font_huge, 0, 40, g->w, ALIGN_CENTER, head[s_stars], s_stars ? C_GOLD : C_DIM);
        char t[64];
        snprintf(t, sizeof(t), "%d of %d right first time", g_ear.first, EAR_QUESTIONS);
        gfx_text_box(g, &font_main, 0, 90, g->w, ALIGN_CENTER, t, C_TEXT);
        gfx_text_box(g, &font_small, 0, 112, g->w, ALIGN_CENTER, "12 = 1 star, 16 = 2, 19 = 3", C_DIM);
        return;
    }
    bool listening = g_ear.state == EQ_LISTEN;
    if (g_ear.state == EQ_READY) {
        gfx_text(g, &font_large, 12, 10, ln_ear_level_name(s_level), C_ACC);
        static const char *const how[EAR_NLEVELS][2] = {
            { "Two notes: the same, or not?", "Answer with 6 or 7." },
            { "Two notes: does it go up or down?", "Answer with 6 or 7." },
            { "Two notes a 2nd or 3rd apart.", "Play the second one on your keyboard." },
            { "Two notes up to a 5th apart.", "Play the second one on your keyboard." },
            { "Any interval up to an octave, up or down.", "Play the second one on your keyboard." },
            { "Three notes together: happy or sad?", "Answer with 6 (major) or 7 (minor)." },
        };
        gfx_text(g, &font_small, 12, 40, how[s_level][0], C_DIM);
        gfx_text(g, &font_small, 12, 54, how[s_level][1], C_DIM);
        if (s_best[s_level]) {
            char b[32];
            snprintf(b, sizeof(b), "Best %lu", (unsigned long)s_best[s_level]);
            gfx_text(g, &font_small, 12, 80, b, C_TEXT);
        }
    } else {
        const char *head = listening ? "Listening" UI_ELLIPSIS : "Your turn";
        uint16_t hc = listening ? C_ACC : C_AMBER;
        if (g_ear.state == EQ_SHOW) {
            head = g_ear.last == 1 ? "Right!" : "Not this time";
            hc = g_ear.last == 1 ? C_ACC : C_REC;
        }
        gfx_text(g, &font_large, 12, 10, head, hc);
        /* sound waves while the box plays */
        uint32_t f = ui_frame2();
        for (int i = 0; i < 3; i++) {
            int r = 18 + i * 14;
            uint16_t c = listening && (int)((f / 8) % 3) == i ? C_ACC : RGB(0x2E, 0x6B, 0x47);
            if (!listening) {
                c = C_LINE;
            }
            static const int8_t cs[11][2] = { { 64, -38 }, { 69, -30 }, { 72, -22 }, { 74, -13 }, { 75, -4 }, { 75, 4 },
                                              { 74, 13 }, { 72, 22 }, { 69, 30 }, { 64, 38 }, { 58, 45 } };
            for (int k = 0; k < 10; k++) {
                int x = 70 + cs[k][0] * r / 75, y = 78 + cs[k][1] * r / 75;
                gfx_fill(g, x, y, 2, 2, c);
            }
        }
        gfx_fill(g, 40, 66, 14, 24, listening ? C_ACC : C_LINE);
        gfx_fill(g, 54, 60, 10, 36, listening ? C_ACC : C_LINE);
    }
    /* the levels */
    int lx = g_ear.state == EQ_READY ? 290 : 150;
    gfx_text(g, &font_small, lx, 40, "LEVELS", C_DIM);
    int cur = g_ear.state == EQ_READY ? s_level : g_ear.level;
    int max = g_ear.state == EQ_READY ? s_max : g_ear.max_level;
    for (int l = 0; l < EAR_NLEVELS; l++) {
        int y = 54 + l * 13;
        int x0 = lx;
        uint16_t c = l == cur ? C_AMBER : (l <= max ? C_ACC : C_DIM);
        if (l == cur) {
            gfx_tri_right(g, x0, y + 1, 9, C_AMBER);
        } else if (l <= max) {
            gfx_fill(g, x0, y + 3, 6, 6, C_ACC);
        }
        gfx_text(g, &font_small, x0 + 12, y, ln_ear_level_name(l), c);
    }
    /* one octave of keys around the question: your answer, and the right one when shown */
    if (g_ear.state != EQ_READY && !ln_ear_is_choice(g_ear.level)) {
        int lo = g_ear.a < g_ear.b ? g_ear.a : g_ear.b;
        lo = lo - lo % 12;
        int hi = (g_ear.a > g_ear.b ? g_ear.a : g_ear.b);
        hi = hi - hi % 12 + 12;
        ln_kb_t k;
        ln_ui_kb_layout(&k, lo, hi, 300, g->w - 308, 100, 36);
        static uint16_t col[128];
        memset(col, 0, sizeof(col));
        col[g_ear.a] = C_BLUE;
        if (g_ear.last >= 0 && g_ear.last_key < 128) {
            col[g_ear.last_key] = g_ear.last ? C_ACC : C_REC;
        }
        if (g_ear.hint || g_ear.state == EQ_SHOW) {
            col[g_ear.b] = g_ear.last == 1 ? C_ACC : C_AMBER;
        }
        ln_ui_kb_draw(g, &k, col, 0, 0);
        gfx_text(g, &font_small, 300, 84, "blue: given", C_DIM);
    }
}

const screen_t scr_lear = {
    .name = "lear", .enter = enter, .leave = leave, .tick = tick, .key = key, .draw = draw, .draw2 = draw2,
};
