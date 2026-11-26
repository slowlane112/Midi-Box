/*
 * Edit: piano roll of the selected track with a bar selection.
 * 9/11 move the selection (Shift: change its length), 10 opens Notes (single notes),
 * Shift+10 selects the whole track,
 * 2/3 scroll notes (Shift: an octave).
 * 5 copy  6 paste  7 quantise  8 transpose       4 back
 * Shift: 5 cut  6 delete  7 velocity  8 duplicate   Shift+4 undo
 */
#include <stdio.h>
#include <string.h>
#include "midi_out.h"
#include "song.h"
#include "theme.h"
#include "widgets.h"
#include "dialog.h"
#include "app.h"

#define KEY_W     14
#define ROWS      25
#define ROW_PX    8
#define VIEW_BARS 4

static int s_sel_bar, s_sel_len = 1;
static int s_view_bar;
static int s_low;            /* lowest note shown */
static int s_semis, s_vel;

static uint32_t sel_t0(void) { return (uint32_t)s_sel_bar * song_bar_ticks(); }
static uint32_t sel_t1(void) { return (uint32_t)(s_sel_bar + s_sel_len) * song_bar_ticks(); }
static strack_t *track(void) { return &g_song.tr[g_studio_track]; }

static void fit_notes(void)
{
    const strack_t *tr = track();
    int lo = 127, hi = 0;
    for (int i = 0; i < tr->n; i++) {
        if (tr->ev[i].type == EV_NOTE) {
            lo = tr->ev[i].a < lo ? tr->ev[i].a : lo;
            hi = tr->ev[i].a > hi ? tr->ev[i].a : hi;
        }
    }
    s_low = lo > hi ? 48 : (lo + hi) / 2 - ROWS / 2;
    s_low = s_low < 0 ? 0 : (s_low > 127 - ROWS ? 127 - ROWS : s_low);
}

static void enter(void *arg)
{
    (void)arg;
    s_sel_bar = (int)(g_studio_playhead / song_bar_ticks());
    s_sel_len = 1;
    s_view_bar = s_sel_bar;
    song_lock();
    fit_notes();
    song_unlock();
}

static void follow(void)
{
    if (s_sel_bar < s_view_bar) {
        s_view_bar = s_sel_bar;
    }
    if (s_sel_bar + s_sel_len > s_view_bar + VIEW_BARS) {
        s_view_bar = s_sel_bar + s_sel_len - VIEW_BARS;
    }
    if (s_view_bar < 0) {
        s_view_bar = 0;
    }
}

/* ------------------------------------------------------------------ value dialogs */

static void tr_fmt(char *o, int n) { snprintf(o, (size_t)n, "%+d semitones", s_semis); }
static void tr_change(int d) { s_semis += d > 1 ? 12 : (d < -1 ? -12 : d); s_semis = s_semis < -48 ? -48 : (s_semis > 48 ? 48 : s_semis); }
static void tr_done(bool ok)
{
    if (ok && s_semis) {
        song_lock();
        edit_transpose(g_studio_track, sel_t0(), sel_t1(), s_semis);
        song_unlock();
    }
}

static void vel_fmt(char *o, int n) { snprintf(o, (size_t)n, "%d%%", s_vel); }
static void vel_change(int d) { s_vel += d * 5; s_vel = s_vel < 10 ? 10 : (s_vel > 200 ? 200 : s_vel); }
static void vel_done(bool ok)
{
    if (ok && s_vel != 100) {
        song_lock();
        edit_velocity(g_studio_track, sel_t0(), sel_t1(), s_vel);
        song_unlock();
    }
}

static void do_delete(void)
{
    song_lock();
    edit_delete(g_studio_track, sel_t0(), sel_t1());
    song_unlock();
}

/* ------------------------------------------------------------------ quantise dialog */

static const struct { const char *name; uint32_t ticks; } s_grids[] = {
    { "1/4", SONG_PPQ }, { "1/8", SONG_PPQ / 2 }, { "1/16", SONG_PPQ / 4 }, { "1/32", SONG_PPQ / 8 },
    { "1/8 triplet", SONG_PPQ / 3 }, { "1/16 triplet", SONG_PPQ / 6 },
};
#define NGRIDS ((int)(sizeof(s_grids) / sizeof(s_grids[0])))
static int s_q_grid = 2, s_q_field;
static uint8_t s_q_strength = 100, s_q_swing;
static bool s_q_ends;

static quant_t q_now(void)
{
    return (quant_t){ s_grids[s_q_grid].ticks, s_q_strength, s_q_swing, s_q_ends };
}

