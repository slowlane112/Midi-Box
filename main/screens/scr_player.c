/*
 * Player: now playing. Screen 2 runs the visualisers.
 * Buttons: 2/3 prev/next song, 4 back, 5/8 seek, 6 play/pause, 7 stop.
 * Shift:   4 play mode, 5 tempo, 6 transpose, 7 sound bank, 8 Tracks screen.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_random.h"
#include "sd.h"
#include "settings.h"
#include "midi_out.h"
#include "monitor.h"
#include "player.h"
#include "theme.h"
#include "widgets.h"
#include "dialog.h"
#include "viz.h"
#include "app.h"

#define SEEK_MS 5000

static char s_path[FS_PATH_MAX];
static char s_dir[FS_PATH_MAX];
static fs_list_t s_songs;          /* .mid files in the folder, for prev / next */
static int s_index = -1;
static uint32_t s_load_id;
static char s_title[48];
static int s_nparts, s_ntracks;
static uint32_t s_last_draw;
static player_state_t s_last_state;

#define SONG_GAP_MS 1000
#define SKIP_MS     3000    /* Folder / Shuffle: how long an unplayable file's message shows */
static uint8_t *s_bad;      /* per song in the folder: 1 = failed to load */
static int *s_order;        /* Shuffle: the folder's songs in a random order (indices into s_songs) */
static bool s_skip_pending, s_all_bad;
static uint32_t s_error_at, s_error_id;
static uint32_t s_ended_at;
static bool s_next_pending;

static const char *const s_mode_names[PLAY_MODE_COUNT] = { "Once", "Repeat", "Folder", "Shuffle" };

static void find_index(void)
{
    s_index = -1;
    const char *name = path_name(s_path);
    for (int i = 0; i < s_songs.count; i++) {
        if (!strcmp(s_songs.items[i].name, name)) {
            s_index = i;
            break;
        }
    }
}

/* autoplay: Settings > Auto play when the user opens a song (Files or Resume); for
 * next / previous it also plays if a song was already playing; Folder / Shuffle moving
 * on by themselves always play */
static bool s_want_play;   /* the last load was asked to play */

/* a song name too long for the screen scrolls one character at a time (as the
 * original firmware did) */
#define NAME_W 308
static w_scroll_t s_name;

static void start(const char *path, uint32_t start_ms, bool autoplay)
{
    s_want_play = autoplay;
    w_scroll_reset(&s_name);
    player_set_transpose(0);   /* every new song starts at normal tempo and pitch */
    player_set_speed(100);
    snprintf(s_path, sizeof(s_path), "%s", path);
    find_index();
    s_title[0] = '\0';
    s_nparts = s_ntracks = 0;
    player_load(s_path, start_ms, autoplay);
    snprintf(g_settings.resume_path, sizeof(g_settings.resume_path), "%s", s_path);
    g_settings.resume_ms = start_ms;
    settings_changed();
    ui_invalidate();
}

static void play_index(int i, bool autoplay)
{
    if (s_songs.count == 0) {
        return;
    }
    i = (i + s_songs.count) % s_songs.count;
    char p[FS_PATH_MAX];
    path_join(p, sizeof(p), s_dir, s_songs.items[i].name);
    start(p, 0, autoplay);
}

static bool is_bad(int i) { return s_bad && i >= 0 && i < s_songs.count && s_bad[i]; }

static void next_song(int dir, bool autoplay)
{
    int n = s_songs.count;
    if (n == 0) {
        return;
    }
    int good = 0;
    for (int i = 0; i < n; i++) {
        good += !is_bad(i);
    }
    if (good == 0) {
        play_index(s_index + dir, autoplay);   /* nothing known to work: just step */
        return;
    }
    if (g_settings.play_mode == PLAY_SHUFFLE && s_order) {
        /* the song after (or before) the current one in the shuffle order, wrapping
         * round to the start of the same order, stepping over files that failed */
        int pos = 0;
        while (pos < n && s_order[pos] != s_index) {
            pos++;
        }
        for (int k = 0; k < n; k++) {
            pos = ((pos + dir) % n + n) % n;
            if (!is_bad(s_order[pos])) {
                break;
            }
        }
        play_index(s_order[pos], autoplay);
        return;
    }
    int i = s_index;
    for (int k = 0; k < n; k++) {   /* step over files that already failed */
        i = ((i + dir) % n + n) % n;
        if (!is_bad(i)) {
            break;
        }
    }
    play_index(i, autoplay);
}

