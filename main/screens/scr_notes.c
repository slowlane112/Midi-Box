/*
 * Notes (Studio > Edit > 10): change single notes, or add them one by one with the buttons.
 *
 * An amber cursor sits on the piano roll. Landing on the start of a note (9 / 11 / 10)
 * picks that note, and the buttons change it; on empty space they set up a new note.
 *
 *   9 / 11   cursor back / forward by the current length     10  next note (Shift: previous)
 *   2 / 3    pitch up / down (Shift: an octave): the picked note, or the new note
 *   5 / 6    shorter / longer                              4  back to Edit
 *   7        picked: delete     empty: rest (move on)       8  picked: next   empty: add and move on
 *   Shift:   4 undo   5 / 6 move the note earlier / later by 1/16   7 velocity   8 add a chord note (stay)
 * A key on your keyboard sets the pitch too. Every change plays the note.
 */
#include <stdio.h>
#include <string.h>
#include "midi_out.h"
#include "song.h"
#include "seq.h"
#include "theme.h"
#include "widgets.h"
#include "ln_ui.h"
#include "app.h"

#define KEY_W     14
#define ROWS      25
#define ROW_PX    8
#define VIEW_BARS 2
#define MOVE_STEP (SONG_PPQ / 4)
#define NEW_VEL   90

static const struct {
    uint16_t ticks;
    const char *name;
} s_lens[] = {
    { 60, "1/32" }, { 120, "1/16" }, { 160, "1/8 T" }, { 240, "1/8" }, { 320, "1/4 T" }, { 360, "1/8." },
    { 480, "1/4" }, { 720, "1/4." }, { 960, "1/2" }, { 1440, "1/2." }, { 1920, "1/1" },
};
#define NLENS ((int)(sizeof(s_lens) / sizeof(s_lens[0])))
#define LEN_QUARTER 6

static uint32_t s_t;          /* cursor, ticks */
static int s_pitch = 60;
static int s_len = LEN_QUARTER;
static bool s_picked;         /* the note starting at s_t with pitch s_pitch is being edited */
static int s_low;             /* lowest piano-roll row */
static uint32_t s_view;       /* first tick shown */
static uint8_t s_sound = 255; /* note sounding for a preview */
static uint32_t s_sound_off;

static volatile uint8_t s_q[16];
static volatile uint8_t s_qh, s_qt;

static strack_t *track(void) { return &g_song.tr[g_studio_track]; }

static void key_hook(uint8_t note)
{
    uint8_t h = s_qh, nh = (uint8_t)((h + 1) % sizeof(s_q));
    if (nh != s_qt) {
        s_q[h] = note;
        s_qh = nh;
    }
}

static void preview(int note, int vel)
{
    uint8_t ch = track()->channel;
    if (s_sound < 128) {
        midi_out_note(ch, s_sound, 0, false);
    }
    s_sound = (uint8_t)note;
    s_sound_off = ui_now() + 350;
    midi_out_note(ch, (uint8_t)note, (uint8_t)vel, true);
}

/* index of the picked note (song locked), or -1 */
static int picked_index(void)
{
    if (!s_picked) {
        return -1;
    }
    const strack_t *tr = track();
    for (int i = ev_first_at(tr, s_t); i < tr->n && tr->ev[i].tick == s_t; i++) {
        if (tr->ev[i].type == EV_NOTE && tr->ev[i].a == s_pitch) {
            return i;
        }
    }
    return -1;
}

static int len_index(uint32_t len)
{
    int best = 0;
    for (int i = 1; i < NLENS; i++) {
        uint32_t d0 = len > s_lens[best].ticks ? len - s_lens[best].ticks : s_lens[best].ticks - len;
        uint32_t d1 = len > s_lens[i].ticks ? len - s_lens[i].ticks : s_lens[i].ticks - len;
        if (d1 < d0) {
            best = i;
        }
    }
    return best;
}

static void keep_in_view(void)
{
    uint32_t bt = song_bar_ticks(), span = VIEW_BARS * bt;
    if (s_t < s_view || s_t >= s_view + span) {
        s_view = (s_t / bt) * bt;
        if (s_view >= bt && s_t - s_view < bt / 2) {
            s_view -= bt;   /* show a bit of what came before */
        }
    }
    if (s_pitch < s_low + 2) {
        s_low = s_pitch - 2 < 0 ? 0 : s_pitch - 2;
    } else if (s_pitch > s_low + ROWS - 3) {
        s_low = s_pitch - ROWS + 3 > 127 - ROWS ? 127 - ROWS : s_pitch - ROWS + 3;
    }
}