static void q_draw(gfx_t *g)
{
    ui_draw_below(g);
    int x = 56, y = 34, w = 208, h = 150;
    w_panel(g, x, y, w, h, C_ACC);
    gfx_text(g, &font_main, x + 8, y + 6, "Quantise selection", C_ACC);
    char vals[4][20];
    snprintf(vals[0], 20, "%s", s_grids[s_q_grid].name);
    snprintf(vals[1], 20, "%u%%", s_q_strength);
    snprintf(vals[2], 20, "%u%%", s_q_swing);
    snprintf(vals[3], 20, "%s", s_q_ends ? "Quantise" : "Keep");
    static const char *const names[4] = { "Grid", "Strength", "Swing", "Note ends" };
    for (int i = 0; i < 4; i++) {
        int yy = y + 26 + i * 20;
        bool sel = i == s_q_field;
        if (sel) {
            gfx_fill(g, x + 4, yy - 2, w - 8, 18, C_ACC_BG);
        }
        gfx_text(g, &font_main, x + 10, yy, names[i], C_TEXT);
        char v[96];
        snprintf(v, sizeof(v), sel ? "< %s >" : "%s", vals[i]);
        gfx_text_right(g, &font_main, x + w - 10, yy, v, sel ? C_ACC : C_TEXT);
    }
    song_lock();
    quant_t q = q_now();
    int moved = 0;
    uint32_t worst = edit_quantise_preview(g_studio_track, sel_t0(), sel_t1(), &q, &moved);
    song_unlock();
    char info[48];
    uint32_t ms = (uint32_t)((uint64_t)worst * 60000 / SONG_PPQ / (g_song.bpm ? g_song.bpm : 120));
    snprintf(info, sizeof(info), "%d note%s move, at most %lu ms", moved, moved == 1 ? "" : "s", (unsigned long)ms);
    gfx_text_box(g, &font_small, x + 10, y + h - 20, w - 20, ALIGN_LEFT, info, C_DIM);
    static const char *const keys[5] = { "Cancel", "-", "+", "Next", "Apply" };
    w_softkeys(g, keys, false);
}

static void q_change(int d)
{
    switch (s_q_field) {
    case 0: s_q_grid = (s_q_grid + NGRIDS + (d > 0 ? 1 : -1)) % NGRIDS; break;
    case 1: { int v = s_q_strength + d * 10; s_q_strength = (uint8_t)(v < 10 ? 10 : (v > 100 ? 100 : v)); break; }
    case 2: { int v = s_q_swing + d * 5; s_q_swing = (uint8_t)(v < 0 ? 0 : (v > 75 ? 75 : v)); break; }
    default: s_q_ends = !s_q_ends; break;
    }
}

static bool q_key(const btn_event_t *e)
{
    switch (e->key) {
    case K_UP: s_q_field = (s_q_field + 3) % 4; return true;
    case K_DOWN:
    case K_S4: if (!e->repeat || e->key == K_DOWN) { s_q_field = (s_q_field + 1) % 4; } return true;
    case K_S2: q_change(-1); return true;
    case K_S3: q_change(1); return true;
    case K_S1: if (!e->repeat) { ui_pop(); } return true;
    case K_S5:
        if (!e->repeat) {
            quant_t q = q_now();
            song_lock();
            edit_quantise(g_studio_track, sel_t0(), sel_t1(), &q);
            song_unlock();
            ui_pop();
            ui_toast("Quantised to %s", s_grids[s_q_grid].name);
        }
        return true;
    default:
        return false;
    }
}

static const screen_t s_quant = { .name = "quantise", .key = q_key, .draw = q_draw, .draw2 = ui_draw2_below };

/* ------------------------------------------------------------------ keys */

