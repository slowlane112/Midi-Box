/*
 * Learn > Play Along: Setup, Playing and Results.
 *
 * Setup:   2/3 setting, 5/6 change, 8 start, 4 back.
 * Playing: 4 stop, 5 back a bar, 6 pause, 7 loop 4 bars, 8 note-name hints. Screen 2 is the game:
 *          notes fall onto a keyboard sized to yours; green are yours, blue the box plays.
 * Results: 4 done, 5 retry, 7 faster, 8 next song.
 */
#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "midi_out.h"
#include "theme.h"
#include "widgets.h"
#include "viz.h"
#include "ln_builtin.h"
#include "ln_song.h"
#include "ln_store.h"
#include "ln_ui.h"
#include "app.h"

/* ================================================================== setup */

enum { F_HAND, F_MODE, F_SPEED, F_START, F_COUNTIN, F_RHPART, F_LHPART, F_COUNT };
static int s_field;
static int s_fields;          /* 5 for built-in songs, 7 for MIDI files */
static int s_shift, s_out;    /* fitting to your keyboard */
static uint8_t s_lo, s_hi;    /* range of your notes */
static int s_best_stars;

static void refit(void)
{
    s_shift = ln_ui_fit(&s_out, &s_lo, &s_hi);
    char k[240];
    ln_ui_score_key(k, sizeof(k));
    uint32_t sc;
    if (!ln_best_get(k, &sc, &s_best_stars)) {
        s_best_stars = 0;
    }
}

static bool has_left(void) { return g_ln.lh_part != LN_NONE; }

static void setup_enter(void *arg)
{
    (void)arg;
    s_fields = g_ln.builtin ? 5 : F_COUNT;
    s_field = 0;
    g_lset.start_bar = 0;
    if (!has_left() && g_lset.hand != LH_RIGHT) {
        g_lset.hand = LH_RIGHT;
    }
    refit();
}

static void setup_resume(void)
{
    /* back from Results (maybe a different song now) */
    s_fields = g_ln.builtin ? 5 : F_COUNT;
    if (g_lset.start_bar >= g_ln.nbars - 1) {
        g_lset.start_bar = 0;
    }
    refit();
    ui_invalidate();
}

/* the parts a hand can be: any part with notes that isn't drums */
static int next_part(int cur, int dir, bool allow_split)
{
    int n = g_ln.nparts;
    int pos = cur;
    for (int i = 0; i < n + 2; i++) {
        pos += dir;
        if (pos < (allow_split ? LN_SPLIT : LN_NONE)) {
            pos = n - 1;
        } else if (pos >= n) {
            pos = allow_split ? LN_SPLIT : LN_NONE;
        }
        if (pos < 0 || (!g_ln.parts[pos].drums && g_ln.parts[pos].count)) {
            return pos;
        }
    }
    return cur;
}

static void change(int d)
{
    switch (s_field) {
    case F_HAND:
        g_lset.hand = has_left() ? (g_lset.hand + 3 + d) % 3 : LH_RIGHT;
        break;
    case F_MODE:
        g_lset.mode = !g_lset.mode;
        break;
    case F_SPEED: {
        int v = g_lset.speed + d * 10;
        g_lset.speed = v < 30 ? 30 : (v > 150 ? 150 : v);
        break;
    }
    case F_START: {
        int v = g_lset.start_bar + d;
        int max = g_ln.nbars - 2 > 0 ? g_ln.nbars - 2 : 0;
        g_lset.start_bar = v < 0 ? 0 : (v > max ? max : v);
        break;
    }
    case F_COUNTIN:
        g_lset.countin = (g_lset.countin + 3 + d) % 3;
        break;
    case F_RHPART: {
        int p = next_part(g_ln.rh_part, d, false);
        if (p >= 0 && p != g_ln.lh_part) {
            ln_song_assign(p, g_ln.lh_part);
        }
        break;
    }
    case F_LHPART: {
        int p = next_part(g_ln.lh_part, d, true);
        if (p == g_ln.rh_part) {
            p = next_part(p, d, true);
        }
        ln_song_assign(g_ln.rh_part, p);
        if (!has_left()) {
            g_lset.hand = LH_RIGHT;
        }
        break;
    }
    default:
        break;
    }
    refit();
}

