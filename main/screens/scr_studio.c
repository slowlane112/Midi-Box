/*
 * Studio: the song arrangement. Tracks down, bars across; the selected track is
 * the one you hear when you play your keyboard and the one Record writes to.
 *
 * 2/3 track   4 back (Shift save)   5 record (Shift export .mid)   6 play/stop (Shift song menu)
 * 7 edit (Shift track menu)   8 add track (Shift delete track)   9/11 playhead -/+ bar (Shift 4)   10 to start
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "sd.h"
#include "lcd.h"
#include "settings.h"
#include "midi_out.h"
#include "midi_in.h"
#include "monitor.h"
#include "gm.h"
#include "song.h"
#include "seq.h"
#include "theme.h"
#include "widgets.h"
#include "dialog.h"
#include "viz.h"
#include "app.h"

#define HEAD_W   70
#define BAR_W    25
#define VIS_BARS 10
#define ROWS     8
#define ROW      23

int g_studio_track;
uint32_t g_studio_playhead;

static int s_view_bar;      /* first bar on screen */
static uint32_t s_last;
static uint16_t s_old16;
static uint8_t s_old8;

void studio_select_track(int t)
{
    if (t < 0) {
        t = 0;
    }
    if (t >= g_song.ntracks) {
        t = g_song.ntracks - 1;
    }
    g_studio_track = t;
    seq_set_armed(t);
}

static bool playing(void)
{
    seq_status_t st;
    seq_get(&st);
    return st.state != SEQ_STOPPED;
}

/* ------------------------------------------------------------------ enter / leave */

static uint16_t *s_ov;           /* screen-2 overview cache (see draw2) */
static bool s_ov_dirty = true;

static void enter(void *arg)
{
    (void)arg;
    s_ov_dirty = true;
    s_view_bar = 0;
    g_studio_playhead = 0;
    studio_select_track(0);
    monitor_clear();
    midi_out_reset(g_song.bank, g_settings.volume);
    seq_activate(true);
    seq_set_click(g_settings.click);
    seq_send_setup();
    if (!midi_in_running()) {
        midi_in_start(g_settings.din_port);
    }
    viz_set_active(false);
}

static void leave(void)
{
    free(s_ov);
    s_ov = NULL;
    s_ov_dirty = true;
    seq_stop();
    seq_activate(false);
    midi_in_stop();
    midi_out_all_notes_off();
    monitor_clear();
    viz_set_active(false);
}

static void resume(void)
{
    s_ov_dirty = true;
    seq_send_setup();   /* the track menu may have changed instruments */
    ui_invalidate();
}

/* ------------------------------------------------------------------ actions */

static bool save(void)
{
    dlg_busy("Saving" UI_ELLIPSIS, 0, 0);
    song_lock();
    bool ok = song_save(g_project_path);
    song_unlock();
    dlg_busy_end();
    ui_toast(ok ? "Saved" : "Could not save. Is the card full?");
    return ok;
}

static void export_mid(void)
{
    char path[300];
    snprintf(path, sizeof(path), "%s", g_project_path);
    char *dot = strrchr(path, '.');
    if (dot) {
        strcpy(dot, ".mid");
    }
    dlg_busy("Exporting" UI_ELLIPSIS, 0, 0);
    song_lock();
    bool ok = song_export_mid(path);
    song_unlock();
    dlg_busy_end();
    if (ok) {
        ui_toast("Exported %s", path_name(path));
    } else {
        ui_toast("Export failed");
    }
}

static void leave_now(void) { ui_pop(); }

static void add_track(void)
{
    song_lock();
    char name[24];
    snprintf(name, sizeof(name), "Track %d", g_song.ntracks + 1);
    int t = song_track_add(name, song_free_channel(), 0);
    song_unlock();
    if (t < 0) {
        ui_toast("16 tracks is the most");
        return;
    }
    studio_select_track(t);
    seq_send_setup();
}

static void delete_track(void)
{
    seq_stop();
    song_lock();
    song_track_delete(g_studio_track);
    song_unlock();
    studio_select_track(g_studio_track);
}