/* move the cursor to tick t and pick a note that starts there (song locked) */
static void land(uint32_t t)
{
    s_t = t;
    s_picked = false;
    const strack_t *tr = track();
    int any = -1;
    for (int i = ev_first_at(tr, t); i < tr->n && tr->ev[i].tick == t; i++) {
        if (tr->ev[i].type != EV_NOTE) {
            continue;
        }
        if (tr->ev[i].a == s_pitch) {
            any = i;
            break;
        }
        if (any < 0 || tr->ev[i].a > tr->ev[any].a) {
            any = i;   /* the top note of a chord */
        }
    }
    if (any >= 0) {
        s_picked = true;
        s_pitch = tr->ev[any].a;
        s_len = len_index(tr->ev[any].len);
        preview(s_pitch, tr->ev[any].b);
    }
    keep_in_view();
}

/* next / previous note start, chord notes one by one (song locked) */
static void step_note(int dir)
{
    const strack_t *tr = track();
    int cur = picked_index();
    int i;
    if (dir > 0) {
        i = cur >= 0 ? cur + 1 : ev_first_at(tr, s_t);
        while (i < tr->n && (tr->ev[i].type != EV_NOTE || (cur < 0 && tr->ev[i].tick < s_t))) {
            i++;
        }
    } else {
        i = cur >= 0 ? cur - 1 : ev_first_at(tr, s_t) - 1;
        while (i >= 0 && tr->ev[i].type != EV_NOTE) {
            i--;
        }
    }
    if (i < 0 || i >= tr->n) {
        ui_toast(dir > 0 ? "No more notes" : "This is the first note");
        return;
    }
    s_t = tr->ev[i].tick;
    s_pitch = tr->ev[i].a;
    s_picked = true;
    s_len = len_index(tr->ev[i].len);
    preview(s_pitch, tr->ev[i].b);
    keep_in_view();
}

static void changed(void)
{
    g_song.dirty = true;
    song_fit_length();
}

static void set_pitch(int p)
{
    p = p < 0 ? 0 : (p > 127 ? 127 : p);
    song_lock();
    int i = picked_index();
    if (i >= 0) {
        song_save_undo(g_studio_track);
        track()->ev[i].a = (uint8_t)p;
        changed();
        preview(p, track()->ev[i].b);
    } else {
        preview(p, NEW_VEL);
    }
    s_pitch = p;
    song_unlock();
    keep_in_view();
}

static void set_len(int dir)
{
    int l = s_len + dir;
    if (l < 0 || l >= NLENS) {
        return;
    }
    s_len = l;
    song_lock();
    int i = picked_index();
    if (i >= 0) {
        song_save_undo(g_studio_track);
        track()->ev[i].len = s_lens[l].ticks;
        changed();
        preview(s_pitch, track()->ev[i].b);
    }
    song_unlock();
}

static void add_note(bool stay)
{
    sev_t e = { .tick = s_t, .len = s_lens[s_len].ticks, .type = EV_NOTE, .a = (uint8_t)s_pitch, .b = NEW_VEL };
    song_lock();
    song_save_undo(g_studio_track);
    ev_insert(track(), &e);
    changed();
    preview(s_pitch, NEW_VEL);
    if (stay) {
        s_picked = false;   /* free: 2/3 now choose the next chord note */
    } else {
        land(s_t + e.len);
    }
    song_unlock();
}

static void move_note(int dir)
{
    song_lock();
    int i = picked_index();
    if (i >= 0) {
        sev_t e = track()->ev[i];
        if (dir < 0 && e.tick < MOVE_STEP) {
            song_unlock();
            return;
        }
        song_save_undo(g_studio_track);
        ev_remove(track(), i);
        e.tick = dir < 0 ? e.tick - MOVE_STEP : e.tick + MOVE_STEP;
        ev_insert(track(), &e);
        s_t = e.tick;
        changed();
        preview(e.a, e.b);
    }
    song_unlock();
    keep_in_view();
}