/* Next / previous pressed by the user: play the new song if Auto play is on, or if a
 * song was playing (or loading to play); otherwise just load it and wait for Play */
static bool user_autoplay(const player_status_t *st)
{
    return g_settings.autoplay || st->state == PLAYER_PLAYING ||
           ((st->state == PLAYER_LOADING || st->state == PLAYER_ERROR) && s_want_play);
}

/* Shuffle: a new random order of the folder (Fisher-Yates), made only when Shuffle is
 * switched on or a song is opened from the browser in Shuffle mode; it is kept until then */
static void make_shuffle(void)
{
    free(s_order);
    s_order = NULL;
    int n = s_songs.count;
    if (n == 0 || !(s_order = malloc((size_t)n * sizeof(int)))) {
        return;
    }
    for (int i = 0; i < n; i++) {
        s_order[i] = i;
    }
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(esp_random() % (uint32_t)(i + 1));
        int t = s_order[i];
        s_order[i] = s_order[j];
        s_order[j] = t;
    }
}

/* called on entering the player and when the play mode is changed */
static void apply_mode(void)
{
    player_set_repeat(g_settings.play_mode == PLAY_REPEAT);
    if (g_settings.play_mode == PLAY_SHUFFLE) {
        make_shuffle();
    } else {
        free(s_order);
        s_order = NULL;
    }
}

/* the current song's number in the order being played (shuffle order in Shuffle) */
static int song_number(void)
{
    if (g_settings.play_mode == PLAY_SHUFFLE && s_order) {
        for (int i = 0; i < s_songs.count; i++) {
            if (s_order[i] == s_index) {
                return i + 1;
            }
        }
    }
    return s_index + 1;
}

static void enter(void *arg)
{
    const player_args_t *a = arg;
    snprintf(s_dir, sizeof(s_dir), "%s", a->path);
    path_parent(s_dir);
    fs_list_free(&s_songs);
    s_songs = fs_list(s_dir, ".mid");
    /* drop folders from the playlist */
    int k = 0;
    for (int i = 0; i < s_songs.count; i++) {
        if (s_songs.items[i].is_dir) {
            free(s_songs.items[i].name);
        } else {
            s_songs.items[k++] = s_songs.items[i];
        }
    }
    s_songs.count = k;
    free(s_bad);
    s_bad = k ? calloc((size_t)k, 1) : NULL;
    {
        player_status_t now_st;
        player_get_status(&now_st);
        s_error_id = now_st.load_id;   /* an old error from before this visit doesn't count */
    }
    s_skip_pending = s_all_bad = false;
    s_load_id = 0;
    apply_mode();
    viz_set_caption(NULL);
    start(a->path, a->start_ms, g_settings.autoplay != 0);   /* Settings > Auto play */
}

static void leave(void)
{
    player_status_t st;
    player_get_status(&st);
    if (st.state == PLAYER_PLAYING || st.state == PLAYER_PAUSED) {
        g_settings.resume_ms = st.pos_ms;
    } else if (st.state == PLAYER_ENDED || st.state == PLAYER_STOPPED) {
        g_settings.resume_ms = 0;
    }
    settings_changed();
    player_unload();
    viz_set_active(false);
    fs_list_free(&s_songs);
    free(s_order);   /* back in the browser: the next song opened in Shuffle gets a new order */
    s_order = NULL;
    viz_set_caption(NULL);
}

static void resume(void) { ui_invalidate(); }

/* ------------------------------------------------------------------ dialogs */

static uint16_t s_old_speed;
static int8_t s_old_transpose;

static void speed_fmt(char *o, int n)
{
    player_status_t st;
    player_get_status(&st);
    snprintf(o, (size_t)n, "%u%%", st.speed_pct);
}

