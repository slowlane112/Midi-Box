/*
 * Learn: the menu, and the one-time keyboard check (press your lowest, then highest key).
 *
 * Menu: 2/3 choose, 8 open, 7 keyboard check, 4 back.
 */
#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "midi_in.h"
#include "theme.h"
#include "widgets.h"
#include "icons.h"
#include "ln_song.h"
#include "ln_store.h"
#include "ln_builtin.h"
#include "ln_ui.h"
#include "app.h"

/* ------------------------------------------------------------------ menu */

static int s_sel;
static int s_stars;
static int s_sec[4];       /* stars per section */
static int s_ex_done;      /* exercises with at least one star */
static int s_ear_max;      /* highest Ear Training level unlocked */

static void load_stats(void)
{
    s_stars = ln_total_stars();
    s_sec[0] = ln_total_stars_prefix("b:");
    s_sec[1] = ln_total_stars_prefix("x:");
    s_sec[2] = ln_total_stars_prefix("nr:");
    s_sec[3] = ln_total_stars_prefix("ear:");
    s_ear_max = ln_ear_max_load();
    static const char *const pre[4] = { "x:maj:", "x:min:", "x:chd:", "x:arp:" };
    int8_t st[24];
    s_ex_done = 0;
    for (int c = 0; c < 4; c++) {
        ln_best_stars_list(pre[c], st, 24);
        for (int i = 0; i < 24; i++) {
            s_ex_done += st[i] > 0;
        }
    }
}
static const char *const s_items[4] = { "Play Along", "Exercises", "Note Reading", "Ear Training" };
static const char *const s_desc[4] = { "Songs with falling notes", "Scales, chords, arpeggios", "Read the staff, fast",
                                       "Hear it, play it back" };

static void menu_enter(void *arg)
{
    (void)arg;
    if (!midi_in_running()) {
        midi_in_start(g_settings.din_port);
    }
    ln_ui_load_keyboard();
    load_stats();
    if (!g_kb_known) {
        ui_push(&scr_lrange, NULL);   /* first time: find out the keyboard */
    }
}

static void menu_leave(void)
{
    ln_prac_end();   /* back to Home: this practice session is over */
}

static void menu_resume(void)
{
    load_stats();
    ui_invalidate();
}

static bool menu_key(const btn_event_t *e)
{
    switch (e->key) {
    case K_UP: s_sel = (s_sel + 3) % 4; return true;
    case K_DOWN: s_sel = (s_sel + 1) % 4; return true;
    case K_S1:
        if (!e->repeat) {
            ui_pop();
        }
        return true;
    case K_S4:
        if (!e->repeat) {
            ui_push(&scr_lrange, NULL);
        }
        return true;
    case K_S5:
        if (e->repeat) {
            return true;
        }
        if (s_sel == 0) {
            ui_push(&scr_lsongs, NULL);
        } else if (s_sel == 1) {
            ui_push(&scr_lex, NULL);
        } else if (s_sel == 2) {
            ui_push(&scr_lnr, NULL);
        } else {
            ui_push(&scr_lear, NULL);
        }
        return true;
    default:
        return false;
    }
}

static void kb_text(char *out, int n)
{
    if (!g_kb_known) {
        snprintf(out, (size_t)n, "keyboard not checked");
        return;
    }
    char a[8], b[8];
    ln_ui_note_name(a, sizeof(a), g_kb_lo);
    ln_ui_note_name(b, sizeof(b), g_kb_hi);
    snprintf(out, (size_t)n, "%d keys " UI_MIDDOT " %s" UI_ENDASH "%s", g_kb_hi - g_kb_lo + 1, a, b);
}

static void menu_draw(gfx_t *g)
{
    char right[40];
    kb_text(right, sizeof(right));
    w_status(g, "Learn", right, C_ACC);
    for (int i = 0; i < 4; i++) {
        int y = CONTENT_Y + 4 + i * 48;
        bool sel = i == s_sel, soon = false;
        w_row(g, y, 46, sel);
        gfx_text(g, &font_large, 12, y + 5, s_items[i], sel ? C_ACC : (soon ? C_DIM : C_TEXT));
        gfx_text(g, &font_small, 12, y + 27, s_desc[i], C_DIM);
        char st[32];
        int stars = s_sec[i];
        if (i == 0) {
            snprintf(st, sizeof(st), "%d songs " UI_MIDDOT " %d stars", ln_builtin_count, stars);
        } else if (i == 1) {
            snprintf(st, sizeof(st), "%d of 72 done", s_ex_done);
        } else if (i == 2) {
            snprintf(st, sizeof(st), "%d stars", stars);
        } else {
            snprintf(st, sizeof(st), "level %d of 6", s_ear_max + 1);
        }
        gfx_text_right(g, &font_small, g->w - 10, y + 10, st, s_sec[i] ? C_GOLD : C_DIM);
    }
    static const char *const keys[5] = { "Back", NULL, NULL, "Keyboard", "Open" };
    w_softkeys(g, keys, false);
}