static bool setup_key(const btn_event_t *e)
{
    switch (e->key) {
    case K_UP: s_field = (s_field + s_fields - 1) % s_fields; return true;
    case K_DOWN: s_field = (s_field + 1) % s_fields; return true;
    case K_S2: change(e->shift ? -5 : -1); return true;
    case K_S3: change(e->shift ? 5 : 1); return true;
    case K_S1:
        if (!e->repeat) {
            ui_pop();
        }
        return true;
    case K_S5:
        if (!e->repeat) {
            if (g_ln.rh_part < 0) {
                ui_toast("Choose the right-hand part first");
            } else {
                ui_push(&scr_lplay, NULL);
            }
        }
        return true;
    default:
        return false;
    }
}

static void part_name(char *o, int n, int p)
{
    if (p == LN_NONE) {
        snprintf(o, (size_t)n, "None");
    } else if (p == LN_SPLIT) {
        snprintf(o, (size_t)n, "Split at middle C");
    } else {
        snprintf(o, (size_t)n, "%s", g_ln.parts[p].name);
    }
}

static void value_text(int f, char *o, int n)
{
    switch (f) {
    case F_HAND: snprintf(o, (size_t)n, "%s", g_lset.hand == LH_BOTH ? "Both" : (g_lset.hand == LH_LEFT ? "Left" : "Right")); break;
    case F_MODE: snprintf(o, (size_t)n, "%s", g_lset.mode == LN_MODE_WAIT ? "Wait for me" : "Play (scored)"); break;
    case F_SPEED: snprintf(o, (size_t)n, "%d%%", g_lset.speed); break;
    case F_START: snprintf(o, (size_t)n, "Bar %d", g_lset.start_bar + 1); break;
    case F_COUNTIN: snprintf(o, (size_t)n, g_lset.countin ? "%d bar%s" : "Off", g_lset.countin, g_lset.countin == 1 ? "" : "s"); break;
    case F_RHPART: part_name(o, n, g_ln.rh_part); break;
    default: part_name(o, n, g_ln.lh_part); break;
    }
}

static void setup_draw(gfx_t *g)
{
    w_status(g, "Play Along", g_ln.title, C_ACC);
    static const char *const names[F_COUNT] = { "Hand", "Mode", "Speed", "Start at", "Count-in", "Right hand part", "Left hand part" };
    int rh = s_fields > 5 ? 19 : 22;
    for (int f = 0; f < s_fields; f++) {
        int y = CONTENT_Y + 3 + f * rh;
        bool sel = f == s_field;
        w_row(g, y, rh - 1, sel);
        gfx_text(g, &font_main, 12, y + (rh - 14) / 2, names[f], sel ? C_ACC : C_TEXT);
        char v[40];
        value_text(f, v, sizeof(v));
        gfx_text_box(g, &font_main, 130, y + (rh - 14) / 2, g->w - 142, ALIGN_RIGHT, v, sel ? C_ACC : C_DIM);
    }
    int y = CONTENT_Y + 3 + s_fields * rh + 4;
    const char *a = g_lset.mode == LN_MODE_WAIT ? "Wait: the song stops until you play the right note."
                                                 : "Play: the song keeps going; every note is scored.";
    gfx_text_box(g, &font_small, 12, y, 296, ALIGN_LEFT, a, C_DIM);
    const char *b = g_lset.hand == LH_BOTH ? "You play both hands." :
                    (g_lset.hand == LH_RIGHT ? (has_left() ? "The box plays the left hand for you." : "The box plays the rest.")
                                             : "The box plays the right hand for you.");
    gfx_text_box(g, &font_small, 12, y + 14, 296, ALIGN_LEFT, b, C_DIM);
    /* fit to your keyboard */
    gfx_fill(g, 12, 194, 296, 16, C_PANEL);
    char line[64], l1[8], l2[8];
    uint16_t c = C_ACC;
    ln_ui_note_name(l1, sizeof(l1), s_lo + s_shift);
    ln_ui_note_name(l2, sizeof(l2), s_hi + s_shift);
    if (!g_kb_known) {
        snprintf(line, sizeof(line), "Range %s" UI_ENDASH "%s " UI_MIDDOT " keyboard not checked", l1, l2);
        c = C_DIM;
    } else if (s_out) {
        snprintf(line, sizeof(line), "%d note%s out of reach: the box plays them", s_out, s_out == 1 ? "" : "s");
        c = C_AMBER;
    } else if (s_shift) {
        snprintf(line, sizeof(line), "Moved %s %d octave%s to fit (%s" UI_ENDASH "%s)", s_shift > 0 ? "up" : "down",
                 (s_shift > 0 ? s_shift : -s_shift) / 12, (s_shift == 12 || s_shift == -12) ? "" : "s", l1, l2);
        c = C_AMBER;
    } else {
        snprintf(line, sizeof(line), "Range %s" UI_ENDASH "%s " UI_MIDDOT " fits your keyboard", l1, l2);
    }
    gfx_text_box(g, &font_small, 18, 197, 240, ALIGN_LEFT, line, c);
    w_stars(g, 266, 202, 6, 3, s_best_stars, C_GOLD, C_LINE);
    static const char *const keys[5] = { "Back", "-", "+", NULL, "Start" };
    w_softkeys(g, keys, false);
}

