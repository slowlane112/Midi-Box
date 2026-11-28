/*
 * viz.c - screen-2 visualisers: the list, the buttons, and the heading.
 *
 * Two kinds:
 *   Info    - shows what is being played: Piano, Spectrum, Staff, Chords, Piano Roll
 *   Graphic - moves with the music:        One, Two, Three, Four, Pulse, Fireworks
 *
 * Buttons: 9 next (Shift+9 previous), 10 option, 11 colour.
 * Every change shows a heading for 2 s ("Info · Spectrum", "Peaks on", "Colour: Blue").
 */
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "monitor.h"
#include "settings.h"
#include "theme.h"
#include "font.h"
#include "ui.h"
#include "v1_compat.h"
#include "v1_state.h"
#include "viz_common.h"
#include "vz.h"
#include "display_piano.h"
#include "display_visualiser_one.h"
#include "display_visualiser_two.h"
#include "display_visualiser_three.h"
#include "display_visualiser_four.h"
#include "viz.h"

enum { CK_NONE, CK_COLOUR, CK_BG, CK_CHAN, CK_THEME };   /* what button 11 changes */

typedef struct {
    const char *name;
    bool graphic;
    uint8_t nopt;
    const char *opt_title;                 /* e.g. "Peaks" */
    const char *const *opt_names;          /* NULL = Off/On */
    uint8_t colour;                        /* CK_* */
    void (*draw)(int colour, int opt, bool fresh);
    const char *const *col_names;          /* button 11 choices; NULL = the 8 standard colours */
    uint8_t ncol;
} viz_t;

static bool s_active;

/* --- v1 visualisers, fed through their globals ------------------------------------- */

static void d_piano(int colour, int opt, bool fresh)
{
    display_piano_show_notes = opt != 0;
    display_piano_key_on_color = (uint8_t)colour;
    if (fresh) {
        g_v1_key_on_fired = true;
    }
    display_piano_draw(&g_v1_values, &g_v1_key_on_fired, NULL, s_active);
}

static void d_one(int colour, int opt, bool fresh)
{
    (void)fresh;
    display_visualiser_one_background_oval = opt != 0;
    display_visualiser_one_background_color = (uint8_t)colour;
    display_visualiser_one_draw(&g_v1_values, NULL, s_active);
}

static void d_two(int colour, int opt, bool fresh)
{
    (void)fresh;
    display_visualiser_two_background_oval = opt != 0;
    display_visualiser_two_background_color = (uint8_t)colour;
    display_visualiser_two_draw(&g_v1_values, NULL, s_active);
}

static void d_three(int colour, int opt, bool fresh)
{
    (void)fresh;
    display_visualiser_three_shape = (uint8_t)opt;
    display_visualiser_three_background_color = (uint8_t)colour;
    display_visualiser_three_draw(&g_v1_values, NULL, s_active);
}

static void d_four(int colour, int opt, bool fresh)
{
    (void)fresh;
    (void)colour;
    display_visualiser_four_shape = (uint8_t)opt;
    display_visualiser_four_draw(&g_v1_values, NULL, s_active);
}

/* --- v2 visualisers ------------------------------------------------------------------ */

static void d_spectrum(int c, int o, bool f) { vz_spectrum_draw(c, o, s_active, f); }
static void d_staff(int c, int o, bool f) { vz_staff_draw(c, o, s_active, f); }
static void d_chords(int c, int o, bool f) { vz_chords_draw(c, o, s_active, f); }
static void d_roll(int c, int o, bool f) { vz_roll_draw(c, o, s_active, f); }
static void d_pulse(int c, int o, bool f) { vz_pulse_draw(c, o, s_active, f); }
static void d_fireworks(int c, int o, bool f) { vz_fireworks_draw(c, o, s_active, f); }
static void d_kaleido(int c, int o, bool f) { vz_kaleido_draw(c, o, s_active, f); }
static void d_synthwave(int c, int o, bool f) { vz_synthwave_draw(c, o, s_active, f); }
static void d_spiro(int c, int o, bool f) { vz_spiro_draw(c, o, s_active, f); }

