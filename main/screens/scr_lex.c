/*
 * Learn > Exercises: pick a scale, chord or arpeggio, then play it to the metronome.
 *
 * Picker:  2/3 category, 5/6 key, 7 hand, 8 start, 4 back.
 *          Shift+5/6 tempo -/+ 5, Shift+7 one or two octaves.
 * Playing: 4 stop, 5 slower, 6 faster (both start again), 7 metronome on/off, 8 restart.
 * Then the shared Results screen: 4 done, 5 retry, 7 faster (+10 bpm), 8 next key.
 * Screen 2 shows the notes on the grand staff with the suggested fingering.
 */
#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "theme.h"
#include "widgets.h"
#include "viz.h"
#include "ln_song.h"
#include "ln_store.h"
#include "ln_engine.h"
#include "ln_ex.h"
#include "ln_ui.h"
#include "app.h"

#define C_FUTURE RGB(0x6B, 0x75, 0x84)

static ln_ex_t s_x = { .cat = LX_MAJOR, .idx = 0, .hand = LH_RIGHT, .bpm = 80, .octaves = 2 };
static int s_idx[LX_NCAT];
static int8_t s_stars[LX_NCAT][24];
static bool s_built;              /* g_ln holds the selected exercise */
static ln_setup_t s_saved_lset;    /* Play Along's settings, put back on the way out */
static int s_shift, s_out;
static bool s_click = true;

static void load_stars(void)
{
    static const char *const pre[LX_NCAT] = { "x:maj:", "x:min:", "x:chd:", "x:arp:" };
    for (int c = 0; c < LX_NCAT; c++) {
        ln_best_stars_list(pre[c], s_stars[c], 24);
    }
}

static int done_in(int c)
{
    int n = 0;
    for (int i = 0; i < ln_ex_count(c); i++) {
        n += s_stars[c][i] > 0;
    }
    return n;
}

static void rebuild(void)
{
    s_x.idx = s_idx[s_x.cat];
    s_built = ln_ex_build(&s_x);
    g_lset.hand = s_x.hand;
    g_lset.mode = LN_MODE_PLAY;
    g_lset.speed = 100;
    g_lset.start_bar = 0;
    uint8_t lo, hi;
    s_shift = s_built ? ln_ui_fit(&s_out, &lo, &hi) : 0;
}

/* ------------------------------------------------------------------ results hooks */

static void ops_retry(int faster)
{
    if (faster > 0 && s_x.bpm < 160) {
        s_x.bpm += faster;
    }
    rebuild();
    ui_replace(&scr_lexplay, NULL);
}

static bool ops_has_next(void) { return s_idx[s_x.cat] + 1 < ln_ex_count(s_x.cat); }
static bool ops_can_faster(void) { return s_x.bpm < 160; }

static bool ops_next(void)
{
    if (!ops_has_next()) {
        return false;
    }
    s_idx[s_x.cat]++;
    rebuild();
    ui_replace(&scr_lexplay, NULL);
    return true;
}

static void ops_done(void) { ui_pop(); }   /* back to the picker */

static const ln_res_ops_t s_ops = { ops_retry, ops_next, ops_has_next, ops_can_faster, ops_done };

/* ================================================================== picker */

static void pick_enter(void *arg)
{
    (void)arg;
    s_saved_lset = g_lset;
    g_lres_ops = &s_ops;
    ln_ui_load_keyboard();
    load_stars();
    rebuild();
}

static void pick_resume(void)
{
    g_lres_ops = &s_ops;
    load_stars();
    rebuild();
    ui_invalidate();
}

static void pick_leave(void)
{
    g_lres_ops = NULL;
    g_lset = s_saved_lset;
    ln_song_free();
}

