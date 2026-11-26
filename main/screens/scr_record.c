/*
 * Record into the selected track.
 * Ready:     2/3 pick a setting, 5/6 change it, 8 start, 4 back.
 * Recording: 4 discard, 5 click on/off, 6 overdub/replace, 7 undo last loop pass, 8 keep.
 */
#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "monitor.h"
#include "song.h"
#include "seq.h"
#include "theme.h"
#include "widgets.h"
#include "dialog.h"
#include "app.h"

enum { F_LOOP, F_FROM, F_LEN, F_COUNTIN, F_CLICK, F_MODE, F_COUNT };
static const char *const s_fields[F_COUNT] = { "Loop", "Start bar", "Loop length", "Count-in", "Metronome", "Mode" };

static int s_field;
static bool s_loop;
static int s_from_bar;    /* 0-based */
static int s_len_bars = 4;
static bool s_overdub = true;
static uint32_t s_last;

static bool active(void)
{
    seq_status_t st;
    seq_get(&st);
    return st.state == SEQ_RECORDING || st.state == SEQ_COUNTIN;
}

static void enter(void *arg)
{
    (void)arg;
    s_field = 0;
    s_from_bar = (int)(g_studio_playhead / song_bar_ticks());
}

static void finish(bool keep)
{
    seq_stop();
    if (keep) {
        seq_keep();
    } else {
        seq_discard();
    }
}

static void leave(void)
{
    seq_status_t st;
    seq_get(&st);
    if (st.state != SEQ_STOPPED || st.take_pending) {
        finish(true);   /* leaving by any route keeps what you played */
    }
}

static void value_text(int f, char *o, int n)
{
    switch (f) {
    case F_LOOP: snprintf(o, (size_t)n, "%s", s_loop ? "On" : "Off"); break;
    case F_FROM: snprintf(o, (size_t)n, "%d", s_from_bar + 1); break;
    case F_LEN: snprintf(o, (size_t)n, "%d bar%s", s_len_bars, s_len_bars == 1 ? "" : "s"); break;
    case F_COUNTIN: snprintf(o, (size_t)n, g_settings.countin ? "%u bar%s" : "Off", g_settings.countin, g_settings.countin == 1 ? "" : "s"); break;
    case F_CLICK: snprintf(o, (size_t)n, "%s", g_settings.click ? "On" : "Off"); break;
    default: snprintf(o, (size_t)n, "%s", s_overdub ? "Overdub (add)" : "Replace"); break;
    }
}

static void change(int d)
{
    switch (s_field) {
    case F_LOOP: s_loop = !s_loop; break;
    case F_FROM: s_from_bar = s_from_bar + d < 0 ? 0 : (s_from_bar + d > 998 ? 998 : s_from_bar + d); break;
    case F_LEN: s_len_bars = s_len_bars + d < 1 ? 1 : (s_len_bars + d > 32 ? 32 : s_len_bars + d); break;
    case F_COUNTIN: g_settings.countin = (uint8_t)((g_settings.countin + 3 + (d > 0 ? 1 : -1)) % 3); settings_changed(); break;
    case F_CLICK: g_settings.click = !g_settings.click; seq_set_click(g_settings.click); settings_changed(); break;
    default: s_overdub = !s_overdub; break;
    }
}

static void start(void)
{
    uint32_t bt = song_bar_ticks();
    seq_rec_t r = {
        .loop = s_loop,
        .loop_start = (uint32_t)s_from_bar * bt,
        .loop_end = (uint32_t)(s_from_bar + s_len_bars) * bt,
        .start = (uint32_t)s_from_bar * bt,
        .overdub = s_overdub,
        .countin_bars = g_settings.countin,
    };
    seq_set_click(g_settings.click);
    seq_record(&r);
}

static bool key(const btn_event_t *e)
{
    if (active()) {
        if (e->repeat) {
            return true;
        }
        switch (e->key) {
        case K_S1: {
            seq_status_t st;
            seq_get(&st);
            finish(false);
            ui_toast(st.take_notes ? "Take discarded" : "Stopped");
            return true;
        }
        case K_S2:
            g_settings.click = !g_settings.click;
            seq_set_click(g_settings.click);
            settings_changed();
            return true;
        case K_S3:
            s_overdub = !s_overdub;
            seq_set_overdub(s_overdub);
            return true;
        case K_S4:
            seq_undo_round();
            ui_toast(s_loop ? "Last pass removed" : "Take cleared");
            return true;
        case K_S5: {
            finish(true);
            seq_status_t st;
            seq_get(&st);
            g_studio_playhead = (uint32_t)s_from_bar * song_bar_ticks();
            ui_toast("Kept");
            ui_pop();
            return true;
        }
        default:
            return false;
        }
    }
    switch (e->key) {
    case K_UP: s_field = (s_field + F_COUNT - 1) % F_COUNT; return true;
    case K_DOWN: s_field = (s_field + 1) % F_COUNT; return true;
    case K_S2: change(e->shift ? -4 : -1); return true;
    case K_S3: change(e->shift ? 4 : 1); return true;
    case K_S1:
        if (!e->repeat) {
            ui_pop();
        }
        return true;
    case K_S5:
        if (!e->repeat) {
            start();
        }
        return true;
    default:
        return false;
    }
}

