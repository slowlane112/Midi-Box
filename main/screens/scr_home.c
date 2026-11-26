/* Home: app list on screen 1, status board on screen 2. */
#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "sd.h"
#include "midi_in.h"
#include "theme.h"
#include "widgets.h"
#include "ln_store.h"
#include "ln_ui.h"
#include "app.h"

typedef struct {
    const char *name;
    const char *line1, *line2;
    const icon_t *icon;
} app_t;

static const app_t s_apps[] = {
    { "Player",   "Play songs and manage",  "files on the SD card", &icon_boombox_64x64 },
    { "Studio",   "Record and edit",   "your own songs",    &icon_studio_64x64 },
    { "Learn",    "Piano practice games", "with scores and stars", &icon_learn_64x64 },
    { "Live In",  "Play a keyboard",   "through the synth", &icon_mididin_64x64 },
    { "Transfer", "Send files",        "from your PC",      &icon_file_transfer_64x64 },
    { "Settings", "Display, sound",    "and MIDI",          &icon_gear_64x64 },
};
#define APP_COUNT ((int)(sizeof(s_apps) / sizeof(s_apps[0])))

static int s_sel;
static char s_free[24];
#define APP_LEARN 2
static struct {
    bool loaded;
    ln_prac_t p;
    int stars, songs;
} s_learn;                 /* practice stats for screen 2, read when Learn is selected */

static void refresh_space(void)
{
    uint64_t total = 0, free_b = 0;
    if (sd_space(&total, &free_b)) {
        char f[16];
        fmt_size(f, sizeof(f), free_b);
        snprintf(s_free, sizeof(s_free), "%s free", f);
    } else {
        snprintf(s_free, sizeof(s_free), "no card");
    }
}

static void load_learn(void)
{
    if (s_sel != APP_LEARN || s_learn.loaded) {
        return;
    }
    ln_prac_get(&s_learn.p);
    s_learn.stars = ln_total_stars();
    s_learn.songs = ln_count_learnt("b:");
    ln_ui_load_keyboard();
    s_learn.loaded = true;
}

static void enter(void *arg)
{
    (void)arg;
    refresh_space();
    s_learn.loaded = false;
    load_learn();
}

static void resume(void)
{
    refresh_space();
    s_learn.loaded = false;   /* you may have practised */
    load_learn();
}

static void open_app(int i)
{
    static files_args_t fa;
    const screen_t *const screens[] = { &scr_files, &scr_projects, &scr_learn, &scr_livein, &scr_transfer, &scr_settings };
    if (i == 0) {
        /* Player: the song browser (and file manager), in the folder last played from */
        fa = (files_args_t){ .dir = g_settings.last_dir };
        ui_push(&scr_files, &fa);
    } else if (i < APP_COUNT) {
        ui_push(screens[i], NULL);
    }
}

static bool key(const btn_event_t *e)
{
    switch (e->key) {
    case K_UP:
        s_sel = (s_sel + APP_COUNT - 1) % APP_COUNT;
        load_learn();
        return true;
    case K_DOWN:
        s_sel = (s_sel + 1) % APP_COUNT;
        load_learn();
        return true;
    case K_S5:
        if (!e->repeat) {
            open_app(s_sel);
        }
        return true;
    case K_S1:
        if (!e->repeat) {
            if (g_settings.resume_path[0] && fs_exists(g_settings.resume_path)) {
                static player_args_t pa;
                pa.path = g_settings.resume_path;
                pa.start_ms = g_settings.resume_ms;
                ui_push(&scr_player, &pa);
            } else {
                ui_toast("Nothing to resume yet");
            }
        }
        return true;
    default:
        return false;
    }
}

static void draw(gfx_t *g)
{
    w_status(g, "MIDI Box", midi_in_running() ? "DIN on" : "", C_ACC);
    for (int i = 0; i < APP_COUNT; i++) {
        int y = CONTENT_Y + 4 + i * 32;
        bool sel = i == s_sel;
        if (sel) {
            gfx_fill(g, 0, y, 140, 30, C_ACC_BG);
            gfx_fill(g, 0, y, 3, 30, C_ACC);
        }
        gfx_text(g, &font_large, 12, y + 6, s_apps[i].name, sel ? C_ACC : C_TEXT);
    }
    gfx_vline(g, 146, CONTENT_Y + 4, 190, C_LINE);
    const app_t *a = &s_apps[s_sel];
    gfx_icon(g, a->icon, 146 + (174 - 64) / 2, 44, C_ACC);
    gfx_text_box(g, &font_large, 150, 122, 166, ALIGN_CENTER, a->name, C_TEXT);
    gfx_text_box(g, &font_small, 150, 146, 166, ALIGN_CENTER, a->line1, C_DIM);
    gfx_text_box(g, &font_small, 150, 160, 166, ALIGN_CENTER, a->line2, C_DIM);
    static const char *const keys[5] = { "Resume", NULL, NULL, NULL, "Open" };
    w_softkeys(g, keys, false);
}

static void fmt_dur(char *o, int n, uint32_t s)
{
    if (s >= 3600) {
        snprintf(o, (size_t)n, "%lu h %lu min", (unsigned long)(s / 3600), (unsigned long)(s % 3600 / 60));
    } else {
        snprintf(o, (size_t)n, "%lu min", (unsigned long)(s / 60));
    }
}