static void move_playhead(int bars)
{
    int64_t t = (int64_t)g_studio_playhead + (int64_t)bars * song_bar_ticks();
    if (t < 0) {
        t = 0;
    }
    g_studio_playhead = (uint32_t)t / song_bar_ticks() * song_bar_ticks();
}

/* --- song menu (Shift+6) --- */

static void bpm_fmt(char *o, int n) { snprintf(o, (size_t)n, "%u bpm", g_song.bpm); }
static void bpm_change(int d)
{
    int v = g_song.bpm + d;
    g_song.bpm = (uint16_t)(v < 30 ? 30 : (v > 260 ? 260 : v));
    g_song.dirty = true;
}
static void bpm_done(bool ok) { if (!ok) { g_song.bpm = s_old16; } }

static void beats_fmt(char *o, int n) { snprintf(o, (size_t)n, "%u/4", g_song.beats); }
static void beats_change(int d)
{
    int v = g_song.beats + (d > 0 ? 1 : -1);
    g_song.beats = (uint8_t)(v < 2 ? 2 : (v > 7 ? 7 : v));
    g_song.dirty = true;
}
static void beats_done(bool ok) { if (!ok) { g_song.beats = s_old8; } }

static void bars_fmt(char *o, int n) { snprintf(o, (size_t)n, "%u bars", g_song.bars); }
static void bars_change(int d)
{
    int v = g_song.bars + d;
    uint32_t min = (song_end_tick() + song_bar_ticks() - 1) / song_bar_ticks();
    g_song.bars = (uint16_t)(v < (int)min ? (int)min : (v > 999 ? 999 : (v < 1 ? 1 : v)));
    g_song.dirty = true;
}
static void bars_done(bool ok) { if (!ok) { g_song.bars = s_old16; } }

static const char *const s_song_menu[] = { "Tempo", "Beats per bar", "Length", "Sound bank", "Undo last change" };

static void on_song_menu(int i)
{
    switch (i) {
    case 0: {
        s_old16 = g_song.bpm;
        static const dlg_value_t v = { "Tempo", "Shift + / - steps 10", bpm_fmt, bpm_change, bpm_done };
        dlg_value(&v);
        break;
    }
    case 1: {
        s_old8 = g_song.beats;
        static const dlg_value_t v = { "Beats per bar", "Quarter-note beats", beats_fmt, beats_change, beats_done };
        dlg_value(&v);
        break;
    }
    case 2: {
        s_old16 = g_song.bars;
        static const dlg_value_t v = { "Song length", "Grows by itself while you record", bars_fmt, bars_change, bars_done };
        dlg_value(&v);
        break;
    }
    case 3:
        g_song.bank = g_song.bank ? BANK_GM : BANK_MT32;
        g_song.dirty = true;
        seq_send_setup();
        ui_toast("Sound bank: %s", g_song.bank ? "Roland MT-32" : "General MIDI");
        break;
    default:
        if (song_can_undo()) {
            song_lock();
            song_undo();
            song_unlock();
            ui_toast("Undone (again to redo)");
        } else {
            ui_toast("Nothing to undo");
        }
        break;
    }
}

/* --- track menu (Shift+7) --- */

static void prog_item(int i, char *o, int n) { snprintf(o, (size_t)n, "%03d  %s", i + 1, gm_program_name((uint8_t)i)); }
static void prog_move(int i) { midi_out_program(g_song.tr[g_studio_track].channel, (uint8_t)i); }
static void prog_pick(int i)
{
    g_song.tr[g_studio_track].program = (uint8_t)i;
    g_song.dirty = true;
}

static void name_done(const char *name)
{
    snprintf(g_song.tr[g_studio_track].name, TRACK_NAME_LEN, "%s", name);
    g_song.dirty = true;
}