static const char *const s_channels[17] = {
    "All channels", "Channel 1", "Channel 2", "Channel 3", "Channel 4", "Channel 5", "Channel 6",
    "Channel 7", "Channel 8", "Channel 9", "Channel 10", "Channel 11", "Channel 12", "Channel 13",
    "Channel 14", "Channel 15", "Channel 16",
};
static const char *const s_speed[3] = { "Slow", "Medium", "Fast" };
static const char *const s_shape3[4] = { "Auto", "Circle", "Square", "Triangle" };
static const char *const s_shape4[5] = { "Auto", "Circle", "Diamond", "Squircle", "Octagon" };
static const char *const s_shape_pulse[5] = { "Auto", "Circle", "Square", "Diamond", "Triangle" };
static const char *const s_pulse_cols[9] = { "Green", "Blue", "Amber", "Purple", "Pink", "Cyan", "Red", "White", "Rainbow" };
static const char *const s_slices[3] = { "6", "8", "12" };
static const char *const s_themes[4] = { "Classic", "Outrun", "Ice", "Toxic" };
static const char *const s_spiro_cols[9] = { "Rainbow", "Green", "Blue", "Amber", "Purple", "Pink", "Cyan", "Red", "White" };

/* order is stored in settings: only add to the end */
static const viz_t s_viz[] = {
    { "Piano",            false, 2,  "Note names", NULL,       CK_COLOUR, d_piano, NULL, 0 },
    { "Spectrum Analyser", false, 2, "Peaks",      NULL,       CK_COLOUR, d_spectrum, NULL, 0 },
    { "Staff",            false, 17, NULL,         s_channels, CK_CHAN,   d_staff, NULL, 0 },
    { "Chords",           false, 17, NULL,         s_channels, CK_COLOUR, d_chords, NULL, 0 },
    { "Piano Roll",       false, 3,  "Speed",      s_speed,    CK_BG,     d_roll, NULL, 0 },
    { "Constellation",    true,  2,  "Blobs",      NULL,       CK_BG,     d_one, NULL, 0 },
    { "Embers",           true,  2,  "Rings",      NULL,       CK_BG,     d_two, NULL, 0 },
    { "Warp",             true,  4,  "Shape",      s_shape3,   CK_BG,     d_three, NULL, 0 },
    { "Psychedelic",      true,  5,  "Shape",      s_shape4,   CK_NONE,   d_four, NULL, 0 },
    { "Pulse",            true,  5,  "Shape",      s_shape_pulse, CK_COLOUR, d_pulse, s_pulse_cols, 9 },
    { "Fireworks",        true,  2,  "Trails",     NULL,       CK_CHAN,   d_fireworks, NULL, 0 },
    { "Kaleidoscope",     true,  3,  "Slices",     s_slices,   CK_BG,     d_kaleido, NULL, 0 },
    { "Synthwave",        true,  3,  "Speed",      s_speed,    CK_THEME,  d_synthwave, s_themes, 4 },
    { "Spirograph",       true,  2,  "Trails",     NULL,       CK_COLOUR, d_spiro, s_spiro_cols, 9 },
};
#define VIZ_COUNT ((int)(sizeof(s_viz) / sizeof(s_viz[0])))

static const char *const s_colour_names[VIZ_COLOURS] = {
    "Green", "Blue", "Amber", "Purple", "Pink", "Cyan", "Red", "White",
};

static char s_head[48];
static int64_t s_head_until;
static int s_drawn = -1;          /* visualiser drawn last frame */
static uint32_t s_last_frame;

static int mode(void) { return g_settings.viz % VIZ_COUNT; }

static void heading(const char *text)
{
    snprintf(s_head, sizeof(s_head), "%s", text);
    s_head_until = esp_timer_get_time() + 2000000;
}