static void velocity(void)
{
    static const uint8_t v[] = { 40, 64, 90, 110, 127 };
    song_lock();
    int i = picked_index();
    if (i >= 0) {
        uint8_t cur = track()->ev[i].b;
        int k = 0;
        while (k < 5 && v[k] <= cur) {
            k++;
        }
        song_save_undo(g_studio_track);
        track()->ev[i].b = v[k % 5];
        changed();
        preview(s_pitch, v[k % 5]);
        ui_toast("Velocity %d", v[k % 5]);
    }
    song_unlock();
}

/* arg: uint32_t * start tick, or NULL for the Studio playhead */
static void enter(void *arg)
{
    s_qh = s_qt = 0;
    seq_set_key_hook(key_hook);
    uint32_t bt = song_bar_ticks();
    song_lock();
    uint32_t t = ((arg ? *(const uint32_t *)arg : g_studio_playhead) / bt) * bt;
    const strack_t *tr = track();
    int i = ev_first_at(tr, t);
    while (i < tr->n && tr->ev[i].type != EV_NOTE) {
        i++;
    }
    if (i < tr->n && tr->ev[i].tick < t + bt) {
        s_pitch = tr->ev[i].a;   /* start on the first note of the bar */
        t = tr->ev[i].tick;
    }
    s_low = s_pitch - ROWS / 2 < 0 ? 0 : s_pitch - ROWS / 2;
    s_view = (t / bt) * bt;
    land(t);
    song_unlock();
}

static void leave(void)
{
    seq_set_key_hook(NULL);
    if (s_sound < 128) {
        midi_out_note(track()->channel, s_sound, 0, false);
        s_sound = 255;
    }
}

static void tick(uint32_t now)
{
    while (s_qt != s_qh) {
        uint8_t k = s_q[s_qt];
        s_qt = (uint8_t)((s_qt + 1) % sizeof(s_q));
        set_pitch(k);
        ui_invalidate();
    }
    if (s_sound < 128 && (int32_t)(now - s_sound_off) >= 0) {
        midi_out_note(track()->channel, s_sound, 0, false);
        s_sound = 255;
    }
}

static bool key(const btn_event_t *e)
{
    uint32_t step = s_lens[s_len].ticks;
    switch (e->key) {
    case K_UP: set_pitch(s_pitch + (e->shift ? 12 : 1)); return true;
    case K_DOWN: set_pitch(s_pitch - (e->shift ? 12 : 1)); return true;
    case K_VIZ_L:
        song_lock();
        land(s_t >= step ? s_t - step : 0);
        song_unlock();
        return true;
    case K_VIZ_R:
        song_lock();
        land(s_t + step);
        song_unlock();
        return true;
    case K_VIZ_M:
        song_lock();
        step_note(e->shift ? -1 : 1);
        song_unlock();
        return true;
    case K_S2:
        if (e->shift) move_note(-1); else set_len(-1);
        return true;
    case K_S3:
        if (e->shift) move_note(1); else set_len(1);
        return true;
    default:
        break;
    }
    if (e->repeat) {
        return true;
    }
    switch (e->key) {
    case K_S1:
        if (e->shift) {
            song_lock();
            bool can = song_can_undo();
            if (can) {
                song_undo();
                land(s_t);
            }
            song_unlock();
            ui_toast(can ? "Undone" : "Nothing to undo");
        } else {
            ui_pop();
        }
        return true;
    case K_S4:
        if (e->shift) {
            velocity();
        } else if (s_picked) {
            song_lock();
            int i = picked_index();
            if (i >= 0) {
                song_save_undo(g_studio_track);
                ev_remove(track(), i);
                changed();
            }
            s_picked = false;
            song_unlock();
        } else {
            song_lock();
            land(s_t + step);   /* a rest */
            song_unlock();
        }
        return true;
    case K_S5:
        if (e->shift) {
            add_note(true);
        } else if (s_picked) {
            song_lock();
            int i = picked_index();
            land(i >= 0 ? s_t + track()->ev[i].len : s_t + step);   /* on past this note */
            song_unlock();
        } else {
            add_note(false);
        }
        return true;
    default:
        return false;
    }
}

static bool is_black(int n)
{
    int k = n % 12;
    return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
}