static void ch_fmt(char *o, int n)
{
    uint8_t c = g_song.tr[g_studio_track].channel;
    snprintf(o, (size_t)n, "%d%s", c + 1, c == 9 ? " (drums)" : "");
}
static void ch_change(int d)
{
    strack_t *tr = &g_song.tr[g_studio_track];
    tr->channel = (uint8_t)((tr->channel + 16 + (d > 0 ? 1 : -1)) % 16);
    g_song.dirty = true;
}
static void ch_done(bool ok) { if (!ok) { g_song.tr[g_studio_track].channel = s_old8; } }

static void vol_fmt(char *o, int n) { snprintf(o, (size_t)n, "%u", g_song.tr[g_studio_track].volume); }
static void vol_change(int d)
{
    strack_t *tr = &g_song.tr[g_studio_track];
    int v = tr->volume + d * 4;
    tr->volume = (uint8_t)(v < 0 ? 0 : (v > 127 ? 127 : v));
    midi_out_cc(tr->channel, 7, tr->volume);
    g_song.dirty = true;
}
static void vol_done(bool ok)
{
    if (!ok) {
        g_song.tr[g_studio_track].volume = s_old8;
    }
}

static const char *const s_track_menu[] = { "Instrument", "Name", "Channel", "Volume", "Mute", "Solo" };

static void on_track_menu(int i)
{
    strack_t *tr = &g_song.tr[g_studio_track];
    switch (i) {
    case 0: {
        static dlg_list_t l;
        l = (dlg_list_t){ .title = "Instrument", .count = 128, .sel = tr->program, .item = prog_item,
                          .on_pick = prog_pick, .on_move = prog_move };
        dlg_list(&l);
        break;
    }
    case 1:
        dlg_text("Track name", tr->name, name_done);
        break;
    case 2: {
        s_old8 = tr->channel;
        static const dlg_value_t v = { "MIDI channel", "Channel 10 plays drums", ch_fmt, ch_change, ch_done };
        dlg_value(&v);
        break;
    }
    case 3: {
        s_old8 = tr->volume;
        static const dlg_value_t v = { "Track volume", NULL, vol_fmt, vol_change, vol_done };
        dlg_value(&v);
        break;
    }
    case 4:
        tr->flags ^= TRK_MUTE;
        g_song.dirty = true;
        break;
    default: {
        /* one solo at a time: soloing a track un-solos the others; again turns it off */
        bool was = tr->flags & TRK_SOLO;
        for (int i = 0; i < g_song.ntracks; i++) {
            g_song.tr[i].flags &= (uint8_t)~TRK_SOLO;
        }
        if (!was) {
            tr->flags |= TRK_SOLO;
        }
        g_song.dirty = true;
        break;
    }
    }
}

/* ------------------------------------------------------------------ keys */

static bool key(const btn_event_t *e)
{
    s_ov_dirty = true;   /* selection, mutes or edits may change the overview */
    int n = g_song.ntracks;
    if (e->repeat && e->key != K_UP && e->key != K_DOWN && e->key != K_VIZ_L && e->key != K_VIZ_R) {
        return true;
    }
    switch (e->key) {
    case K_UP: studio_select_track((g_studio_track + n - 1) % n); return true;
    case K_DOWN: studio_select_track((g_studio_track + 1) % n); return true;
    case K_S1:
        if (e->shift) {
            save();
        } else if (g_song.dirty) {
            dlg_confirm("Unsaved changes", "Leave without saving? (Shift+4 saves)", "Leave", leave_now);
        } else {
            ui_pop();
        }
        return true;
    case K_S2:
        if (e->shift) {
            export_mid();
        } else {
            seq_stop();
            ui_push(&scr_record, NULL);
        }
        return true;
    case K_S3:
        if (e->shift) {
            dlg_menu(s_song_menu, 5, 0, on_song_menu);
        } else if (playing()) {
            seq_stop();
        } else {
            seq_play(g_studio_playhead);
        }
        return true;
    case K_S4:
        seq_stop();
        if (e->shift) {
            dlg_menu(s_track_menu, 6, 0, on_track_menu);
        } else {
            ui_push(&scr_edit, NULL);
        }
        return true;
    case K_S5:
        if (e->shift) {
            if (n > 1) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Delete track \"%s\" and its notes?", g_song.tr[g_studio_track].name);
                dlg_confirm("Delete track", msg, "Delete", delete_track);
            } else {
                ui_toast("A song needs at least one track");
            }
        } else {
            add_track();
        }
        return true;
    case K_VIZ_L: move_playhead(e->shift ? -4 : -1); return true;
    case K_VIZ_R: move_playhead(e->shift ? 4 : 1); return true;
    case K_VIZ_M:
        if (!e->repeat) {
            g_studio_playhead = 0;
        }
        return true;
    default:
        return false;
    }
}