static void heading_name(void)
{
    const viz_t *v = &s_viz[mode()];
    char b[48];
    snprintf(b, sizeof(b), "%s " UI_MIDDOT " %s", v->graphic ? "Graphic" : "Info", v->name);
    heading(b);
}

void viz_init(void)
{
    if (g_settings.viz >= VIZ_COUNT) {
        g_settings.viz = 0;
    }
}

const char *viz_name(void) { return s_viz[mode()].name; }
const char *viz_heading(void) { return esp_timer_get_time() < s_head_until ? s_head : ""; }
void viz_set_active(bool on) { s_active = on; }

static void free_buffers(void)
{
    display_visualiser_one_deinit();
    display_visualiser_two_deinit();
    display_visualiser_three_deinit();
    display_visualiser_four_deinit();
    vz_pulse_free();
    vz_spectrum_free();
    vz_roll_free();
    vz_staff_free();
    vz_fireworks_free();
    vz_kaleido_free();
    vz_synthwave_free();
    vz_spiro_free();
}

void viz_step(int dir)
{
    free_buffers();   /* only the visualiser on screen keeps its buffers */
    g_settings.viz = (uint8_t)((mode() + VIZ_COUNT + dir) % VIZ_COUNT);
    settings_changed();
    heading_name();
}

void viz_option(int dir)
{
    int m = mode();
    const viz_t *v = &s_viz[m];
    g_settings.viz_opt[m] = (uint8_t)((g_settings.viz_opt[m] + v->nopt + (dir < 0 ? -1 : 1)) % v->nopt);
    settings_changed();
    int o = g_settings.viz_opt[m];
    char b[48];
    if (!v->opt_title) {
        snprintf(b, sizeof(b), "%s", v->opt_names[o]);
    } else if (!v->opt_names) {
        snprintf(b, sizeof(b), "%s %s", v->opt_title, o ? "on" : "off");
    } else {
        snprintf(b, sizeof(b), "%s: %s", v->opt_title, v->opt_names[o]);
    }
    heading(b);
}

static int s_cyc_k = -1, s_cyc_m = -1;   /* the colour Cycle is on, for the heading */
static int64_t s_cyc_start;              /* when Cycle started: the first change is 8 s later */

/* button 11 choices: the visualiser's own list, then Cycle (stored as the list length) */
static int ncol_of(const viz_t *v) { return v->ncol ? v->ncol : VIZ_COLOURS; }

/* the name of choice c, with its prefix ("Colour: Blue", "Background: Black", "Theme: Ice") */
static void choice_text(int m, int c, char *b, int n)
{
    const viz_t *v = &s_viz[m];
    int ncol = ncol_of(v);
    const char *pre = v->colour == CK_THEME ? "Theme" : (v->colour == CK_BG ? "Background" : "Colour");
    if (c >= ncol) {
        snprintf(b, (size_t)n, "%s: Cycle", pre);
    } else if (v->col_names) {
        snprintf(b, (size_t)n, "%s: %s", pre, v->col_names[c]);
    } else if (v->colour == CK_BG && c == 0) {
        snprintf(b, (size_t)n, "Background: Black");
    } else if (v->colour == CK_CHAN && c == 0) {
        snprintf(b, (size_t)n, "Colour: By channel");
    } else {
        snprintf(b, (size_t)n, "%s: %s", pre, s_colour_names[c % VIZ_COLOURS]);
    }
}

void viz_colour(int dir)
{
    int m = mode();
    const viz_t *v = &s_viz[m];
    if (v->colour == CK_NONE) {
        heading("Uses its own colours");
        return;
    }
    int ncol = ncol_of(v);
    int step = dir < 0 ? -1 : 1;
    int c = (g_settings.viz_col[m] + ncol + 1 + step) % (ncol + 1);   /* the list, then Cycle */
    if (m == 0 && c == 7) {
        c = step > 0 ? ncol : 6;   /* Piano: no White, the keys are white */
    }
    g_settings.viz_col[m] = (uint8_t)c;
    settings_changed();
    s_cyc_m = -1;
    char b[48];
    choice_text(m, c, b, sizeof(b));
    heading(b);
}