static void setup_draw2(gfx_t *g)
{
    gfx_clear(g, C_BG);
    char r[40];
    uint32_t secs = (uint32_t)((uint64_t)g_ln.len_ms * 100 / (unsigned)g_lset.speed / 1000);
    snprintf(r, sizeof(r), "%d bars " UI_MIDDOT " %lu:%02lu at %d%%", g_ln.nbars - 1, (unsigned long)(secs / 60),
             (unsigned long)(secs % 60), g_lset.speed);
    w_header2(g, "Notes in this part", r, C_ACC);
    gfx_fill(g, 12, 22, 10, 8, C_ACC);
    gfx_text(g, &font_small, 26, 21, "you play", C_DIM);
    gfx_fill(g, 90, 22, 10, 8, C_BLUE);
    gfx_text(g, &font_small, 104, 21, "the box plays", C_DIM);
    uint8_t lo = 36, hi = 96;
    uint8_t all_lo, all_hi;
    if (ln_song_range(3, &all_lo, &all_hi)) {
        lo = all_lo;
        hi = all_hi;
    }
    if (g_kb_known) {
        /* your keyboard, widened only if some notes fall outside it */
        int a = lo + s_shift, b = hi + s_shift;
        lo = (uint8_t)(a < g_kb_lo ? (a < 0 ? 0 : a) : g_kb_lo);
        hi = (uint8_t)(b > g_kb_hi ? (b > 127 ? 127 : b) : g_kb_hi);
    }
    ln_kb_t k;
    ln_ui_kb_layout(&k, lo, hi, 6, g->w - 12, 40, 70);
    static uint16_t col[128];
    memset(col, 0, sizeof(col));
    unsigned mine = ln_ui_hands();
    for (int i = 0; i < g_ln.n; i++) {
        const ln_note_t *n = &g_ln.notes[i];
        if (n->hand > 1) {
            continue;
        }
        int key = n->note + s_shift;
        if (key >= 0 && key < 128) {
            if (mine & (1u << n->hand)) {
                col[key] = C_ACC;
            } else if (!col[key]) {
                col[key] = C_BLUE;
            }
        }
    }
    ln_ui_kb_draw(g, &k, col, g_kb_known ? g_kb_lo : 0, g_kb_known ? g_kb_hi : 0);
    int x0, w0, x1, w1;
    if (ln_ui_key_x(&k, s_lo + s_shift, &x0, &w0) && ln_ui_key_x(&k, s_hi + s_shift, &x1, &w1)) {
        gfx_fill(g, x0, 116, x1 + w1 - x0, 3, C_ACC);
        char a[8], b[8], t[40];
        ln_ui_note_name(a, sizeof(a), s_lo + s_shift);
        ln_ui_note_name(b, sizeof(b), s_hi + s_shift);
        snprintf(t, sizeof(t), "%s %s" UI_ENDASH "%s", ln_ui_hand_name(g_lset.hand), a, b);
        gfx_text(g, &font_small, x0, 121, t, C_ACC);
    }
}