static bool pick_key(const btn_event_t *e)
{
    int n = ln_ex_count(s_x.cat);
    switch (e->key) {
    case K_UP:
        s_x.cat = (s_x.cat + LX_NCAT - 1) % LX_NCAT;
        break;
    case K_DOWN:
        s_x.cat = (s_x.cat + 1) % LX_NCAT;
        break;
    case K_S1:
        if (!e->repeat) {
            ui_pop();
        }
        return true;
    case K_S2:
        if (e->shift) {
            s_x.bpm = s_x.bpm > 40 ? s_x.bpm - 5 : 40;
        } else {
            s_idx[s_x.cat] = (s_idx[s_x.cat] + n - 1) % n;
        }
        break;
    case K_S3:
        if (e->shift) {
            s_x.bpm = s_x.bpm < 160 ? s_x.bpm + 5 : 160;
        } else {
            s_idx[s_x.cat] = (s_idx[s_x.cat] + 1) % n;
        }
        break;
    case K_S4:
        if (e->repeat) {
            return true;
        }
        if (e->shift) {
            s_x.octaves = s_x.octaves == 1 ? 2 : 1;
        } else {
            s_x.hand = (s_x.hand + 1) % 3;
        }
        break;
    case K_S5:
        if (!e->repeat) {
            if (s_built) {
                ui_push(&scr_lexplay, NULL);
            } else {
                ui_toast("Not enough memory");
            }
        }
        return true;
    default:
        return false;
    }
    rebuild();
    ui_invalidate();
    return true;
}

static void pick_draw(gfx_t *g)
{
    int total = 0, done = 0;
    for (int c = 0; c < LX_NCAT; c++) {
        total += ln_ex_count(c);
        done += done_in(c);
    }
    char r[32];
    snprintf(r, sizeof(r), "%d of %d done", done, total);
    w_status(g, "Exercises", r, C_ACC);
    for (int c = 0; c < LX_NCAT; c++) {
        int y = CONTENT_Y + 4 + c * 26;
        bool sel = c == s_x.cat;
        w_row(g, y, 25, sel);
        gfx_text(g, &font_main, 12, y + 6, ln_ex_cat_name(c), sel ? C_ACC : C_TEXT);
        int d = done_in(c), n = ln_ex_count(c);
        w_progress(g, 170, y + 10, 90, 5, (uint32_t)d, (uint32_t)n, C_ACC);
        char t[12];
        snprintf(t, sizeof(t), "%d/%d", d, n);
        gfx_text_right(g, &font_small, g->w - 10, y + 7, t, C_DIM);
    }
    gfx_hline(g, 0, 130, g->w, C_LINE);
    gfx_text(g, &font_small, 12, 136, s_x.cat >= LX_CHORDS ? "CHORD" : "KEY", C_DIM);
    gfx_text(g, &font_small, 150, 136, "HAND", C_DIM);
    gfx_text(g, &font_small, 230, 136, "TEMPO", C_DIM);
    char name[40];
    ln_ex_name(s_x.cat, s_x.idx, name, sizeof(name));
    char *sp = strchr(name, ' ');
    if (sp && sp[1] && strchr(sp + 1, ' ')) {
        *strchr(sp + 1, ' ') = 0;   /* "Eb major" without "chord" */
    }
    gfx_text_box(g, &font_large, 12, 150, 134, ALIGN_LEFT, name, C_TEXT);
    static const char *const hn[3] = { "Right", "Left", "Both" };
    gfx_text(g, &font_large, 150, 150, hn[s_x.hand], C_TEXT);
    char bpm[16];
    snprintf(bpm, sizeof(bpm), "%d bpm", s_x.bpm);
    gfx_text(g, &font_large, 230, 150, bpm, C_TEXT);
    char desc[64];
    ln_ex_desc(&s_x, desc, sizeof(desc));
    int w = gfx_text(g, &font_small, 12, 176, desc, C_DIM);
    int st = s_stars[s_x.cat][s_x.idx];
    gfx_text(g, &font_small, 12 + w + 8, 176, "best", C_DIM);
    w_stars(g, 12 + w + 40, 181, 6, 3, st, C_GOLD, C_LINE);
    /* a nudge for what to try next */
    char hint[80];
    if (s_out) {
        snprintf(hint, sizeof(hint), "%d notes are beyond your keyboard: the box plays them", s_out);
    } else if (st >= 3) {
        snprintf(hint, sizeof(hint), "3 stars! Next: try it at %d bpm", s_x.bpm + 10 > 160 ? 160 : s_x.bpm + 10);
    } else if (st > 0 && s_x.hand != LH_BOTH) {
        snprintf(hint, sizeof(hint), "Next: try it with both hands");
    } else {
        snprintf(hint, sizeof(hint), "One note per click. Steady beats fast.");
    }
    gfx_text_box(g, &font_small, 12, 196, 296, ALIGN_LEFT, hint, C_AMBER);
    static const char *const normal[5] = { "Back", "Key -", "Key +", "Hand", "Start" };
    static const char *const shifted[5] = { "Back", "Tempo -", "Tempo +", "Octaves", "Start" };
    w_softkeys2(g, normal, shifted);
}