/* Cycle: which choices it goes through (every colour, or every coloured background) */
static int cycle_members(int m, int *out)
{
    const viz_t *v = &s_viz[m];
    int n = 0;
    if (v->col_names) {
        for (int i = 0; i < v->ncol; i++) {
            out[n++] = i;
        }
    } else if (v->colour == CK_BG || v->colour == CK_CHAN) {
        for (int i = 1; i < VIZ_COLOURS; i++) {
            out[n++] = i;   /* not black / not by channel */
        }
    } else {
        for (int i = 0; i < VIZ_COLOURS; i++) {
            if (!(m == 0 && i == 7)) {
                out[n++] = i;
            }
        }
    }
    return n;
}

#define CYCLE_US 8000000   /* each colour for 8 s ... */
#define FADE_US  1000000   /* ... the last second fading into the next */

/* sets the blend for this frame; returns the colour argument to draw with */
static int cycle_frame(int m)
{
    int last_k = s_cyc_k, last_m = s_cyc_m;
    if (last_m != m) {
        s_cyc_start = esp_timer_get_time();
    }
    int mem[16];
    int n = cycle_members(m, mem);
    int64_t now = esp_timer_get_time();
    int64_t phase = (now - s_cyc_start) % ((int64_t)CYCLE_US * n);
    int k = (int)(phase / CYCLE_US);
    int64_t in = phase % CYCLE_US;
    uint8_t t = in > CYCLE_US - FADE_US ? (uint8_t)((in - (CYCLE_US - FADE_US)) * 255 / FADE_US) : 0;
    viz_set_cycle(mem[k], mem[(k + 1) % n], t);
    (void)last_k;   /* colours change quietly: no heading */
    s_cyc_k = k;
    s_cyc_m = m;
    return s_viz[m].col_names ? s_viz[m].ncol : VIZ_CYCLE;
}

void viz_set_caption(const char *text)
{
    if (text && text[0]) {
        heading(text);
    }
}

static void draw_heading(gfx_t *g)
{
    const viz_t *v = &s_viz[mode()];
    uint16_t acc = viz_col(g_settings.viz_col[mode()]);
    if (v->colour != CK_COLOUR || v->col_names) {
        acc = C_ACC;
    }
    int w = gfx_text_width(&font_large, s_head) + 28;
    gfx_fill(g, 0, 0, w, 26, C_PANEL);
    gfx_fill(g, w, 0, 1, 26, C_LINE);
    gfx_fill(g, 0, 26, w + 1, 1, C_LINE);
    gfx_fill(g, 8, 9, 8, 8, acc);
    gfx_text(g, &font_large, 21, 4, s_head, C_TEXT);
}

void viz_draw(gfx_t *g)
{
    framebuffer_2 = g->px;
    monitor_decay();
    int m = mode();
    uint32_t frame = ui_frame2();
    /* fresh: a different visualiser, or screen 2 was showing something else in between */
    bool fresh = m != s_drawn || frame != s_last_frame + 1;
    if (fresh && m != s_drawn) {
        viz_clear(C_BG);
    }
    s_drawn = m;
    s_last_frame = frame;
    int o = g_settings.viz_opt[m] % s_viz[m].nopt;
    int ncol = ncol_of(&s_viz[m]);
    int c = g_settings.viz_col[m] % (ncol + 1);
    if (m == 0 && c == 7) {
        c = 0;   /* Piano saved as White before 2.1: show Green */
    }
    if (c == ncol && s_viz[m].colour != CK_NONE) {
        c = cycle_frame(m);
    } else {
        s_cyc_m = -1;   /* next time Cycle starts, its own "Cycle" heading stays up */
    }
    s_viz[m].draw(c, o, fresh);
    if (esp_timer_get_time() < s_head_until) {
        draw_heading(g);
    }
}