static void menu_draw2(gfx_t *g)
{
    gfx_clear(g, C_BG);
    gfx_text(g, &font_huge, 10, 8, s_items[s_sel], C_ACC);
    static const char *const more[4][2] = {
        { "Notes fall onto the keys:", "play them as they land" },
        { "Scales, chords and arpeggios in", "every key, played to the click" },
        { "A note on the staff:", "find the key, fast" },
        { "The box plays it,", "you play it back" },
    };
    gfx_text(g, &font_small, 12, 44, more[s_sel][0], C_DIM);
    gfx_text(g, &font_small, 12, 56, more[s_sel][1], C_DIM);
    gfx_vline(g, 222, 10, 122, C_LINE);
    char kb[40];
    kb_text(kb, sizeof(kb));
    char stars[12], songs[12], done[16];
    snprintf(stars, sizeof(stars), "%d", s_sec[s_sel]);
    char ear[24];
    snprintf(ear, sizeof(ear), "%d of 6", s_ear_max + 1);
    snprintf(songs, sizeof(songs), "%d", ln_builtin_count);
    snprintf(done, sizeof(done), "%d of 72", s_ex_done);
    struct { const char *k, *v; uint16_t c; } rows[4][3] = {
        { { "Built-in songs", songs, C_TEXT }, { "Your MIDI files", "from the SD card", C_TEXT }, { 0 } },
        { { "Done", done, C_TEXT }, { "Keys", "all 12, major and minor", C_TEXT }, { 0 } },
        { { "A round", "1 minute, 3 lives", C_TEXT }, { "Clefs", "treble, bass or both", C_TEXT }, { 0 } },
        { { "Levels unlocked", ear, C_TEXT }, { "A round", "20 questions", C_TEXT }, { 0 } },
    };
    gfx_text(g, &font_small, 232, 10, "Stars earned", C_DIM);
    gfx_text_right(g, &font_small, g->w - 8, 10, stars, C_GOLD);
    for (int i = 0; i < 2 && rows[s_sel][i].k; i++) {
        int y = 30 + i * 20;
        gfx_text(g, &font_small, 232, y, rows[s_sel][i].k, C_DIM);
        gfx_text_right(g, &font_small, g->w - 8, y, rows[s_sel][i].v, rows[s_sel][i].c);
    }
    gfx_text(g, &font_small, 232, 70, "Keyboard", C_DIM);
    gfx_text_right(g, &font_small, g->w - 8, 70, kb, C_TEXT);
    gfx_icon(g, &icon_learn_64x64, 140, 72, C_ACC);
}

const screen_t scr_learn = {
    .name = "learn", .enter = menu_enter, .resume = menu_resume, .leave = menu_leave, .key = menu_key,
    .draw = menu_draw, .draw2 = menu_draw2,
};

/* ------------------------------------------------------------------ keyboard check */

static int s_stage;               /* 0 lowest, 1 highest, 2 done */
static volatile int s_heard = -1; /* note from the listener */
static uint8_t s_lo, s_hi;
static uint32_t s_err_until;

static void range_listener(const uint8_t *m, uint16_t len, uint8_t source)
{
    (void)source;
    if (len >= 3 && (m[0] & 0xF0) == 0x90 && m[2]) {
        s_heard = m[1] & 0x7F;
    }
}

static void range_enter(void *arg)
{
    (void)arg;
    s_stage = 0;
    s_heard = -1;
    if (!midi_in_running()) {
        midi_in_start(g_settings.din_port);
    }
    midi_in_set_listener(range_listener);
}

static void range_leave(void)
{
    midi_in_set_listener(NULL);
}

static void range_tick(uint32_t now)
{
    int n = s_heard;
    if (n < 0 || s_stage == 2) {
        return;
    }
    s_heard = -1;
    if (s_stage == 0) {
        s_lo = (uint8_t)n;
        s_stage = 1;
    } else if (n < s_lo + 12) {
        s_err_until = now + 2500;   /* that can't be the top of a keyboard */
    } else {
        s_hi = (uint8_t)n;
        s_stage = 2;
    }
    ui_invalidate();
}

static bool range_key(const btn_event_t *e)
{
    if (e->repeat) {
        return true;
    }
    switch (e->key) {
    case K_S1:
        ui_pop();
        return true;
    case K_S4:
        if (s_stage == 2) {
            s_stage = 0;
        }
        return true;
    case K_S5:
        if (s_stage == 2) {
            if (ln_kb_save(s_lo, s_hi)) {
                ln_ui_load_keyboard();
                ui_toast("Keyboard saved");
            } else {
                g_kb_lo = s_lo;
                g_kb_hi = s_hi;
                g_kb_known = true;
                ui_toast("No SD card: kept until power off");
            }
            ui_pop();
        }
        return true;
    default:
        return false;
    }
}