/* screen 2: the notes and fingering. now_ms < 0: a still preview of the way up. */
static void staff_view(gfx_t *g, int64_t now_ms, bool fingers)
{
    gfx_clear(g, C_BG);
    bool rh = s_x.hand != LH_LEFT, lh = s_x.hand != LH_RIGHT;
    ln_ui_staff(g, 4, g->w - 6, rh ? LN_STAFF_LINE : C_LINE, lh ? LN_STAFF_LINE : C_LINE);
    if (!s_built || !g_ln.n) {
        return;
    }
    uint32_t beat = g_ln.beat_ms ? g_ln.beat_ms : 750;
    /* the way up: up to the highest note */
    uint32_t t_top = 0;
    int hi = -1;
    for (int i = 0; i < g_ln.n; i++) {
        if (g_ln.notes[i].note > hi) {
            hi = g_ln.notes[i].note;
            t_top = g_ln.notes[i].t;
        }
    }
    int steps = (int)(t_top / beat) + 1;
    int span = steps > 1 ? (g->w - 80) / steps : 40;
    if (span > 40) {
        span = 40;
    }
    int next = -1;
    for (int i = 0; i < g_ln.n; i++) {
        const ln_note_t *n = &g_ln.notes[i];
        int x;
        if (now_ms < 0) {
            if (n->t > t_top) {
                continue;
            }
            x = 64 + (int)(n->t / beat) * span;
        } else {
            x = 120 + (int)(((int64_t)n->t - now_ms) * 32 / (int64_t)beat);
        }
        if (x < 52 || x > g->w - 10) {
            continue;
        }
        uint16_t c = C_TEXT;
        if (now_ms >= 0) {
            uint8_t j = g_ln_judged ? g_ln_judged[i] : 0;
            bool mine = g_ln_user && g_ln_user[i];
            c = !mine ? C_GREY : (j == LJ_PERFECT ? C_ACC : (j == LJ_GOOD ? C_BLUE : (j == LJ_MISS ? C_REC : C_FUTURE)));
            if (mine && !j && next < 0) {
                next = i;
            }
            if (next >= 0 && g_ln.notes[next].t == n->t && mine && !j) {
                c = C_AMBER;
            }
        }
        ln_ui_staff_note(g, x, n->note, n->acc, c, false);
        int f = ln_ex_finger(&s_x, i);
        if (fingers && f) {
            char d[2] = { (char)('0' + f), 0 };
            /* next to the note: above for the right hand, below for the left; beside a chord's notes */
            bool chord = (i > 0 && g_ln.notes[i - 1].t == n->t && g_ln.notes[i - 1].hand == n->hand) ||
                         (i + 1 < g_ln.n && g_ln.notes[i + 1].t == n->t && g_ln.notes[i + 1].hand == n->hand);
            int ny = ln_ui_staff_y(ln_ui_staff_pos(n->note, n->acc));
            int y = n->hand == LN_RH ? ny - 18 : ny + 6;
            if (y < 0) {
                y = ny + 6;
            } else if (y > g->h - 11) {
                y = ny - 18;
            }
            if (chord) {
                gfx_text(g, &font_small, x + 8, ny - 6, d, C_AMBER);
                continue;
            }
            gfx_text(g, &font_small, x - 3, y, d, C_AMBER);
        }
    }
    if (now_ms >= 0) {
        gfx_vline(g, 120, 10, 122, C_AMBER);
        if (next >= 0) {
            char nm[8], t[24];
            ln_ui_note_name(nm, sizeof(nm), g_ln.notes[next].note);
            snprintf(t, sizeof(t), "next: %s", nm);
            gfx_fill(g, 4, 0, 46, 12, C_BG);
            gfx_text(g, &font_small, 6, 0, t, C_AMBER);
        }
    }
}