static void draw(gfx_t *g)
{
    const strack_t *tr = track();
    uint32_t bt = song_bar_ticks();
    char nm[8], right[64];
    ln_ui_note_name(nm, sizeof(nm), s_pitch);
    uint32_t beat = (s_t % bt) / SONG_PPQ + 1;
    snprintf(right, sizeof(right), "%s%s " UI_MIDDOT " %s " UI_MIDDOT " bar %lu beat %lu", s_picked ? "" : "add ", nm,
             s_lens[s_len].name, (unsigned long)(s_t / bt + 1), (unsigned long)beat);
    w_status(g, "Notes", right, C_ACC);

    int px_w = g->w - KEY_W - 1;
    uint32_t span = VIEW_BARS * bt;
    #define XOF(t) (KEY_W + 1 + (int)(((int64_t)(t) - (int64_t)s_view) * px_w / (int64_t)span))
    for (int r = 0; r < ROWS; r++) {
        int note = s_low + ROWS - 1 - r;
        int y = CONTENT_Y + r * ROW_PX;
        bool blk = is_black(note);
        gfx_fill(g, 0, y, KEY_W, ROW_PX - 1, note == s_pitch ? C_AMBER : (blk ? C_KEY_B : C_KEY_W));
        if (note % 12 == 0) {
            char c[12];
            snprintf(c, sizeof(c), "%d", note / 12 - 1);
            gfx_text(g, &font_small, 3, y - 3, c, C_BG);
        }
        gfx_fill(g, KEY_W + 1, y, px_w, ROW_PX, blk ? RGB(0x0E, 0x11, 0x15) : C_BG);
    }
    for (uint32_t t = s_view; t <= s_view + span; t += SONG_PPQ) {
        int x = XOF(t);
        if ((t % bt) == 0) {
            gfx_vline(g, x, CONTENT_Y, ROWS * ROW_PX, C_LINE);
            if (t < s_view + span) {
                char s[12];
                snprintf(s, sizeof(s), "%lu", (unsigned long)(t / bt + 1));
                gfx_text(g, &font_small, x + 2, CONTENT_Y + ROWS * ROW_PX - 12, s, C_DIM);
            }
        } else {
            for (int y = CONTENT_Y; y < CONTENT_Y + ROWS * ROW_PX; y += 3) {
                gfx_fill(g, x, y, 1, 1, C_LINE);
            }
        }
    }
    song_lock();
    int pi = picked_index();
    for (int i = ev_first_at(tr, s_view > bt * 8 ? s_view - bt * 8 : 0); i < tr->n && tr->ev[i].tick < s_view + span; i++) {
        const sev_t *e = &tr->ev[i];
        if (e->type != EV_NOTE || e->tick + e->len <= s_view) {
            continue;
        }
        int r = s_low + ROWS - 1 - e->a;
        if (r < 0 || r >= ROWS) {
            continue;
        }
        int x0 = XOF(e->tick), x1 = XOF(e->tick + e->len);
        x0 = x0 < KEY_W + 1 ? KEY_W + 1 : x0;
        gfx_fill(g, x0, CONTENT_Y + r * ROW_PX + 1, x1 - x0 > 2 ? x1 - x0 - 1 : 2, ROW_PX - 2, i == pi ? C_AMBER : C_GREY);
    }
    song_unlock();
    int cx = XOF(s_t);
    gfx_vline(g, cx, CONTENT_Y, ROWS * ROW_PX, C_AMBER);
    if (!s_picked) {
        int r = s_low + ROWS - 1 - s_pitch;
        int x1 = XOF(s_t + s_lens[s_len].ticks);
        if (r >= 0 && r < ROWS) {
            gfx_rect(g, cx, CONTENT_Y + r * ROW_PX, (x1 > g->w ? g->w : x1) - cx, ROW_PX, C_AMBER);
        }
    }
    #undef XOF
    const char *normal[5] = { "Back", "Shorter", "Longer", s_picked ? "Delete" : "Rest", s_picked ? "Next" : "Add" };
    static const char *const shifted[5] = { "Undo", "Earlier", "Later", "Velocity", "Chord" };
    w_softkeys2(g, normal, shifted);
}