static void tick(uint32_t now)
{
    seq_status_t st;
    seq_get(&st);
    bool on = st.state != SEQ_STOPPED;
    viz_set_active(on);
    if (on && now - s_last > 50) {
        s_last = now;
        ui_invalidate();
    }
}

/* ------------------------------------------------------------------ draw */

static void bars_with_notes(const strack_t *tr, uint8_t *bars, int first, int count)
{
    memset(bars, 0, (size_t)count);
    uint32_t bt = song_bar_ticks();
    for (int i = 0; i < tr->n; i++) {
        const sev_t *e = &tr->ev[i];
        if (e->type != EV_NOTE) {
            continue;
        }
        int b0 = (int)(e->tick / bt), b1 = (int)((e->tick + e->len - 1) / bt);
        for (int b = b0; b <= b1; b++) {
            if (b >= first && b < first + count) {
                bars[b - first] = (uint8_t)(bars[b - first] < 255 ? bars[b - first] + 1 : 255);
            }
        }
    }
}

static void draw(gfx_t *g)
{
    seq_status_t st;
    seq_get(&st);
    uint32_t bt = song_bar_ticks();
    int32_t pos = st.state != SEQ_STOPPED ? st.tick : (int32_t)g_studio_playhead;
    int play_bar = pos >= 0 ? pos / (int32_t)bt : 0;
    if (play_bar < s_view_bar) {
        s_view_bar = play_bar;
    }
    if (play_bar >= s_view_bar + VIS_BARS) {
        s_view_bar = play_bar - VIS_BARS + 2;
    }

    char right[96];
    snprintf(right, sizeof(right), "%s%s " UI_MIDDOT " %u bpm " UI_MIDDOT " %u/4 " UI_MIDDOT " bar %d",
             g_song.name, g_song.dirty ? "*" : "", g_song.bpm, g_song.beats, play_bar + 1);
    w_status(g, "Studio", right, st.state == SEQ_STOPPED ? C_ACC : C_ACC);

    /* ruler */
    gfx_fill(g, 0, CONTENT_Y, g->w, 12, C_PANEL);
    for (int b = 0; b < VIS_BARS; b++) {
        char s[12];
        snprintf(s, sizeof(s), "%d", s_view_bar + b + 1);
        bool past_end = s_view_bar + b >= g_song.bars;
        gfx_text(g, &font_small, HEAD_W + b * BAR_W + 2, CONTENT_Y, s, past_end ? C_LINE : C_DIM);
    }

    song_lock();
    int n = g_song.ntracks;
    int top = w_list_top(g_studio_track, n, ROWS);
    int y0 = CONTENT_Y + 13;
    for (int r = 0; r < ROWS && top + r < n; r++) {
        int t = top + r, y = y0 + r * ROW;
        const strack_t *tr = &g_song.tr[t];
        bool sel = t == g_studio_track;
        bool sounds = song_track_sounds(t);
        w_row(g, y, ROW - 1, sel);
        gfx_hline(g, 0, y + ROW - 1, g->w, C_PANEL);
        gfx_text_box(g, &font_main, 8, y + 1, HEAD_W - 12, ALIGN_LEFT, tr->name, !sounds ? C_DIM : (sel ? C_ACC : C_TEXT));
        char sub[24];
        snprintf(sub, sizeof(sub), "ch %d%s", tr->channel + 1,
                 (tr->flags & TRK_SOLO) ? " S" : ((tr->flags & TRK_MUTE) ? " M" : (sel ? " " UI_MIDDOT " rec" : "")));
        gfx_text_box(g, &font_small, 8, y + 12, HEAD_W - 10, ALIGN_LEFT, sub,
                     sel ? C_REC : ((tr->flags & (TRK_MUTE | TRK_SOLO)) ? C_AMBER : C_DIM));
        uint8_t bars[VIS_BARS];
        bars_with_notes(tr, bars, s_view_bar, VIS_BARS);
        for (int b = 0; b < VIS_BARS; b++) {
            if (!bars[b]) {
                continue;
            }
            int x = HEAD_W + b * BAR_W;
            bool joins_prev = b > 0 && bars[b - 1];
            bool joins_next = b + 1 < VIS_BARS && bars[b + 1];
            int x0 = joins_prev ? x : x + 1, x1 = joins_next ? x + BAR_W : x + BAR_W - 2;
            uint16_t fill = sounds ? (sel ? gfx_blend(C_ACC, C_BG, 110) : C_ACC_DK) : C_PANEL;
            gfx_fill(g, x0, y + 3, x1 - x0, 16, fill);
            gfx_hline(g, x0, y + 3, x1 - x0, sounds ? C_ACC : C_LINE);
            gfx_hline(g, x0, y + 18, x1 - x0, sounds ? C_ACC : C_LINE);
            if (!joins_prev) {
                gfx_vline(g, x0, y + 3, 16, sounds ? C_ACC : C_LINE);
            }
            if (!joins_next) {
                gfx_vline(g, x1 - 1, y + 3, 16, sounds ? C_ACC : C_LINE);
            }
            int dots = bars[b] > 8 ? 8 : bars[b];   /* density hint */
            for (int k = 0; k < dots; k++) {
                gfx_fill(g, x + 3 + k * 2, y + 8 + (k * 5) % 9, 2, 1, C_TEXT);
            }
        }
    }
    song_unlock();
    gfx_vline(g, HEAD_W - 2, CONTENT_Y, SOFT_Y - CONTENT_Y, C_LINE);
    if (pos >= 0) {
        int px = HEAD_W + (int)((int64_t)(pos - (int32_t)(s_view_bar * bt)) * BAR_W / (int32_t)bt);
        if (px >= HEAD_W && px < g->w) {
            gfx_vline(g, px, CONTENT_Y, SOFT_Y - CONTENT_Y, st.state == SEQ_STOPPED ? C_AMBER : C_ACC);
        }
    }
    static const char *const normal_stop[5] = { "Back", "Rec", "@play", "Edit", "+Track" };
    static const char *const normal_play[5] = { "Back", "Rec", "@stop", "Edit", "+Track" };
    static const char *const shifted[5] = { "Save", "Export", "Song", "Track", "Delete" };
    w_softkeys2(g, st.state == SEQ_STOPPED ? normal_stop : normal_play, shifted);
}