const screen_t scr_lsetup = {
    .name = "lsetup", .enter = setup_enter, .resume = setup_resume, .key = setup_key,
    .draw = setup_draw, .draw2 = setup_draw2,
};

/* ================================================================== playing */

static bool s_hints;
static uint32_t s_last_draw;
static ln_status_t s_st;
static int s_draw_from;       /* first note that may still be on screen 2 */

static void play_enter(void *arg)
{
    (void)arg;
    ln_cfg_t c = {
        .mode = (ln_mode_t)g_lset.mode,
        .speed = (uint16_t)g_lset.speed,
        .hands = ln_ui_hands(),
        .shift = (int8_t)s_shift,
        .kb_lo = g_kb_known ? g_kb_lo : 0,
        .kb_hi = g_kb_known ? g_kb_hi : 0,
        .countin_bars = (uint8_t)g_lset.countin,
        .start_ms = g_ln.bars[g_lset.start_bar],
    };
    s_draw_from = 0;
    if (!ln_start(&c)) {
        ui_toast("Could not start");
        ui_pop();
        return;
    }
    viz_set_active(false);
}

static void play_leave(void)
{
    ln_stop();
}

static void play_tick(uint32_t now)
{
    ln_get(&s_st);
    if (s_st.state == LE_RUN || s_st.state == LE_COUNTIN) {
        ln_prac_active(now);
    }
    if (s_st.state == LE_DONE) {
        ui_replace(&scr_lresults, NULL);
        return;
    }
    if (now - s_last_draw >= 100) {
        s_last_draw = now;
        ui_invalidate();
    }
}

static bool play_key(const btn_event_t *e)
{
    if (e->repeat) {
        return true;
    }
    switch (e->key) {
    case K_S1:
        ui_pop();
        return true;
    case K_S2:
        ln_seek_bars(-1);
        return true;
    case K_S3:
        ln_pause_toggle();
        return true;
    case K_S4:
        ln_loop_toggle();
        ln_get(&s_st);
        ui_toast(s_st.loop ? "Looping 4 bars" : "Loop off");
        return true;
    case K_S5:
        s_hints = !s_hints;
        ui_toast(s_hints ? "Note names on" : "Note names off");
        return true;
    default:
        return false;
    }
}

static void grade_flash(gfx_t *g, int y)
{
    uint32_t age = ui_now() - s_st.last_ms;
    if (s_st.state == LE_COUNTIN) {
        char t[16];
        snprintf(t, sizeof(t), "Get ready " UI_ENDASH " %d", s_st.countin_left);
        gfx_fill(g, 10, y, 300, 30, C_PANEL);
        gfx_text_box(g, &font_large, 0, y + 6, g->w, ALIGN_CENTER, t, C_AMBER);
        return;
    }
    if (s_st.state == LE_PAUSED) {
        gfx_fill(g, 10, y, 300, 30, C_PANEL);
        gfx_text_box(g, &font_large, 0, y + 6, g->w, ALIGN_CENTER, "Paused", C_DIM);
        return;
    }
    if (s_st.waiting && s_st.nwait) {
        char keys[48] = "", nm[8];
        for (int i = 0; i < s_st.nwait && i < 4; i++) {
            ln_ui_note_name(nm, sizeof(nm), s_st.wait_keys[i]);
            strncat(keys, nm, sizeof(keys) - strlen(keys) - 3);
            if (i + 1 < s_st.nwait && i < 3) {
                strncat(keys, " + ", sizeof(keys) - strlen(keys) - 1);
            }
        }
        char t[64];
        snprintf(t, sizeof(t), "Play  %s", keys);
        gfx_fill(g, 10, y, 300, 30, C_PANEL);
        gfx_text_box(g, &font_large, 0, y + 6, g->w, ALIGN_CENTER, t, C_AMBER);
        return;
    }
    if (s_st.last == LG_NONE || age > 700) {
        return;
    }
    static const char *const txt[5] = { "", "PERFECT!", "GOOD", "MISS", "WRONG KEY" };
    uint16_t c = s_st.last == LG_PERFECT ? C_ACC : (s_st.last == LG_GOOD ? C_BLUE : C_REC);
    gfx_fill(g, 10, y, 300, 30, s_st.last == LG_PERFECT ? C_ACC_BG : C_PANEL);
    gfx_text_box(g, &font_large, 0, y + 6, g->w, ALIGN_CENTER, txt[s_st.last], c);
}

