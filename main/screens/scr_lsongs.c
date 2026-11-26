/*
 * Learn > Play Along: choose a song. The built-in songs (easiest first) and, last,
 * any MIDI file from the SD card (picked in the Player's browser).
 *
 * 2/3 choose (5/6 a page), 8 open, 4 back.
 */
#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "theme.h"
#include "widgets.h"
#include "ln_builtin.h"
#include "ln_song.h"
#include "ln_store.h"
#include "ln_ui.h"
#include "app.h"

#define ROWS 7
#define SONG_ROW_H 28

static int s_sel;
static int8_t s_stars[32];
static struct {
    int sel;                 /* song these bests belong to, -1 = stale */
    bool have[3][2];
    uint32_t score[3][2];
    int stars[3][2];
} s_best = { .sel = -1 };

static const char *const s_level[5] = { "", "Beginner", "Easy", "Intermediate", "Advanced" };

static int count(void) { return ln_builtin_count + 1; }

static void refresh(void)
{
    s_best.sel = -1;
    for (int i = 0; i < ln_builtin_count && i < 32; i++) {
        char k[40];
        snprintf(k, sizeof(k), "b:%s", ln_builtin[i].id);
        s_stars[i] = (int8_t)ln_best_stars(k);
    }
}

static void enter(void *arg)
{
    (void)arg;
    refresh();
}

static void resume(void)
{
    refresh();
    ui_invalidate();
}

static void file_picked(const char *path)
{
    int err = ln_song_file(path);
    if (err) {
        static const char *const why[6] = { "", "Could not read the file", "This is not a MIDI file we can play",
                                            "Not enough memory for this file", "This file has no notes",
                                            "Incomplete file" };
        ui_toast("%s", why[err < 6 ? err : 1]);
        return;
    }
    g_lset.builtin = -1;
    ui_push(&scr_lsetup, NULL);
}

static bool key(const btn_event_t *e)
{
    int n = count();
    switch (e->key) {
    case K_UP: s_sel = (s_sel + n - 1) % n; return true;
    case K_DOWN: s_sel = (s_sel + 1) % n; return true;
    case K_S2: s_sel = s_sel >= ROWS ? s_sel - ROWS : 0; return true;
    case K_S3: s_sel = s_sel + ROWS < n ? s_sel + ROWS : n - 1; return true;
    case K_S1:
        if (!e->repeat) {
            ui_pop();
        }
        return true;
    case K_S5:
        if (e->repeat) {
            return true;
        }
        if (s_sel < ln_builtin_count) {
            if (ln_song_builtin(s_sel)) {
                g_lset.builtin = s_sel;
                ui_push(&scr_lsetup, NULL);
            } else {
                ui_toast("Not enough memory");
            }
        } else {
            static files_args_t fa;
            fa = (files_args_t){ .dir = g_settings.last_dir, .on_pick = file_picked, .title = "Pick a MIDI file" };
            ui_push(&scr_files, &fa);
        }
        return true;
    default:
        return false;
    }
}

static void draw(gfx_t *g)
{
    char r[24];
    snprintf(r, sizeof(r), "%d songs", ln_builtin_count);
    w_status(g, "Play Along", r, C_ACC);
    int n = count(), top = w_list_top(s_sel, n, ROWS);
    for (int i = 0; i < ROWS && top + i < n; i++) {
        int idx = top + i, y = CONTENT_Y + 1 + i * SONG_ROW_H;
        bool sel = idx == s_sel;
        w_row(g, y, SONG_ROW_H - 1, sel);
        if (idx < ln_builtin_count) {
            const ln_builtin_t *b = &ln_builtin[idx];
            gfx_text(g, &font_main, 12, y + 2, b->title, sel ? C_ACC : C_TEXT);
            char sub[48];
            snprintf(sub, sizeof(sub), "%s " UI_MIDDOT " %s", b->composer, s_level[b->level]);
            gfx_text(g, &font_small, 12, y + 16, sub, C_DIM);
            w_stars(g, g->w - 52, y + 13, 6, 3, s_stars[idx], C_GOLD, C_LINE);
        } else {
            gfx_text(g, &font_main, 12, y + 2, "A MIDI file from the SD card" UI_ELLIPSIS, sel ? C_ACC : C_TEXT);
            gfx_text(g, &font_small, 12, y + 16, "Choose the right and left hand parts", C_DIM);
        }
    }
    w_scrollbar(g, CONTENT_Y, ROWS * SONG_ROW_H, top, ROWS, n);
    static const char *const keys[5] = { "Back", "Page up", "Page dn", NULL, "Choose" };
    w_softkeys(g, keys, false);
}