static void tick(uint32_t now)
{
    if (now - s_last > 40) {
        s_last = now;
        ui_invalidate();
    }
}

static void draw_ready(gfx_t *g)
{
    const strack_t *tr = &g_song.tr[g_studio_track];
    char right[48];
    snprintf(right, sizeof(right), "%s " UI_MIDDOT " ch %d", tr->name, tr->channel + 1);
    w_status(g, "Record", right, C_ACC);
    for (int f = 0; f < F_COUNT; f++) {
        int y = CONTENT_Y + 2 + f * ROW_H;
        bool sel = f == s_field;
        bool off = !s_loop && f == F_LEN;
        w_row(g, y, ROW_H, sel);
        gfx_text(g, &font_main, 12, y + 4, s_fields[f], off ? C_DIM : (sel ? C_ACC : C_TEXT));
        char v[32];
        value_text(f, v, sizeof(v));
        gfx_text_right(g, &font_main, g->w - 12, y + 4, off ? "-" : v, sel ? C_ACC : C_DIM);
    }
    gfx_text_box(g, &font_small, 12, 160, g->w - 24, ALIGN_LEFT, "Play your keyboard to hear this track's sound.", C_DIM);
    gfx_text_box(g, &font_small, 12, 174, g->w - 24, ALIGN_LEFT,
                 s_loop ? "Each pass round the loop adds to the last (7 takes one back)." : "Records until you press Keep or Discard.", C_DIM);
    /* input level */
    int lvl = 0;
    for (int c = 0; c < 16; c++) {
        if (g_mon.level[c] > lvl) {
            lvl = g_mon.level[c];
        }
    }
    gfx_text(g, &font_small, 12, 194, "Input", C_DIM);
    w_progress(g, 50, 197, 258, 6, (uint32_t)lvl, 127, C_ACC);
    static const char *const keys[5] = { "Back", "-", "+", NULL, "@rec" };
    w_softkeys(g, keys, false);
}

static void draw_rec(gfx_t *g, const seq_status_t *st)
{
    const strack_t *tr = &g_song.tr[g_studio_track];
    char right[48];
    snprintf(right, sizeof(right), "track %d " UI_MIDDOT " %s", g_studio_track + 1, tr->name);
    bool counting = st->state == SEQ_COUNTIN;
    w_status(g, counting ? "Count-in" : "Recording", right, C_REC);
    gfx_circle(g, 14, 40, 6, C_REC);
    gfx_text(g, &font_large, 26, 32, counting ? "GET READY" : "REC", C_REC);
    char loop[40];
    if (s_loop) {
        snprintf(loop, sizeof(loop), "loop bars %d" UI_ENDASH "%d " UI_MIDDOT " pass %u", s_from_bar + 1, s_from_bar + s_len_bars, st->round);
    } else {
        snprintf(loop, sizeof(loop), "from bar %d", s_from_bar + 1);
    }
    gfx_text_right(g, &font_small, g->w - 6, 34, loop, C_DIM);

    uint32_t bt = song_bar_ticks();
    int32_t t = st->tick;
    int bar, beat;
    if (t >= 0) {
        bar = t / (int32_t)bt + 1;
        beat = (t % (int32_t)bt) / SONG_PPQ + 1;
    } else {
        int32_t back = -t - 1;
        bar = 0;
        beat = g_song.beats - (int)((back % (int32_t)bt) / SONG_PPQ);
    }
    char pos[16];
    if (counting) {
        snprintf(pos, sizeof(pos), "-%d", g_song.beats - beat + 1);
    } else {
        snprintf(pos, sizeof(pos), "%d.%d", bar, beat);
    }
    gfx_text(g, &font_huge, 6, 54, pos, C_TEXT);
    gfx_text(g, &font_small, 6, 88, counting ? "beats to go" : "bar . beat", C_DIM);
    int nb = g_song.beats > 7 ? 7 : g_song.beats;
    int bw = (int)(144 / nb) - 4;
    for (int b = 0; b < nb; b++) {
        int x = 170 + b * (bw + 4);
        bool on = b == beat - 1;
        gfx_fill(g, x, 56, bw, 32, on ? C_ACC : C_PANEL);
        gfx_rect(g, x, 56, bw, 32, b == 0 ? C_ACC : C_LINE);
        char s[4];
        snprintf(s, sizeof(s), "%d", b + 1);
        gfx_text_box(g, &font_main, x, 65, bw, ALIGN_CENTER, s, on ? C_BG : C_DIM);
    }
    char notes[12];
    snprintf(notes, sizeof(notes), "%d", st->take_notes);
    char ci[12];
    snprintf(ci, sizeof(ci), g_settings.countin ? "%u bar" : "Off", g_settings.countin);
    w_stat(g, 6, 106, 74, "Notes", notes, C_TEXT);
    w_stat(g, 84, 106, 74, "Click", g_settings.click ? "On" : "Off", C_TEXT);
    w_stat(g, 162, 106, 74, "Count-in", ci, C_TEXT);
    w_stat(g, 240, 106, 74, "Mode", s_overdub ? "Overdub" : "Replace", s_overdub ? C_TEXT : C_AMBER);

    int lvl = 0;
    for (int c = 0; c < 16; c++) {
        if (g_mon.level[c] > lvl) {
            lvl = g_mon.level[c];
        }
    }
    gfx_text(g, &font_small, 6, 144, "Input", C_DIM);
    w_progress(g, 6, 158, 308, 6, (uint32_t)lvl, 127, C_ACC);
    if (s_loop) {
        uint32_t ls = (uint32_t)s_from_bar * bt, le = (uint32_t)(s_from_bar + s_len_bars) * bt;
        gfx_text(g, &font_small, 6, 172, "Loop", C_DIM);
        gfx_rect(g, 6, 186, 308, 12, C_LINE);
        if (t >= (int32_t)ls) {
            int w = (int)((int64_t)(t - (int32_t)ls) * 306 / (int32_t)(le - ls));
            gfx_fill(g, 7, 187, w > 306 ? 306 : w, 10, gfx_blend(C_REC, C_BG, 60));
            gfx_fill(g, 7 + (w > 305 ? 305 : w), 184, 2, 16, C_REC);
        }
    }
    const char *keys[5] = { "Discard", "Click", s_overdub ? "Overdub" : "Replace", s_loop ? "Undo" : "Clear", "Keep" };
    w_softkeys(g, keys, false);
}