static void range_draw(gfx_t *g)
{
    char r[20];
    snprintf(r, sizeof(r), "step %d of 2", s_stage == 0 ? 1 : 2);
    w_status(g, "Your keyboard", r, C_ACC);
    if (s_stage < 2) {
        gfx_text_box(g, &font_main, 0, 40, g->w, ALIGN_CENTER, "Press the", C_DIM);
        gfx_text_box(g, &font_huge, 0, 58, g->w, ALIGN_CENTER, s_stage == 0 ? "LOWEST" : "HIGHEST", C_ACC);
        gfx_text_box(g, &font_main, 0, 94, g->w, ALIGN_CENTER, "key on your keyboard", C_DIM);
        if (ui_now() < s_err_until) {
            gfx_text_box(g, &font_main, 0, 116, g->w, ALIGN_CENTER, "That's not the top: press the highest key", C_REC);
        } else if (s_stage == 1) {
            char a[8], t[32];
            ln_ui_note_name(a, sizeof(a), s_lo);
            snprintf(t, sizeof(t), "Lowest: %s", a);
            gfx_text_box(g, &font_main, 0, 116, g->w, ALIGN_CENTER, t, C_TEXT);
        }
        gfx_text_box(g, &font_small, 20, 140, 280, ALIGN_CENTER, "Every song and exercise is fitted to the keys", C_DIM);
        gfx_text_box(g, &font_small, 20, 154, 280, ALIGN_CENTER, "you have, so this only needs doing once.", C_DIM);
        static const char *const keys[5] = { "Cancel", NULL, NULL, NULL, NULL };
        w_softkeys(g, keys, false);
        return;
    }
    char a[8], b[8], t[32];
    ln_ui_note_name(a, sizeof(a), s_lo);
    ln_ui_note_name(b, sizeof(b), s_hi);
    gfx_text_box(g, &font_main, 0, 34, g->w, ALIGN_CENTER, "Your keyboard", C_DIM);
    snprintf(t, sizeof(t), "%d keys", s_hi - s_lo + 1);
    gfx_text_box(g, &font_huge, 0, 52, g->w, ALIGN_CENTER, t, C_ACC);
    snprintf(t, sizeof(t), "%s " UI_ENDASH " %s", a, b);
    gfx_text_box(g, &font_large, 0, 92, g->w, ALIGN_CENTER, t, C_TEXT);
    int oct = (s_hi - s_lo + 1) / 12;
    snprintf(t, sizeof(t), "%d octave%s", oct >= 2 ? 2 : 1, oct >= 2 ? "s" : "");
    w_stat(g, 30, 128, 80, "Scales", t, C_TEXT);
    w_stat(g, 120, 128, 80, "Songs", s_hi - s_lo >= 60 ? "full range" : "fitted", C_TEXT);
    w_stat(g, 210, 128, 80, "Middle C", (s_lo <= 60 && s_hi >= 60) ? "C4" : "not on it", C_TEXT);
    gfx_text_box(g, &font_small, 20, 176, 280, ALIGN_CENTER, "Using octave buttons? Run this again after", C_DIM);
    gfx_text_box(g, &font_small, 20, 190, 280, ALIGN_CENTER, "changing octave so the notes line up.", C_DIM);
    static const char *const keys[5] = { "Back", NULL, NULL, "Redo", "Save" };
    w_softkeys(g, keys, false);
}

static void range_draw2(gfx_t *g)
{
    gfx_clear(g, C_BG);
    ln_kb_t k;
    ln_ui_kb_layout(&k, 21, 108, 6, g->w - 12, 30, s_stage == 2 ? 90 : 100);
    k.lo = 21;
    k.hi = 108;
    k.kw = 8;
    k.x0 = (g->w - 52 * 8) / 2;
    static uint16_t col[128];
    memset(col, 0, sizeof(col));
    if (s_stage >= 1) {
        col[s_lo] = C_ACC;
    }
    if (s_stage == 2) {
        col[s_hi] = C_ACC;
        w_header2(g, "Your keys on an 88-key piano", "", C_ACC);
        ln_ui_kb_draw(g, &k, col, s_lo, s_hi);
        int x0, w0, x1, w1, xc, wc;
        if (ln_ui_key_x(&k, s_lo, &x0, &w0) && ln_ui_key_x(&k, s_hi, &x1, &w1)) {
            gfx_fill(g, x0, 124, x1 + w1 - x0, 3, C_ACC);
            char a[8];
            ln_ui_note_name(a, sizeof(a), s_lo);
            gfx_text(g, &font_small, x0, 128, a, C_ACC);
            ln_ui_note_name(a, sizeof(a), s_hi);
            gfx_text_right(g, &font_small, x1 + w1, 128, a, C_ACC);
        }
        if (ln_ui_key_x(&k, 60, &xc, &wc) && s_lo <= 60 && s_hi >= 60) {
            gfx_text(g, &font_small, xc - 4, 128, "C4", C_DIM);
        }
    } else {
        w_header2(g, "88-key piano " UI_MIDDOT " press a key to see where it is", "", C_ACC);
        ln_ui_kb_draw(g, &k, col, 0, 0);
    }
}

const screen_t scr_lrange = {
    .name = "lrange", .enter = range_enter, .leave = range_leave, .key = range_key, .tick = range_tick,
    .draw = range_draw, .draw2 = range_draw2,
};