/* screen 2: the song overview, stopped or playing. While playing, a playhead moves through
 * it and each track's light shows it sounding. The note bars are drawn into a cached
 * image (redrawn when stopped or after a key press), so playback never waits for the
 * screen to read the song. */
#define OV_X 14
#define OV_W 410
#define OV_Y 18

static uint32_t ov_total(void)
{
    uint32_t bt = song_bar_ticks();
    uint32_t total = (uint32_t)g_song.bars * bt;
    if (song_end_tick() > total) {
        total = song_end_tick();
    }
    return total ? total : bt;
}

/* row height: fill the screen with few tracks, 7 px each with all 16 */
static int ov_row(int n, int screen_h)
{
    int h = (screen_h - OV_Y - 4) / (n > 0 ? n : 1);
    return h > 18 ? 18 : (h < 7 ? 7 : h);
}

static void ov_bars(gfx_t *g, uint32_t total)
{
    song_lock();
    int n = g_song.ntracks, rh = ov_row(n, g->h);
    for (int t = 0; t < n; t++) {
        const strack_t *tr = &g_song.tr[t];
        int y = OV_Y + t * rh;
        uint16_t c = t == g_studio_track ? C_ACC : (song_track_sounds(t) ? C_GREY : C_PANEL);
        for (int i = 0; i < tr->n; i++) {
            const sev_t *e = &tr->ev[i];
            if (e->type != EV_NOTE) {
                continue;
            }
            int x0 = OV_X + (int)((uint64_t)e->tick * OV_W / total);
            int x1 = OV_X + (int)((uint64_t)(e->tick + e->len) * OV_W / total);
            gfx_fill(g, x0, y, x1 > x0 ? x1 - x0 : 1, rh - 2, c);
        }
    }
    song_unlock();
}