static void pick_draw2(gfx_t *g)
{
    staff_view(g, -1, true);
    if (s_x.hand != LH_BOTH) {
        char name[40], t[80];
        ln_ex_name(s_x.cat, s_x.idx, name, sizeof(name));
        snprintf(t, sizeof(t), "%s " UI_MIDDOT " %s fingering", name, s_x.hand == LH_RIGHT ? "right hand" : "left hand");
        gfx_text_right(g, &font_small, g->w - 6, g->h - 12, t, C_DIM);
    }
}

const screen_t scr_lex = {
    .name = "lex", .enter = pick_enter, .resume = pick_resume, .leave = pick_leave, .key = pick_key,
    .draw = pick_draw, .draw2 = pick_draw2,
};

/* ================================================================== playing */

static ln_status_t s_st;
static uint32_t s_last_draw;
static int s_smooth;           /* steadiness needle: your recent timing, ms */
static uint16_t s_seen_n;

static void start(void)
{
    ln_cfg_t c = {
        .mode = LN_MODE_PLAY,
        .speed = 100,
        .hands = ln_ui_hands(),
        .shift = (int8_t)s_shift,
        .kb_lo = g_kb_known ? g_kb_lo : 0,
        .kb_hi = g_kb_known ? g_kb_hi : 0,
        .countin_bars = 1,
        .metronome = s_click,
    };
    s_smooth = 0;
    s_seen_n = 0;
    if (!ln_start(&c)) {
        ui_toast("Could not start");
        ui_pop();
        return;
    }
    viz_set_active(false);
}

static void play_enter(void *arg)
{
    (void)arg;
    start();
}

static void play_leave(void) { ln_stop(); }

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
    if (s_st.dt_n != s_seen_n) {
        s_seen_n = s_st.dt_n;
        s_smooth = (s_smooth * 2 + s_st.last_dt) / 3;
    }
    if (now - s_last_draw >= 100) {
        s_last_draw = now;
        ui_invalidate();
    }
}

static void tempo(int d)
{
    int b = s_x.bpm + d;
    if (b < 40 || b > 160) {
        return;
    }
    s_x.bpm = b;
    ln_stop();
    rebuild();
    start();
}

static bool play_key(const btn_event_t *e)
{
    if (e->repeat) {
        return true;
    }
    switch (e->key) {
    case K_S1: ui_pop(); return true;
    case K_S2: tempo(-5); return true;
    case K_S3: tempo(5); return true;
    case K_S4:
        s_click = !s_click;
        ln_set_metronome(s_click);
        ui_toast(s_click ? "Metronome on" : "Metronome off");
        return true;
    case K_S5:
        ln_stop();
        start();
        return true;
    default:
        return false;
    }
}