static void speed_change(int d)
{
    player_status_t st;
    player_get_status(&st);
    int v = st.speed_pct;
    if (d > 1) {
        v = (v / 50 + 1) * 50;         /* Shift +: up to the next 50, so 75 -> 100 */
    } else if (d < -1) {
        v = ((v - 1) / 50) * 50;       /* Shift -: down to the next 50, so 75 -> 50 */
    } else {
        v += d * 5;
    }
    player_set_speed((uint16_t)(v < 25 ? 25 : v));
    vTaskDelay(1);
}

static void speed_done(bool ok)
{
    if (!ok) {
        player_set_speed(s_old_speed);
    }
}

static void transpose_fmt(char *o, int n)
{
    player_status_t st;
    player_get_status(&st);
    snprintf(o, (size_t)n, "%+d semitones", st.transpose);
}

static void transpose_change(int d)
{
    player_status_t st;
    player_get_status(&st);
    int t = st.transpose + (d > 1 ? 12 : (d < -1 ? -12 : d));
    if (t < -12) {
        t = -12;
    }
    if (t > 12) {
        t = 12;
    }
    player_set_transpose((int8_t)t);
}

static void transpose_done(bool ok)
{
    if (!ok) {
        player_set_transpose(s_old_transpose);
    }
}

/* ------------------------------------------------------------------ keys */

static bool key(const btn_event_t *e)
{
    player_status_t st;
    player_get_status(&st);
    if (e->shift) {
        if (e->repeat && e->key != K_S2) {
            return true;
        }
        switch (e->key) {
        case K_S1:
            g_settings.play_mode = (uint8_t)((g_settings.play_mode + 1) % PLAY_MODE_COUNT);
            settings_changed();
            apply_mode();
            ui_toast("Play mode: %s", s_mode_names[g_settings.play_mode]);
            return true;
        case K_S2: {
            s_old_speed = st.speed_pct;
            static const dlg_value_t v = { "Tempo", "Shift + / - steps 50%", speed_fmt, speed_change, speed_done };
            dlg_value(&v);
            return true;
        }
        case K_S3: {
            s_old_transpose = st.transpose;
            static const dlg_value_t v = { "Transpose", "Drums (channel 10) are not transposed", transpose_fmt,
                                           transpose_change, transpose_done };
            dlg_value(&v);
            return true;
        }
        case K_S4:
            g_settings.bank = st.bank == BANK_GM ? BANK_MT32 : BANK_GM;
            settings_changed();
            player_set_bank(g_settings.bank);
            ui_toast("Sound bank: %s", g_settings.bank ? "Roland MT-32" : "General MIDI");
            return true;
        case K_S5:
            if (st.state >= PLAYER_STOPPED && st.state != PLAYER_ERROR) {
                ui_push(&scr_tracks, NULL);
            }
            return true;
        default:
            return false;
        }
    }
    switch (e->key) {
    case K_UP:
        if (!e->repeat) {
            /* like a CD player: back to the start first, previous song if already near it */
            if (st.pos_ms > 3000 && st.state != PLAYER_ERROR) {
                player_seek_abs(0);
            } else {
                next_song(-1, user_autoplay(&st));
            }
        }
        return true;
    case K_DOWN:
        if (!e->repeat) {
            next_song(1, user_autoplay(&st));
        }
        return true;
    case K_S1:
        if (!e->repeat) {
            ui_pop();
        }
        return true;
    case K_S2:
        player_seek_rel(-SEEK_MS);
        return true;
    case K_S3:
        if (!e->repeat) {
            player_toggle();
        }
        return true;
    case K_S4:
        if (!e->repeat) {
            player_stop();
        }
        return true;
    case K_S5:
        player_seek_rel(SEEK_MS);
        return true;
    default:
        return false;
    }
}

/* ------------------------------------------------------------------ tick */