static void play_draw(gfx_t *g)
{
    int64_t pos = s_st.song_us < 0 ? 0 : s_st.song_us;
    int bar = ln_song_bar_at((uint32_t)(pos / 1000));
    char right[96];
    snprintf(right, sizeof(right), "%.40s " UI_MIDDOT " bar %d/%d%s", g_ln.title, bar + 1, g_ln.nbars - 1, s_st.loop ? " " UI_MIDDOT " loop" : "");
    w_status(g, "Play Along", right, C_ACC);
    char sc[16];
    snprintf(sc, sizeof(sc), "%lu", (unsigned long)s_st.score);
    gfx_text(g, &font_small, 10, 26, "SCORE", C_DIM);
    gfx_text(g, &font_huge, 10, 38, sc, C_TEXT);
    char cb[12];
    snprintf(cb, sizeof(cb), "x%u", s_st.combo);
    gfx_text(g, &font_small, 230, 26, "COMBO", C_DIM);
    gfx_text(g, &font_large, 230, 37, cb, C_AMBER);
    grade_flash(g, 78);
    char v[4][12];
    snprintf(v[0], 12, "%u", s_st.perfect);
    snprintf(v[1], 12, "%u", s_st.good);
    snprintf(v[2], 12, "%u", s_st.miss);
    snprintf(v[3], 12, "%d%%", ln_accuracy(&s_st));
    w_stat(g, 10, 116, 70, "Perfect", v[0], C_ACC);
    w_stat(g, 86, 116, 70, "Good", v[1], C_BLUE);
    w_stat(g, 162, 116, 70, "Miss", v[2], C_REC);
    w_stat(g, 238, 116, 72, "Accuracy", v[3], C_TEXT);
    gfx_text(g, &font_small, 10, 156, "Progress", C_DIM);
    w_progress(g, 10, 168, 300, 6, (uint32_t)(pos / 1000), g_ln.len_ms, C_ACC);
    char info[64];
    snprintf(info, sizeof(info), "Speed %d%% " UI_MIDDOT " %s " UI_MIDDOT " %s", g_lset.speed, ln_ui_hand_name(g_lset.hand),
             g_lset.mode == LN_MODE_WAIT ? "Wait" : "Play (scored)");
    gfx_text(g, &font_small, 10, 180, info, C_DIM);
    gfx_text(g, &font_small, 10, 196, "On track for", C_DIM);
    w_stars(g, 262, 201, 6, 3, ln_stars(&s_st), C_GOLD, C_LINE);
    static const char *const keys[5] = { "Stop", "@rew", "@playpause", "Loop", "Hints" };
    w_softkeys(g, keys, false);
}