static void draw2(gfx_t *g)
{
    gfx_clear(g, C_BG);
    if (s_sel >= ln_builtin_count) {
        gfx_text(g, &font_huge, 10, 8, "Your songs", C_ACC);
        gfx_text(g, &font_small, 12, 44, "Any .mid file: pick which part is each hand,", C_DIM);
        gfx_text(g, &font_small, 12, 58, "the box plays the rest of the band.", C_DIM);
        return;
    }
    const ln_builtin_t *b = &ln_builtin[s_sel];
    /* long titles drop to the next font size before they get cut */
    bool big = gfx_text_width(&font_huge, b->title) <= 410;
    gfx_text_box(g, big ? &font_huge : &font_large, 10, big ? 8 : 14, 410, ALIGN_LEFT, b->title, C_ACC);
    char t[64];
    snprintf(t, sizeof(t), "%s " UI_MIDDOT " %s " UI_MIDDOT " %d bars " UI_MIDDOT " %d bpm",
             b->composer, s_level[b->level], b->nbars, b->bpm);
    gfx_text(g, &font_small, 12, 44, t, C_DIM);
    if (b->credit)
        gfx_text(g, &font_small, 12, 56, b->credit, C_DIM);
    gfx_vline(g, 300, 66, 66, C_LINE);
    /* best for each hand / mode */
    static const char *const hn[3] = { "Right hand", "Left hand", "Both hands" };
    static const char hc[3] = { 'R', 'L', 'B' };
    if (s_best.sel != s_sel) {   /* read the SD card once per song, not every frame */
        s_best.sel = s_sel;
        for (int h = 0; h < 3; h++) {
            for (int m = 0; m < 2; m++) {
                char k[48];
                snprintf(k, sizeof(k), "b:%s|%c|%s", b->id, hc[h], m ? "play" : "wait");
                s_best.have[h][m] = ln_best_get(k, &s_best.score[h][m], &s_best.stars[h][m]);
            }
        }
    }
    gfx_text(g, &font_small, 12, 70, "BEST", C_DIM);
    for (int h = 0; h < 3; h++) {
        int y = 84 + h * 18;
        gfx_text(g, &font_small, 12, y, hn[h], C_TEXT);
        for (int m = 0; m < 2; m++) {
            uint32_t sc = s_best.score[h][m];
            int st = s_best.stars[h][m];
            int x = 100 + m * 100;
            if (s_best.have[h][m]) {
                char s[16];
                snprintf(s, sizeof(s), "%lu", (unsigned long)sc);
                w_stars(g, x, y + 5, 5, 3, st, C_GOLD, C_LINE);
                gfx_text(g, &font_small, x + 40, y, s, C_DIM);
            } else {
                gfx_text(g, &font_small, x, y, "-", C_LINE);
            }
        }
    }
    gfx_text(g, &font_small, 100, 70, "WAIT", C_DIM);
    gfx_text(g, &font_small, 200, 70, "PLAY", C_DIM);
    w_stars(g, 318, 100, 14, 3, s_stars[s_sel], C_GOLD, C_LINE);
}

const screen_t scr_lsongs = {
    .name = "lsongs", .enter = enter, .resume = resume, .key = key, .draw = draw, .draw2 = draw2,
};
