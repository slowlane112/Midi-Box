/* Settings: brightness, colour, sound bank, keyboard sound (MIDI in -> synth), auto play, visualiser, about. */
#include <stdio.h>
#include "esp_heap_caps.h"
#include "settings.h"
#include "lcd.h"
#include "midi_out.h"
#include "midi_in.h"
#include "theme.h"
#include "widgets.h"
#include "dialog.h"
#include "viz.h"
#include "app.h"

enum { S_BRIGHT, S_COLOUR, S_BANK, S_THRU, S_AUTO, S_VIZ, S_ABOUT, S_COUNT };
static const char *const s_names[S_COUNT] = { "Display brightness", "Display colour", "Sound bank", "Keyboard sound", "Auto play", "Visualiser", "About" };
static int s_sel;
static uint8_t s_old;

static void value_text(int i, char *o, int n)
{
    switch (i) {
    case S_BRIGHT: snprintf(o, (size_t)n, "%d / 10", g_settings.brightness); break;
    case S_COLOUR: snprintf(o, (size_t)n, "%s", theme_accent_name(g_settings.accent)); break;
    case S_BANK:   snprintf(o, (size_t)n, "%s", g_settings.bank ? "Roland MT-32" : "General MIDI"); break;
    case S_THRU:   snprintf(o, (size_t)n, "%s", g_settings.thru ? "On" : "Off"); break;
    case S_AUTO:   snprintf(o, (size_t)n, "%s", g_settings.autoplay ? "On" : "Off"); break;
    case S_VIZ:    snprintf(o, (size_t)n, "%s", viz_name()); break;
    default:       snprintf(o, (size_t)n, "v%s", MIDIBOX_VERSION); break;
    }
}

static void bright_fmt(char *o, int n) { value_text(S_BRIGHT, o, n); }
static void bright_change(int d)
{
    int v = g_settings.brightness + (d > 0 ? 1 : -1);
    v = v < 1 ? 1 : (v > 10 ? 10 : v);    /* never fully off from here */
    g_settings.brightness = (uint8_t)v;
    backlight_set(g_settings.brightness);
}
static void bright_done(bool ok)
{
    if (!ok) {
        g_settings.brightness = s_old;
        backlight_set(s_old);
    }
    settings_changed();
}

static void colour_fmt(char *o, int n) { value_text(S_COLOUR, o, n); }
static void colour_change(int d)
{
    g_settings.accent = (uint8_t)((g_settings.accent + THEME_ACCENTS + (d > 0 ? 1 : -1)) % THEME_ACCENTS);
    theme_apply(g_settings.accent);
}
static void colour_done(bool ok)
{
    if (!ok) {
        g_settings.accent = s_old;
        theme_apply(s_old);
    }
    settings_changed();
}

static void open_item(void)
{
    switch (s_sel) {
    case S_BRIGHT: {
        s_old = g_settings.brightness;
        static const dlg_value_t v = { "Display brightness", NULL, bright_fmt, bright_change, bright_done };
        dlg_value(&v);
        break;
    }
    case S_COLOUR: {
        s_old = g_settings.accent;
        static const dlg_value_t v = { "Display colour", "Used for highlights on both screens", colour_fmt, colour_change, colour_done };
        dlg_value(&v);
        break;
    }
    case S_BANK:
        g_settings.bank = g_settings.bank ? BANK_GM : BANK_MT32;
        midi_out_set_bank(g_settings.bank);
        settings_changed();
        break;
    case S_THRU:
        g_settings.thru = !g_settings.thru;
        midi_in_set_thru(g_settings.thru);
        settings_changed();
        break;
    case S_AUTO:
        g_settings.autoplay = !g_settings.autoplay;
        settings_changed();
        break;
    case S_VIZ:
        viz_step(1);
        break;
    default:
        ui_toast("Free: %u KB RAM, %u KB PSRAM",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
        break;
    }
}

static bool key(const btn_event_t *e)
{
    switch (e->key) {
    case K_UP: s_sel = (s_sel + S_COUNT - 1) % S_COUNT; return true;
    case K_DOWN: s_sel = (s_sel + 1) % S_COUNT; return true;
    case K_S1:
        if (!e->repeat) {
            ui_pop();
        }
        return true;
    case K_S5:
        if (!e->repeat) {
            open_item();
        }
        return true;
    default:
        return false;
    }
}

static void draw(gfx_t *g)
{
    w_status(g, "Settings", "", C_ACC);
    for (int i = 0; i < S_COUNT; i++) {
        int y = CONTENT_Y + i * ROW_H;
        bool sel = i == s_sel;
        w_row(g, y, ROW_H, sel);
        gfx_text(g, &font_main, 12, y + 4, s_names[i], sel ? C_ACC : C_TEXT);
        char v[32];
        value_text(i, v, sizeof(v));
        gfx_text_right(g, &font_main, g->w - 10, y + 4, v, C_DIM);
    }
    char build[48];
    snprintf(build, sizeof(build), "Built %s %s", __DATE__, __TIME__);
    gfx_text(g, &font_small, 12, 196, build, C_DIM);
    static const char *const keys[5] = { "Back", NULL, NULL, NULL, "Change" };
    w_softkeys(g, keys, false);
}

/* screen 2: what the selected setting does, every value at a glance; on Visualiser,
 * the visualiser itself so you can see what you are choosing */
static void draw2(gfx_t *g)
{
    if (s_sel == S_VIZ) {
        viz_draw(g);
        return;
    }
    static const char *const help[S_COUNT] = {
        "Backlight of both screens", "Highlight colour of the menus and screens",
        "Sounds of the SAM2695: GM or MT-32 set", "Play your keyboard through the synth",
        "Play songs as soon as they load",
        "", "MIDI Box firmware",
    };
    gfx_clear(g, C_BG);
    gfx_text(g, &font_huge, 10, 8, "Settings", C_ACC);
    gfx_text_box(g, &font_large, 12, 56, 200, ALIGN_LEFT, s_names[s_sel], C_TEXT);
    char v[32];
    value_text(s_sel, v, sizeof(v));
    gfx_text_box(g, &font_main, 12, 80, 200, ALIGN_LEFT, v, C_ACC);
    gfx_text_box(g, &font_small, 12, 104, 200, ALIGN_LEFT, help[s_sel], C_DIM);
    gfx_vline(g, 222, 10, 122, C_LINE);
    static const char *const shortn[S_COUNT] = { "Brightness", "Colour", "Sound bank", "Keyboard sound", "Auto play", "Visualiser", "Version" };
    for (int i = 0; i < S_COUNT; i++) {
        int y = 8 + i * 18;
        value_text(i, v, sizeof(v));
        bool sel = i == s_sel;
        gfx_text(g, &font_small, 232, y, shortn[i], sel ? C_ACC : C_DIM);
        gfx_text_right(g, &font_small, g->w - 8, y, v, sel ? C_ACC : C_TEXT);
    }
}

const screen_t scr_settings = {
    .name = "settings", .key = key, .draw = draw, .draw2 = draw2,
};