static void tick(uint32_t now)
{
    player_status_t st;
    player_get_status(&st);
    if (w_scroll_tick(&s_name, &font_large, path_name(s_path), NAME_W, now)) {
        ui_invalidate();
    }
    if (st.load_id != s_load_id && st.state >= PLAYER_STOPPED && st.state != PLAYER_ERROR) {
        s_load_id = st.load_id;
        player_lock();
        const smf_t *s = player_song();
        snprintf(s_title, sizeof(s_title), "%s", s->title);
        s_nparts = s->nparts;
        s_ntracks = s->ntracks;
        player_unlock();
        ui_invalidate();
    }
    /* a file that won't load: remember it; in Folder / Shuffle show why for 3 s, then
     * go on to the next song, unless every song in the folder has failed */
    if (st.state == PLAYER_ERROR && st.load_id != s_error_id) {
        s_error_id = st.load_id;
        if (s_bad && s_index >= 0) {
            s_bad[s_index] = 1;
        }
        int bad = 0;
        for (int i = 0; i < s_songs.count; i++) {
            bad += is_bad(i);
        }
        s_all_bad = s_songs.count > 0 && bad >= s_songs.count;
        s_error_at = now;
        s_skip_pending = !s_all_bad && s_songs.count > 1 &&
                         (g_settings.play_mode == PLAY_FOLDER || g_settings.play_mode == PLAY_SHUFFLE);
    }
    if (st.state != PLAYER_ERROR) {
        s_skip_pending = false;
    } else if (s_skip_pending) {
        if (now - s_error_at >= SKIP_MS) {
            s_skip_pending = false;
            next_song(1, s_want_play);
        } else {
            ui_invalidate();   /* countdown */
        }
    }
    /* Folder / Shuffle: next song after 1 s of silence (Repeat waits inside the player) */
    if (st.state == PLAYER_ENDED && s_last_state != PLAYER_ENDED) {
        s_ended_at = now;
        s_next_pending = g_settings.play_mode == PLAY_FOLDER || g_settings.play_mode == PLAY_SHUFFLE;
    }
    if (st.state != PLAYER_ENDED) {
        s_next_pending = false;   /* user started, stopped or loaded something meanwhile */
    } else if (s_next_pending && now - s_ended_at >= SONG_GAP_MS) {
        s_next_pending = false;
        next_song(1, true);
    }
    if (st.state != s_last_state) {
        s_last_state = st.state;
        viz_set_active(st.state == PLAYER_PLAYING || st.state == PLAYER_PAUSED);
        ui_invalidate();
    }
    if (st.state == PLAYER_PLAYING && now - s_last_draw > 100) {
        s_last_draw = now;
        ui_invalidate();   /* time, progress and channel meters */
    }
}

/* ------------------------------------------------------------------ draw */

static uint32_t current_bpm(uint32_t pos_ms, uint16_t speed)
{
    uint32_t us_per_qn = 500000;
    player_lock();
    const smf_t *s = player_song();
    uint64_t us = (uint64_t)pos_ms * 1000;
    for (int i = 0; i < s->ntempos; i++) {
        if (s->tempos[i].us > us) {
            break;
        }
        us_per_qn = s->tempos[i].us_per_qn;
    }
    player_unlock();
    return (uint32_t)(60000000ULL * speed / 100 / us_per_qn);
}