/* screen 2: the falling notes */
static void play_draw2(gfx_t *g)
{
    gfx_clear(g, C_BG);
    ln_status_t st;
    ln_get(&st);
    const int KY = 108, HIT = KY - 3, TOP = 12;
    int shift = s_shift;
    uint8_t lo, hi;
    if (g_kb_known) {
        lo = g_kb_lo;
        hi = g_kb_hi;
    } else if (!ln_song_range(3, &lo, &hi)) {
        lo = 48;
        hi = 84;
    }
    ln_kb_t k;
    ln_ui_kb_layout(&k, lo, hi, 4, g->w - 8, KY, g->h - KY);
    /* octave lines */
    for (int n = k.lo; n <= k.hi; n += 12) {
        int x, w;
        if (ln_ui_key_x(&k, n, &x, &w)) {
            gfx_vline(g, x, 0, HIT, RGB(0x1A, 0x20, 0x29));
        }
    }
    int64_t song = st.song_us;
    int64_t window = (int64_t)2500000 * g_lset.speed / 100;
    int h = HIT - TOP;
    static uint16_t col[128];
    memset(col, 0, sizeof(col));
    uint16_t box_col = RGB(0x20, 0x46, 0x6E), box_lit = RGB(0x2F, 0x6C, 0xA8);
    /* first note that can still be on screen: notes are at most 60 s long */
    while (s_draw_from > 0 && (int64_t)g_ln.notes[s_draw_from].t * 1000 > song - 2000000) {
        s_draw_from--;
    }
    while (s_draw_from < g_ln.n && (int64_t)(g_ln.notes[s_draw_from].t + g_ln.notes[s_draw_from].len) * 1000 < song - 4000000) {
        s_draw_from++;
    }
    int hints = 0;
    for (int i = s_draw_from; i < g_ln.n; i++) {
        const ln_note_t *n = &g_ln.notes[i];
        int64_t t = (int64_t)n->t * 1000, e = t + (int64_t)n->len * 1000;
        if (t > song + window) {
            break;
        }
        if (n->hand == LN_BACKING || e < song) {
            continue;
        }
        int key = n->note + shift, x, w;
        bool mine = g_ln_user && g_ln_user[i];
        if (t <= song && e > song && !mine && key >= 0 && key < 128) {
            col[key] = box_lit;   /* the box is playing it now */
        }
        if (!ln_ui_key_x(&k, key, &x, &w)) {
            continue;
        }
        int yb = HIT - (int)((t - song) * h / window);
        int yt = HIT - (int)((e - song) * h / window);
        if (yb > HIT) {
            yb = HIT;
        }
        if (yt < TOP) {
            yt = TOP;
        }
        if (yb - yt < 3) {
            yt = yb - 3;
        }
        uint16_t c = box_col;
        if (mine) {
            uint8_t j = g_ln_judged ? g_ln_judged[i] : 0;
            c = j == LJ_PERFECT ? gfx_blend(0xFFFF, C_ACC, 90) : (j == LJ_GOOD ? C_BLUE : (j == LJ_MISS ? RGB(0x7A, 0x2F, 0x33) : C_ACC));
        }
        /* a gap at the end of every note and a bright leading edge, so the same key twice
         * in a row shows as two bars, not one long one */
        if (yb - yt > 6) {
            yt += 2;
        }
        gfx_fill(g, x, yt, w, yb - yt, c);
        if (yb - yt >= 5) {
            gfx_fill(g, x, yb - 2, w, 2, gfx_blend(0xFFFF, c, 170));
        }
        if (mine && s_hints && !g_ln_judged[i] && hints < 6 && t >= song) {
            char nm[8];
            ln_ui_note_name(nm, sizeof(nm), key);
            gfx_text(g, &font_small, x + w / 2 - gfx_text_width(&font_small, nm) / 2, yt - 11 > TOP ? yt - 11 : TOP, nm, C_TEXT);
            hints++;
        }
    }
    gfx_hline(g, 0, HIT, g->w, st.waiting ? C_AMBER : C_GREY);
    for (int n = 0; n < 128; n++) {
        if (g_ln_pressed[n >> 3] & (1u << (n & 7))) {
            col[n] = C_ACC;
        }
    }
    for (int i = 0; st.waiting && i < st.nwait; i++) {
        col[st.wait_keys[i]] = C_AMBER;
    }
    ln_ui_kb_draw(g, &k, col, 0, 0);
    /* grade popping up over the key */
    uint32_t age = ui_now() - st.last_ms;
    if (st.last != LG_NONE && age < 400 && st.last != LG_WRONG) {
        int x, w;
        if (ln_ui_key_x(&k, st.last_key, &x, &w)) {
            static const char *const txt[5] = { "", "PERFECT", "GOOD", "MISS", "" };
            uint16_t c = st.last == LG_PERFECT ? C_ACC : (st.last == LG_GOOD ? C_BLUE : C_REC);
            int tw = gfx_text_width(&font_main, txt[st.last]);
            int tx = x + w / 2 - tw / 2;
            tx = tx < 2 ? 2 : (tx + tw > g->w - 2 ? g->w - 2 - tw : tx);
            gfx_text(g, &font_main, tx, HIT - 22 - (int)age / 40, txt[st.last], c);
        }
    }
    char sc[16], cb[12];
    snprintf(sc, sizeof(sc), "%lu", (unsigned long)st.score);
    snprintf(cb, sizeof(cb), "x%u", st.combo);
    gfx_text(g, &font_small, 4, 1, sc, C_TEXT);
    gfx_text_right(g, &font_small, g->w - 4, 1, cb, C_AMBER);
    if (st.state == LE_COUNTIN) {
        char t[4];
        snprintf(t, sizeof(t), "%d", st.countin_left);
        gfx_text_box(g, &font_huge, 0, 40, g->w, ALIGN_CENTER, t, C_AMBER);
    }
}