static void draw(gfx_t *g)
{
    seq_status_t st;
    seq_get(&st);
    if (st.state == SEQ_RECORDING || st.state == SEQ_COUNTIN) {
        draw_rec(g, &st);
    } else {
        draw_ready(g);
    }
}

/* screen 2: the armed track's notes around the recording range */
static void draw2(gfx_t *g)
{
    gfx_clear(g, C_BG);
    seq_status_t st;
    seq_get(&st);
    uint32_t bt = song_bar_ticks();
    uint32_t from = (uint32_t)s_from_bar * bt;
    uint32_t len = (uint32_t)(s_loop ? s_len_bars : 4) * bt;
    if (!s_loop && st.tick > (int32_t)(from + len)) {
        from = (uint32_t)st.tick - len * 3 / 4;   /* scroll along */
    }
    char left[40], right[48];
    snprintf(left, sizeof(left), "Live notes " UI_MIDDOT " %s", g_song.tr[g_studio_track].name);
    snprintf(right, sizeof(right), "bars %lu" UI_ENDASH "%lu", (unsigned long)(from / bt + 1), (unsigned long)((from + len) / bt));
    w_header2(g, left, right, active() ? C_REC : C_ACC);
    const int X = 10, W = 410, Y = 18, H = 118;
    for (uint32_t b = 0; b <= len / bt; b++) {
        gfx_vline(g, X + (int)(b * bt * W / len), Y, H, C_LINE);
    }
    song_lock();
    const strack_t *tr = &g_song.tr[g_studio_track];
    int lo = 127, hi = 0;
    for (int i = 0; i < tr->n; i++) {
        if (tr->ev[i].type == EV_NOTE) {
            lo = tr->ev[i].a < lo ? tr->ev[i].a : lo;
            hi = tr->ev[i].a > hi ? tr->ev[i].a : hi;
        }
    }
    if (lo > hi) {
        lo = 48;
        hi = 72;
    }
    if (hi - lo < 24) {
        lo = (lo + hi) / 2 - 12;
        hi = lo + 24;
    }
    for (int i = ev_first_at(tr, from); i < tr->n && tr->ev[i].tick < from + len; i++) {
        const sev_t *e = &tr->ev[i];
        if (e->type != EV_NOTE) {
            continue;
        }
        int x0 = X + (int)((uint64_t)(e->tick - from) * W / len);
        int w = (int)((uint64_t)e->len * W / len);
        int y = Y + H - 4 - (e->a - lo) * (H - 6) / (hi - lo);
        gfx_fill(g, x0, y, w < 2 ? 2 : w, 4, e->round && e->round == st.round ? C_REC : C_ACC);
    }
    song_unlock();
    if (st.tick >= (int32_t)from && st.state != SEQ_STOPPED) {
        int px = X + (int)((int64_t)(st.tick - (int32_t)from) * W / (int32_t)len);
        if (px < X + W) {
            gfx_fill(g, px, Y, 2, H, C_REC);
        }
    }
}

const screen_t scr_record = {
    .name = "record", .enter = enter, .leave = leave, .key = key, .tick = tick, .draw = draw, .draw2 = draw2,
};