/* screen 2: the track as notation around the cursor, with note names */
static void draw2(gfx_t *g)
{
    gfx_clear(g, C_BG);
    const strack_t *tr = track();
    uint32_t bt = song_bar_ticks();
    char left[48], right[32];
    snprintf(left, sizeof(left), "Track %d " UI_MIDDOT " %s", g_studio_track + 1, tr->name);
    snprintf(right, sizeof(right), "%u/4 " UI_MIDDOT " bar %lu", g_song.beats, (unsigned long)(s_t / bt + 1));
    w_header2(g, left, right, C_ACC);
    ln_ui_staff(g, 4, g->w - 6, LN_STAFF_LINE, LN_STAFF_LINE);

    song_lock();
    /* start a few note columns before the cursor */
    int start = ev_first_at(tr, s_t);
    for (int cols = 0; start > 0 && cols < 5;) {
        start--;
        if (tr->ev[start].type == EV_NOTE && (start == 0 || tr->ev[start - 1].tick != tr->ev[start].tick)) {
            cols++;
        }
    }
    while (start > 0 && tr->ev[start - 1].tick == tr->ev[start].tick) {
        start--;
    }
    int x = 58, pi = picked_index();
    uint32_t col_t = UINT32_MAX;
    int32_t last_bar = -1;
    bool ghost_done = s_picked;
    for (int i = start; i < tr->n && x < g->w - 14; i++) {
        const sev_t *e = &tr->ev[i];
        if (e->type != EV_NOTE) {
            continue;
        }
        if (!ghost_done && e->tick > s_t) {
            /* the new note goes here */
            if ((int32_t)(s_t / bt) != last_bar && last_bar >= 0) {
                gfx_vline(g, x - 6, ln_ui_staff_y(38), ln_ui_staff_y(18) - ln_ui_staff_y(38) + 1, LN_STAFF_LINE);
                x += 8;
            }
            last_bar = (int32_t)(s_t / bt);
            ln_ui_staff_note(g, x, s_pitch, 0, C_AMBER, false);
            gfx_text(g, &font_small, x - 3, g->h - 12, "?", C_AMBER);
            gfx_vline(g, x - 10, 16, g->h - 30, C_AMBER);
            x += 30;
            ghost_done = true;
        }
        if (e->tick != col_t) {
            if (col_t != UINT32_MAX) {
                x += 30;
            }
            int32_t bar = (int32_t)(e->tick / bt);
            if (bar != last_bar && last_bar >= 0) {
                gfx_vline(g, x - 6, ln_ui_staff_y(38), ln_ui_staff_y(18) - ln_ui_staff_y(38) + 1, LN_STAFF_LINE);
                x += 8;
            }
            last_bar = bar;
            col_t = e->tick;
            if (x >= g->w - 14) {
                break;
            }
        }
        bool sel = i == pi;
        uint16_t c = sel ? C_AMBER : C_TEXT;
        ln_ui_staff_note(g, x, e->a, 0, c, false);
        if (e->len >= (uint32_t)SONG_PPQ * 2) {
            /* half and whole notes: hollow heads */
            int y = ln_ui_staff_y(ln_ui_staff_pos(e->a, 0));
            gfx_fill(g, x - 2, y - 1, 5, 3, C_BG);
        }
        static const char *const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        gfx_text(g, &font_small, x - 3, g->h - 12, names[e->a % 12], sel ? C_AMBER : C_DIM);
        if (sel) {
            gfx_vline(g, x - 10, 16, g->h - 30, C_AMBER);
        }
    }
    if (!ghost_done && x < g->w - 14) {
        if (col_t != UINT32_MAX) {
            x += 30;
        }
        if ((int32_t)(s_t / bt) != last_bar && last_bar >= 0) {
            gfx_vline(g, x - 6, ln_ui_staff_y(38), ln_ui_staff_y(18) - ln_ui_staff_y(38) + 1, LN_STAFF_LINE);
            x += 8;
        }
        ln_ui_staff_note(g, x, s_pitch, 0, C_AMBER, false);
        gfx_text(g, &font_small, x - 3, g->h - 12, "?", C_AMBER);
        gfx_vline(g, x - 10, 16, g->h - 30, C_AMBER);
    }
    song_unlock();
}

const screen_t scr_notes = {
    .name = "notes", .enter = enter, .leave = leave, .tick = tick, .key = key, .draw = draw, .draw2 = draw2,
};