static void play_draw(gfx_t *g)
{
    char right[40];
    int64_t song = s_st.song_us < 0 ? 0 : s_st.song_us;
    uint32_t beat = g_ln.beat_ms ? g_ln.beat_ms : 750;
    int done = s_st.perfect + s_st.good + s_st.miss;
    snprintf(right, sizeof(right), "%d of %u notes", done, s_st.user_total);
    w_status(g, g_ln.title, right, C_ACC);
    char sc[16];
    snprintf(sc, sizeof(sc), "%lu", (unsigned long)s_st.score);
    gfx_text(g, &font_small, 10, 26, "SCORE", C_DIM);
    gfx_text(g, &font_huge, 10, 38, sc, C_TEXT);
    char cb[12];
    snprintf(cb, sizeof(cb), "x%u", s_st.combo);
    gfx_text(g, &font_small, 230, 26, "COMBO", C_DIM);
    gfx_text(g, &font_large, 230, 37, cb, C_AMBER);

    gfx_text(g, &font_small, 10, 82, "STEADINESS", C_DIM);
    gfx_fill(g, 10, 96, 300, 14, C_PANEL);
    gfx_fill(g, 130, 96, 60, 14, C_ACC_BG);   /* within 60 ms */
    gfx_vline(g, 160, 92, 22, C_TEXT);
    const char *verdict = "play on the click";
    uint16_t vc = C_DIM;
    if (s_st.state == LE_COUNTIN) {
        static char t[24];
        snprintf(t, sizeof(t), "get ready " UI_ENDASH " %d", s_st.countin_left);
        verdict = t;
        vc = C_AMBER;
    } else if (s_st.state == LE_PAUSED) {
        verdict = "paused";
    } else if (s_st.dt_n) {
        int x = 160 + s_smooth * 150 / 210;
        x = x < 13 ? 13 : (x > 307 ? 307 : x);
        gfx_fill(g, x - 3, 99, 6, 8, s_smooth < -60 || s_smooth > 60 ? C_AMBER : C_ACC);
        verdict = s_smooth < -40 ? "a little early" : (s_smooth > 40 ? "a little late" : "right on the beat");
        vc = s_smooth < -40 || s_smooth > 40 ? C_AMBER : C_ACC;
    }
    gfx_text(g, &font_small, 10, 114, "early", C_DIM);
    gfx_text_right(g, &font_small, 310, 114, "late", C_DIM);
    gfx_text_box(g, &font_small, 0, 114, g->w, ALIGN_CENTER, verdict, vc);

    char v[3][16];
    snprintf(v[0], 16, "%u / %d", s_st.perfect + s_st.good, done);
    snprintf(v[1], 16, "%d bpm", s_x.bpm);
    if (s_st.dt_n > 2) {
        snprintf(v[2], 16, "%d%%", ln_evenness(&s_st));
    } else {
        snprintf(v[2], 16, "-");
    }
    w_stat(g, 10, 136, 96, "Right notes", v[0], s_st.miss ? C_TEXT : C_ACC);
    w_stat(g, 112, 136, 96, "Tempo", v[1], C_TEXT);
    w_stat(g, 214, 136, 96, "Evenness", v[2], C_TEXT);

    int b = (int)((song / 1000) / beat) % 4;
    char bt[16];
    snprintf(bt, sizeof(bt), "Beat %d of 4", b + 1);
    gfx_text(g, &font_small, 10, 176, bt, C_DIM);
    for (int i = 0; i < 4; i++) {
        gfx_fill(g, 90 + i * 22, 176, 16, 10, i == b && s_st.state == LE_RUN ? C_ACC : C_LINE);
    }
    gfx_text_right(g, &font_small, 310, 176, s_click ? "click on" : "click off", C_DIM);
    static const char *const keys[5] = { "Stop", "Slower", "Faster", "Click", "Restart" };
    w_softkeys(g, keys, false);
}

static void play_draw2(gfx_t *g)
{
    int64_t ms = ln_song_us() / 1000;
    staff_view(g, ms < 0 ? 0 : ms, true);
}

const screen_t scr_lexplay = {
    .name = "lexplay", .enter = play_enter, .leave = play_leave, .tick = play_tick, .key = play_key,
    .draw = play_draw, .draw2 = play_draw2,
};