/* screen 2 with Learn selected: your practice */
static void draw2_learn(gfx_t *g)
{
    const ln_prac_t *p = &s_learn.p;
    gfx_text(g, &font_huge, 10, 8, "Learn", C_ACC);
    gfx_text(g, &font_small, 12, 44, "Practise, score points, beat your best", C_DIM);
    gfx_text(g, &font_small, 12, 72, "LAST SESSIONS (MINUTES)", C_DIM);
    uint32_t mx = 10 * 60;
    for (int i = 0; i < p->nrecent; i++) {
        mx = p->recent[i] > mx ? p->recent[i] : mx;
    }
    for (int i = 0; i < LN_RECENT; i++) {
        int x = 12 + i * 27;
        uint32_t v = i < p->nrecent ? p->recent[i] : 0;
        int bh = (int)(v * 40 / mx);
        if (i < p->nrecent && bh < 2) {
            bh = 2;
        }
        gfx_fill(g, x, 128 - (bh ? bh : 2), 18, bh ? bh : 2, bh ? (i == p->nrecent - 1 ? C_AMBER : C_ACC) : C_LINE);
        if (i < p->nrecent) {
            char m[24];
            snprintf(m, sizeof(m), "%lu", (unsigned long)((v + 30) / 60));
            gfx_text_box(g, &font_small, x - 4, 131, 26, ALIGN_CENTER, m, C_DIM);
        }
    }
    if (!p->nrecent) {
        gfx_text(g, &font_small, 12, 100, "No practice yet " UI_ENDASH " open Learn!", C_TEXT);
    }
    gfx_vline(g, 222, 10, 122, C_LINE);
    char sess[24], tot[40], stars[24], songs[24], combo[24], kb[24];
    snprintf(sess, sizeof(sess), "%lu", (unsigned long)p->sessions);
    fmt_dur(tot, sizeof(tot), p->total_s);
    snprintf(stars, sizeof(stars), "%d", s_learn.stars);
    snprintf(songs, sizeof(songs), "%d", s_learn.songs);
    snprintf(combo, sizeof(combo), "%u", p->best_combo);
    if (g_kb_known) {
        snprintf(kb, sizeof(kb), "%d keys", g_kb_hi - g_kb_lo + 1);
    } else {
        snprintf(kb, sizeof(kb), "not checked");
    }
    struct { const char *k, *v; uint16_t c; } rows[] = {
        { "Sessions", sess, C_AMBER }, { "Practice time", tot, C_TEXT }, { "Stars earned", stars, C_GOLD },
        { "Songs learnt", songs, C_TEXT }, { "Best combo", combo, C_TEXT }, { "Keyboard", kb, C_TEXT },
    };
    for (int i = 0; i < 6; i++) {
        int y = 10 + i * 20;
        gfx_text(g, &font_small, 232, y, rows[i].k, C_DIM);
        gfx_text_right(g, &font_small, g->w - 8, y, rows[i].v, rows[i].c);
    }
}

static void draw2(gfx_t *g)
{
    gfx_clear(g, C_BG);
    if (s_sel == APP_LEARN && s_learn.loaded) {
        draw2_learn(g);
        return;
    }
    gfx_text(g, &font_huge, 10, 8, "MIDI Box", C_ACC);
    gfx_text(g, &font_small, 12, 44, "v" MIDIBOX_VERSION " " UI_MIDDOT " SAM2695", C_DIM);
    gfx_text(g, &font_small, 12, 72, "RESUME", C_DIM);
    if (g_settings.resume_path[0]) {
        gfx_text_box(g, &font_main, 12, 86, 196, ALIGN_LEFT, path_name(g_settings.resume_path), C_TEXT);
        char t[16];
        fmt_time(t, sizeof(t), g_settings.resume_ms);
        gfx_text(g, &font_small, 12, 106, t, C_DIM);
    } else {
        gfx_text(g, &font_main, 12, 86, "Nothing yet", C_DIM);
    }
    gfx_vline(g, 222, 10, 122, C_LINE);
    struct { const char *k; const char *v; } rows[] = {
        { "SD card", s_free },
        { "Sound bank", g_settings.bank ? "MT-32" : "General MIDI" },
        { "Keyboard sound", g_settings.thru ? "on" : "off" },
        { "DIN input", g_settings.din_port ? "DIN 2 (front)" : "DIN 1 (back)" },
        { "Colour", theme_accent_name(g_settings.accent) },
    };
    for (int i = 0; i < 5; i++) {
        int y = 10 + i * 20;
        gfx_text(g, &font_small, 232, y, rows[i].k, C_DIM);
        gfx_text_right(g, &font_small, g->w - 8, y, rows[i].v, C_TEXT);
    }
}

const screen_t scr_home = {
    .name = "home", .enter = enter, .resume = resume, .key = key, .draw = draw, .draw2 = draw2,
};

/* ------------------------------------------------------------------ placeholder */

static const char *s_soon_name;

static void soon_enter(void *arg) { s_soon_name = arg ? (const char *)arg : ""; }

static bool soon_key(const btn_event_t *e)
{
    if (e->key == K_S1 && !e->repeat) {
        ui_pop();
        return true;
    }
    return false;
}

static void soon_draw(gfx_t *g)
{
    w_status(g, s_soon_name, NULL, C_ACC);
    gfx_text_box(g, &font_large, 0, 90, g->w, ALIGN_CENTER, s_soon_name, C_TEXT);
    gfx_text_box(g, &font_main, 0, 116, g->w, ALIGN_CENTER, "Coming in phase 2", C_DIM);
    static const char *const keys[5] = { "Back", NULL, NULL, NULL, NULL };
    w_softkeys(g, keys, false);
}

const screen_t scr_soon = {
    .name = "soon", .enter = soon_enter, .key = soon_key, .draw = soon_draw,
};