static bool key(const btn_event_t *e)
{
    int nbars = g_song.bars;
    switch (e->key) {
    case K_UP: s_low += e->shift ? 12 : 1; s_low = s_low > 127 - ROWS ? 127 - ROWS : s_low; return true;
    case K_DOWN: s_low -= e->shift ? 12 : 1; s_low = s_low < 0 ? 0 : s_low; return true;
    case K_VIZ_L:
        if (e->shift) {
            s_sel_len = s_sel_len > 1 ? s_sel_len - 1 : 1;
        } else if (s_sel_bar > 0) {
            s_sel_bar--;
        }
        follow();
        return true;
    case K_VIZ_R:
        if (e->shift) {
            s_sel_len = s_sel_len < 64 ? s_sel_len + 1 : 64;
        } else if (s_sel_bar < nbars + 32) {
            s_sel_bar++;
        }
        follow();
        return true;
    case K_VIZ_M:
        if (!e->repeat && !e->shift) {
            static uint32_t from;
            from = sel_t0();                 /* Notes starts at the selected bar */
            ui_push(&scr_notes, &from);
        } else if (!e->repeat) {
            s_sel_bar = 0;
            s_sel_len = nbars > 0 ? nbars : 1;
            s_view_bar = 0;
            ui_toast("Whole track selected");
        }
        return true;
    default:
        break;
    }
    if (e->repeat) {
        return true;
    }
    int t = g_studio_track;
    switch (e->key) {
    case K_S1:
        if (e->shift) {
            song_lock();
            bool can = song_can_undo();
            if (can) {
                song_undo();
            }
            song_unlock();
            ui_toast(can ? "Undone (again to redo)" : "Nothing to undo");
        } else {
            ui_pop();
        }
        return true;
    case K_S2:
        song_lock();
        if (e->shift) {
            edit_cut(t, sel_t0(), sel_t1());
        } else {
            edit_copy(t, sel_t0(), sel_t1());
        }
        int n = g_clip.n;
        song_unlock();
        ui_toast("%s %d event%s", e->shift ? "Cut" : "Copied", n, n == 1 ? "" : "s");
        return true;
    case K_S3:
        if (e->shift) {
            char msg[48];
            song_lock();
            int cnt = edit_count(t, sel_t0(), sel_t1());
            song_unlock();
            snprintf(msg, sizeof(msg), "Delete %d note%s in the selection?", cnt, cnt == 1 ? "" : "s");
            dlg_confirm("Delete", msg, "Delete", do_delete);
        } else if (g_clip.n == 0) {
            ui_toast("Clipboard is empty");
        } else {
            song_lock();
            edit_paste(t, sel_t0());
            song_unlock();
            ui_toast("Pasted at bar %d", s_sel_bar + 1);
        }
        return true;
    case K_S4:
        if (e->shift) {
            s_vel = 100;
            static const dlg_value_t v = { "Velocity", "Louder or softer, keeps the feel", vel_fmt, vel_change, vel_done };
            dlg_value(&v);
        } else {
            s_q_field = 0;
            ui_push(&s_quant, NULL);
        }
        return true;
    case K_S5:
        if (e->shift) {
            song_lock();
            edit_duplicate(t, sel_t0(), sel_t1());
            song_unlock();
            s_sel_bar += s_sel_len;
            follow();
            ui_toast("Duplicated");
        } else {
            s_semis = 0;
            static const dlg_value_t v = { "Transpose selection", "Shift + / - steps an octave", tr_fmt, tr_change, tr_done };
            dlg_value(&v);
        }
        return true;
    default:
        return false;
    }
}

/* ------------------------------------------------------------------ draw */

static bool is_black(int n)
{
    int k = n % 12;
    return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
}