static void draw2(gfx_t *g)
{
    seq_status_t st;
    seq_get(&st);
    bool playing = st.state != SEQ_STOPPED;
    uint32_t bt = song_bar_ticks();
    uint32_t total = ov_total();
    size_t bytes = (size_t)g->w * g->h * sizeof(uint16_t);
    if (!s_ov) {
        s_ov = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    }
    if (!playing || s_ov_dirty || !s_ov) {
        gfx_clear(g, C_BG);
        ov_bars(g, total);
        if (s_ov) {
            memcpy(s_ov, g->px, bytes);
            s_ov_dirty = false;
        }
    } else {
        memcpy(g->px, s_ov, bytes);
    }

    char left[48], right[24];
    snprintf(left, sizeof(left), "Overview " UI_MIDDOT " %d tracks " UI_MIDDOT " %u bars", g_song.ntracks, g_song.bars);
    uint32_t bpm = g_song.bpm ? g_song.bpm : 120;
    uint32_t secs = (uint32_t)((uint64_t)total * 60 / SONG_PPQ / bpm);
    if (playing) {
        uint32_t pos = st.tick > 0 ? (uint32_t)st.tick : 0;
        uint32_t ps = (uint32_t)((uint64_t)pos * 60 / SONG_PPQ / bpm);
        snprintf(right, sizeof(right), "%lu:%02lu / %lu:%02lu", (unsigned long)(ps / 60), (unsigned long)(ps % 60),
                 (unsigned long)(secs / 60), (unsigned long)(secs % 60));
    } else {
        snprintf(right, sizeof(right), "%lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
    }
    w_header2(g, left, right, playing ? (st.state == SEQ_PLAYING ? C_ACC : C_REC) : C_ACC);

    int n = g_song.ntracks, rh = ov_row(n, g->h);
    /* track lights: how loud each track's channel is right now */
    for (int t = 0; t < n; t++) {
        uint8_t lv = g_mon.level[g_song.tr[t].channel & 15];
        uint16_t c = lv ? gfx_blend(t == g_studio_track ? C_ACC : C_TEXT, C_BG, (uint8_t)(60 + lv * 3 / 2)) : C_PANEL;
        gfx_fill(g, 4, OV_Y + t * rh, 6, rh - 2, c);
    }
    int vx0 = OV_X + (int)((uint64_t)s_view_bar * bt * OV_W / total);
    int vx1 = OV_X + (int)((uint64_t)(s_view_bar + VIS_BARS) * bt * OV_W / total);
    if (vx1 > OV_X + OV_W) {
        vx1 = OV_X + OV_W;
    }
    gfx_rect(g, vx0, OV_Y - 2, vx1 - vx0, n * rh + 2, C_AMBER);
    uint32_t head = playing ? (st.tick > 0 ? (uint32_t)st.tick : 0) : g_studio_playhead;
    if (head > total) {
        head = total;
    }
    int px = OV_X + (int)((uint64_t)head * OV_W / total);
    gfx_vline(g, px, OV_Y - 2, n * rh + 2, playing ? C_TEXT : C_AMBER);
    if (playing) {
        gfx_vline(g, px + 1, OV_Y - 2, n * rh + 2, gfx_blend(C_TEXT, C_BG, 90));
    }
}

const screen_t scr_studio = {
    .name = "studio", .enter = enter, .resume = resume, .leave = leave, .key = key, .tick = tick, .draw = draw, .draw2 = draw2,
};