const screen_t scr_lplay = {
    .name = "lplay", .enter = play_enter, .leave = play_leave, .key = play_key, .tick = play_tick,
    .draw = play_draw, .draw2 = play_draw2,
};

/* ================================================================== results */

static ln_status_t s_res;
static bool s_new_best;
static int s_res_stars;

static void res_enter(void *arg)
{
    (void)arg;
    ln_get(&s_res);
    s_res_stars = ln_stars(&s_res);
    char k[240];
    ln_ui_score_key(k, sizeof(k));
    s_new_best = ln_best_put(k, s_res.score, s_res_stars);
    ln_prac_combo(s_res.best_combo);
    ln_prac_flush();
}

static void retry(int speed_delta)
{
    if (g_lres_ops) {
        g_lres_ops->retry(speed_delta);
        return;
    }
    int v = g_lset.speed + speed_delta;
    g_lset.speed = v > 150 ? 150 : v;
    ui_replace(&scr_lplay, NULL);
}

static bool res_key(const btn_event_t *e)
{
    if (e->repeat) {
        return true;
    }
    switch (e->key) {
    case K_S1:
        if (g_lres_ops) {
            g_lres_ops->done();
            return true;
        }
        ui_pop();   /* to Setup */
        ui_pop();   /* to the song list */
        return true;
    case K_S2:
        retry(0);
        return true;
    case K_S4:
        if (g_lres_ops ? g_lres_ops->can_faster() : g_lset.speed < 150) {
            retry(10);
        }
        return true;
    case K_S5:
        if (g_lres_ops) {
            g_lres_ops->next();
        } else if (g_ln.builtin && g_lset.builtin + 1 < ln_builtin_count && ln_song_builtin(g_lset.builtin + 1)) {
            g_lset.builtin++;
            g_lset.start_bar = 0;
            ui_pop();   /* Setup, now for the next song */
        }
        return true;
    default:
        return false;
    }
}