static void draw(gfx_t *g)
{
    const strack_t *tr = track();
    char right[64];
    if (s_sel_len == 1) {
        snprintf(right, sizeof(right), "%s " UI_MIDDOT " bar %d selected", tr->name, s_sel_bar + 1);
    } else {
        snprintf(right, sizeof(right), "%s " UI_MIDDOT " bars %d" UI_ENDASH "%d", tr->name, s_sel_bar + 1, s_sel_bar + s_sel_len);
    }
    w_status(g, "Edit", right, C_ACC);

    uint32_t bt = song_bar_ticks();
    int px_w = g->w - KEY_W - 1;
    uint32_t v0 = (uint32_t)s_view_bar * bt, vlen = VIEW_BARS * bt;
    for (int r = 0; r < ROWS; r++) {
        int note = s_low + ROWS - 1 - r;
        int y = CONTENT_Y + r * ROW_PX;
        bool blk = is_black(note);
        gfx_fill(g, 0, y, KEY_W, ROW_PX - 1, blk ? C_KEY_B : C_KEY_W);
        if (note % 12 == 0) {
            char c[12];
            snprintf(c, sizeof(c), "%d", note / 12 - 1);
            gfx_text(g, &font_small, 3, y - 3, c, C_BG);
        }
        gfx_fill(g, KEY_W + 1, y, px_w, ROW_PX, blk ? RGB(0x0E, 0x11, 0x15) : C_BG);
    }
    for (int b = 0; b <= VIEW_BARS; b++) {
        int x = KEY_W + 1 + b * px_w / VIEW_BARS;
        gfx_vline(g, x, CONTENT_Y, ROWS * ROW_PX, C_LINE);
        if (b < VIEW_BARS) {
            char s[12];
            snprintf(s, sizeof(s), "%d", s_view_bar + b + 1);
            gfx_text(g, &font_small, x + 2, CONTENT_Y + ROWS * ROW_PX - 12, s, C_DIM);
        }
    }
    /* selection */
    int sx0 = KEY_W + 1 + (s_sel_bar - s_view_bar) * px_w / VIEW_BARS;
    int sx1 = KEY_W + 1 + (s_sel_bar + s_sel_len - s_view_bar) * px_w / VIEW_BARS;
    if (sx1 > g->w) {
        sx1 = g->w;
    }
    gfx_rect(g, sx0, CONTENT_Y, sx1 - sx0, ROWS * ROW_PX, C_ACC);

    song_lock();
    for (int i = ev_first_at(tr, v0 > bt * 8 ? v0 - bt * 8 : 0); i < tr->n && tr->ev[i].tick < v0 + vlen; i++) {
        const sev_t *e = &tr->ev[i];
        if (e->type != EV_NOTE || e->tick + e->len <= v0) {
            continue;
        }
        int r = s_low + ROWS - 1 - e->a;
        if (r < 0 || r >= ROWS) {
            continue;
        }
        int x0 = KEY_W + 1 + (int)((int64_t)((int64_t)e->tick - v0) * px_w / vlen);
        int x1 = KEY_W + 1 + (int)((int64_t)((int64_t)e->tick + e->len - v0) * px_w / vlen);
        x0 = x0 < KEY_W + 1 ? KEY_W + 1 : x0;
        bool in = e->tick >= sel_t0() && e->tick < sel_t1();
        gfx_fill(g, x0, CONTENT_Y + r * ROW_PX + 1, x1 - x0 > 2 ? x1 - x0 - 1 : 2, ROW_PX - 2, in ? C_ACC : C_GREY);
    }
    song_unlock();

    static const char *const normal[5] = { "Back", "Copy", "Paste", "Quant", "Transp" };
    static const char *const shifted[5] = { "Undo", "Cut", "Delete", "Velocity", "Dupl" };
    w_softkeys2(g, normal, shifted);
}

/* screen 2: the whole track with the selection and clipboard */
static void draw2(gfx_t *g)
{
    gfx_clear(g, C_BG);
    const strack_t *tr = track();
    uint32_t bt = song_bar_ticks();
    uint32_t total = (uint32_t)g_song.bars * bt;
    if (!total) {
        total = bt;
    }
    char left[64], right[48];
    snprintf(left, sizeof(left), "Track %d " UI_MIDDOT " %s " UI_MIDDOT " %u bars", g_studio_track + 1, tr->name, g_song.bars);
    if (g_clip.n) {
        snprintf(right, sizeof(right), "Clipboard: %lu bar%s", (unsigned long)(g_clip.len_ticks / bt), g_clip.len_ticks / bt == 1 ? "" : "s");
    } else {
        snprintf(right, sizeof(right), "Clipboard empty");
    }
    w_header2(g, left, right, C_ACC);
    const int X = 6, W = 416, Y = 30, H = 104;
    for (uint32_t b = 0; b < g_song.bars; b += 4) {
        int x = X + (int)((uint64_t)b * bt * W / total);
        char s[24];
        snprintf(s, sizeof(s), "%lu", (unsigned long)(b + 1));
        gfx_text(g, &font_small, x, 17, s, C_DIM);
        gfx_vline(g, x, Y, H, C_LINE);
    }
    song_lock();
    for (int i = 0; i < tr->n; i++) {
        const sev_t *e = &tr->ev[i];
        if (e->type != EV_NOTE) {
            continue;
        }
        int x = X + (int)((uint64_t)e->tick * W / total);
        int y = Y + H - 3 - e->a * (H - 3) / 127;
        bool in = e->tick >= sel_t0() && e->tick < sel_t1();
        gfx_fill(g, x, y, 3, 3, in ? C_ACC : C_GREY);
    }
    song_unlock();
    int sx0 = X + (int)((uint64_t)sel_t0() * W / total);
    int sx1 = X + (int)((uint64_t)sel_t1() * W / total);
    gfx_rect(g, sx0, Y - 1, (sx1 - sx0) < 3 ? 3 : sx1 - sx0, H + 2, C_ACC);
}

static void resume(void) { ui_invalidate(); }

const screen_t scr_edit = {
    .name = "edit", .enter = enter, .resume = resume, .key = key, .draw = draw, .draw2 = draw2,
};