static void draw(gfx_t *g)
{
    player_status_t st;
    player_get_status(&st);
    char right[48];
    snprintf(right, sizeof(right), "%s " UI_MIDDOT " %s", st.bank ? "MT-32" : "GM", s_mode_names[g_settings.play_mode % PLAY_MODE_COUNT]);
    w_status(g, "Player", right, C_ACC);

    char line[FS_PATH_MAX + 32];
    const char *rel = s_dir + strlen(SD_ROOT);
    if (s_index >= 0) {
        snprintf(line, sizeof(line), "%d of %d " UI_MIDDOT " %s", song_number(), s_songs.count, rel[0] ? rel : "/");
    } else {
        snprintf(line, sizeof(line), "%s", rel[0] ? rel : "/");
    }
    gfx_text_box(g, &font_small, 6, 22, 308, ALIGN_LEFT, line, C_DIM);
    w_text_scroll(g, &font_large, 6, 36, NAME_W, &s_name, path_name(s_path), C_TEXT);

    if (st.state == PLAYER_LOADING || st.state == PLAYER_EMPTY) {
        gfx_text_box(g, &font_main, 0, 110, g->w, ALIGN_CENTER, "Loading" UI_ELLIPSIS, C_DIM);
    } else if (st.state == PLAYER_ERROR) {
        gfx_text_box(g, &font_main, 0, 100, g->w, ALIGN_CENTER,
                     st.error == SMF_ERR_MEMORY ? "Not enough memory for this file" :
                     st.error == SMF_ERR_INCOMPLETE ? "Incomplete file" :
                     st.error == SMF_ERR_READ ? "Could not read the file" : "This is not a MIDI file we can play", C_REC);
        char hint[48];
        if (s_skip_pending) {
            uint32_t el = ui_now() - s_error_at;
            unsigned left = el >= SKIP_MS ? 0 : (unsigned)((SKIP_MS - el + 999) / 1000);
            snprintf(hint, sizeof(hint), "Next song in %u" UI_ELLIPSIS, left);
        } else if (s_all_bad && s_songs.count > 1) {
            snprintf(hint, sizeof(hint), "No playable songs in this folder");
        } else {
            snprintf(hint, sizeof(hint), "Press " UI_DOWN " for the next song");
        }
        gfx_text_box(g, &font_small, 0, 122, g->w, ALIGN_CENTER, hint, C_DIM);
    } else {
        if (s_title[0]) {
            snprintf(line, sizeof(line), "%s " UI_MIDDOT " %d parts", s_title, s_nparts);
        } else {
            snprintf(line, sizeof(line), "%d tracks " UI_MIDDOT " %d parts", s_ntracks, s_nparts);
        }
        gfx_text_box(g, &font_small, 6, 57, 308, ALIGN_LEFT, line, C_DIM);

        w_progress(g, 6, 76, 308, 6, st.pos_ms, st.len_ms, C_ACC);
        char a[16], b[20];
        fmt_time(a, sizeof(a), st.pos_ms);
        fmt_time(b, sizeof(b), st.len_ms);   /* total length of the song */
        gfx_text(g, &font_small, 6, 85, a, C_TEXT);
        gfx_text_right(g, &font_small, 314, 85, b, C_DIM);
        if (st.state == PLAYER_PAUSED) {
            gfx_text_box(g, &font_small, 6, 85, 308, ALIGN_CENTER, "Paused", C_AMBER);
        } else if (st.state == PLAYER_ENDED) {
            gfx_text_box(g, &font_small, 6, 85, 308, ALIGN_CENTER, "End", C_DIM);
        }

        char t[24], tr[16];
        snprintf(t, sizeof(t), "%lu bpm", (unsigned long)current_bpm(st.pos_ms, st.speed_pct));
        if (st.speed_pct != 100) {
            snprintf(t, sizeof(t), "%lu bpm %u%%", (unsigned long)current_bpm(st.pos_ms, st.speed_pct), st.speed_pct);
        }
        snprintf(tr, sizeof(tr), "%+d", st.transpose);
        w_stat(g, 6, 102, 100, "Tempo", t, st.speed_pct != 100 ? C_AMBER : C_TEXT);
        w_stat(g, 110, 102, 100, "Transpose", tr, st.transpose ? C_AMBER : C_TEXT);
        w_stat(g, 214, 102, 100, "Sound bank", st.bank ? "MT-32" : "GM", C_TEXT);

        for (int c = 0; c < 16; c++) {
            int x = (g->w - (15 * 19 + 16)) / 2 + c * 19;   /* centred: 16 meters, 19 px apart */
            gfx_fill(g, x, 140, 16, 50, C_PANEL);
            int lv = g_mon.level[c] * 50 / 127;
            gfx_fill(g, x, 190 - lv, 16, lv, c == 9 ? C_AMBER : C_ACC);
            char n[4];
            snprintf(n, sizeof(n), "%d", c + 1);
            gfx_text_box(g, &font_small, x, 194, 16, ALIGN_CENTER, n, C_DIM);
        }
    }

    static const char *const normal_play[5] = { "Back", "@rew", "@pause", "@stop", "@ff" };
    static const char *const normal_stop[5] = { "Back", "@rew", "@play", "@stop", "@ff" };
    static const char *const shifted[5] = { "Mode", "Tempo", "Transp", "Bank", "Tracks" };
    w_softkeys2(g, st.state == PLAYER_PLAYING ? normal_play : normal_stop, shifted);
}

const screen_t scr_player = {
    .name = "player", .enter = enter, .resume = resume, .leave = leave, .key = key, .tick = tick, .draw = draw,
};