static void res_draw(gfx_t *g)
{
    char r[64];
    snprintf(r, sizeof(r), "%s " UI_MIDDOT " %s", g_ln.title, ln_ui_hand_name(g_lset.hand));
    w_status(g, "Results", r, C_ACC);
    w_stars(g, 104, 44, 16, 3, s_res_stars, C_GOLD, C_LINE);
    char sc[16];
    snprintf(sc, sizeof(sc), "%lu", (unsigned long)s_res.score);
    gfx_text_box(g, &font_huge, 0, 66, g->w, ALIGN_CENTER, sc, C_TEXT);
    if (s_new_best && s_res.score) {
        gfx_fill(g, 110, 100, 100, 16, C_GOLD);
        gfx_text_box(g, &font_main, 110, 102, 100, ALIGN_CENTER, "NEW BEST!", C_BG);
    }
    char v[4][12];
    snprintf(v[0], 12, "%u", s_res.perfect);
    snprintf(v[1], 12, "%u", s_res.good);
    snprintf(v[2], 12, "%u", s_res.miss);
    snprintf(v[3], 12, "%d%%", ln_accuracy(&s_res));
    w_stat(g, 10, 124, 70, "Perfect", v[0], C_ACC);
    w_stat(g, 86, 124, 70, "Good", v[1], C_BLUE);
    w_stat(g, 162, 124, 70, "Miss", v[2], C_REC);
    w_stat(g, 238, 124, 72, "Accuracy", v[3], C_TEXT);
    char bc[12];
    snprintf(bc, sizeof(bc), "%u", s_res.best_combo);
    w_stat(g, 10, 160, 146, "Best combo", bc, C_TEXT);
    const char *timing = "-";
    if (g_lset.mode == LN_MODE_WAIT) {
        timing = s_res.wrong ? "check the wrong keys" : "no wrong keys";
    } else if (s_res.dt_n) {
        int avg = s_res.dt_sum / s_res.dt_n;
        timing = avg < -25 ? "a little early" : (avg > 25 ? "a little late" : "right on time");
    }
    w_stat(g, 162, 160, 148, g_lset.mode == LN_MODE_WAIT ? "Wrong keys" : "Timing", timing, C_TEXT);
    bool next = g_lres_ops ? g_lres_ops->has_next() : g_ln.builtin && g_lset.builtin + 1 < ln_builtin_count;
    bool faster = g_lres_ops ? g_lres_ops->can_faster() : g_lset.speed < 150;
    const char *keys[5] = { "Done", "Retry", NULL, faster ? "Faster" : NULL, next ? "Next" : NULL };
    w_softkeys(g, keys, false);
}

static void res_draw2(gfx_t *g)
{
    gfx_clear(g, C_BG);
    const uint16_t conf[5] = { C_ACC, C_BLUE, C_GOLD, C_AMBER, C_REC };
    if (s_res_stars >= 2) {
        uint32_t seed = 7 + ui_frame2() / 6;   /* confetti drifts down */
        for (int i = 0; i < 60; i++) {
            seed = seed * 1103515245u + 12345u;
            int x = (int)((seed >> 8) % (uint32_t)g->w);
            int y = (int)(((seed >> 20) % 50 + ui_frame2() / 3 + (uint32_t)i * 7) % 60);
            gfx_fill(g, x, y, 2 + (int)(seed & 1), 2 + (int)((seed >> 3) & 2), conf[i % 5]);
        }
    }
    static const char *const head[4] = { "Keep practising", "1 star", "2 stars!", "3 stars!" };
    gfx_text_box(g, &font_huge, 0, 30, g->w, ALIGN_CENTER, head[s_res_stars], s_res_stars ? C_GOLD : C_DIM);
    if (g_lset.mode == LN_MODE_PLAY && s_res.dt_n) {
        gfx_text(g, &font_small, 20, 84, "TIMING", C_DIM);
        int mx = 1;
        for (int i = 0; i < LN_HIST_BINS; i++) {
            mx = s_res.hist[i] > mx ? s_res.hist[i] : mx;
        }
        for (int i = 0; i < LN_HIST_BINS; i++) {
            int v = s_res.hist[i] * 40 / mx;
            gfx_fill(g, 80 + i * 20, 136 - v, 14, v, (i >= 5 && i <= 8) ? C_ACC : C_BLUE);
        }
        gfx_vline(g, 80 + 7 * 20 - 3, 96, 42, C_TEXT);
        gfx_text(g, &font_small, 20, 124, "early", C_DIM);
        gfx_text_right(g, &font_small, g->w - 20, 124, "late", C_DIM);
    } else {
        char t[64];
        snprintf(t, sizeof(t), "%u of %u notes right first time", s_res.perfect, s_res.user_total);
        gfx_text_box(g, &font_main, 0, 100, g->w, ALIGN_CENTER, t, C_TEXT);
    }
}

const screen_t scr_lresults = {
    .name = "lresults", .enter = res_enter, .key = res_key, .draw = res_draw, .draw2 = res_draw2,
};
