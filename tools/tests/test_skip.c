/* Player screen in Folder / Shuffle with unplayable files: shows the error for 3 s, then
 * moves on; never picks a failed file again; stops when every file has failed. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "settings.h"
#include "sd.h"
#include "player.h"
#include "ui.h"
#include "app.h"
#include "buttons.h"
settings_t g_settings;
static uint32_t g_ms = 100000;
int64_t esp_timer_get_time(void) { return (int64_t)g_ms * 1000; }
uint32_t xTaskGetTickCount(void) { return g_ms; }
void vTaskDelay(uint32_t t) { (void)t; }
uint32_t ui_frame2(void) { static uint32_t f; return ++f; }
uint32_t ui_now(void) { return g_ms; }
bool ui_shift(void) { return false; }
void ui_invalidate(void) {} void ui_render_now(void) {}
void ui_toast(const char *f, ...) { (void)f; }
void ui_push(const screen_t *s, void *a) { (void)s; (void)a; } void ui_pop(void) {}
void ui_draw_below(gfx_t *g) { (void)g; } void ui_draw2_below(gfx_t *g) { (void)g; }
void settings_changed(void) {}
uint32_t esp_random(void) { static uint32_t r = 7; r = r * 1103515245 + 12345; return r >> 8; }
void midi_out_all_notes_off(void) {}
const screen_t scr_tracks;
/* folder: a.mid, b_bad.mid, c.mid, d_bad.mid */
static const char *g_files[8]; static int g_nfiles;
fs_list_t fs_list(const char *d, const char *e) { (void)d; (void)e; fs_list_t l = {0}; l.items = calloc((size_t)g_nfiles, sizeof(fs_entry_t)); for (int i = 0; i < g_nfiles; i++) l.items[i].name = strdup(g_files[i]); l.count = g_nfiles; l.ok = true; return l; }
void fs_list_free(fs_list_t *l) { for (int i = 0; i < l->count; i++) free(l->items[i].name); free(l->items); l->items = NULL; l->count = 0; }
void path_parent(char *p) { char *s = strrchr(p, '/'); if (s) *s = 0; }
void path_join(char *o, size_t n, const char *d, const char *f) { snprintf(o, n, "%s/%s", d, f); }
/* fake player */
static player_status_t g_st; static char g_loaded[64]; static int g_loads; static bool g_auto;
void player_load(const char *p, uint32_t ms, bool a) { g_auto = a; snprintf(g_loaded, sizeof g_loaded, "%s", path_name(p)); g_loads++; g_st.load_id++; g_st.state = strstr(p, "bad") ? PLAYER_ERROR : (a ? PLAYER_PLAYING : (ms ? PLAYER_PAUSED : PLAYER_STOPPED)); g_st.error = SMF_ERR_FORMAT; g_st.pos_ms = 0; g_st.len_ms = 5000; }
void player_get_status(player_status_t *s) { *s = g_st; }
void player_play(void) {} void player_pause(void) {} void player_toggle(void) {} void player_stop(void) {} void player_unload(void) {}
void player_seek_rel(int32_t ms) { (void)ms; } void player_seek_abs(uint32_t ms) { (void)ms; }
void player_set_speed(uint16_t p) { (void)p; } void player_set_transpose(int8_t s) { (void)s; } void player_set_bank(uint8_t b) { (void)b; }
void player_set_volume(uint8_t l) { (void)l; } void player_set_repeat(bool on) { (void)on; }
void player_lock(void) {} void player_unlock(void) {}
static smf_t g_smf; const smf_t *player_song(void) { return &g_smf; }
static uint8_t g_flags[64]; uint8_t *player_part_flags(void) { return g_flags; }
void player_parts_changed(void) {} uint8_t player_part_level(int p) { (void)p; return 0; }

static int fails;
static void check(const char *what, bool ok) { printf("  %-60s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }
static void run(int ms) { for (int t = 0; t < ms; t += 50) { g_ms += 50; scr_player.tick(g_ms); } }

int main(void) {
    static const char *files[] = { "a.mid", "b_bad.mid", "c.mid", "d_bad.mid" };
    for (int i = 0; i < 4; i++) g_files[i] = files[i]; g_nfiles = 4;
    g_settings.play_mode = PLAY_FOLDER;
    player_args_t a = { "/sdcard/x/a.mid", 0 };
    scr_player.enter(&a);
    check("Opened from Files: loads and waits (no autoplay)", !g_auto && g_st.state == PLAYER_STOPPED);
    player_args_t r = { "/sdcard/x/c.mid", 42000 };
    scr_player.enter(&r);
    check("Resume: loads at the saved position and waits", !g_auto && g_st.state == PLAYER_PAUSED);
    btn_event_t down = { .key = K_DOWN };
    scr_player.key(&down);
    check("Auto play off, paused: Next loads the next song and waits", !g_auto && !strcmp(g_loaded, "d_bad.mid"));
    scr_player.enter(&a); g_st.state = PLAYER_PLAYING;
    scr_player.key(&down);
    check("Auto play off, playing: Next plays the next song", g_auto && !strcmp(g_loaded, "b_bad.mid"));
    g_settings.autoplay = 1;
    scr_player.enter(&a);
    check("Auto play on: opened from Files plays at once", g_auto && g_st.state == PLAYER_PLAYING);
    scr_player.enter(&r);
    check("Auto play on: Resume plays at once", g_auto && !strcmp(g_loaded, "c.mid"));
    g_st.state = PLAYER_STOPPED;
    scr_player.key(&down);
    check("Auto play on, stopped: Next plays the next song", g_auto && !strcmp(g_loaded, "d_bad.mid"));
    g_settings.autoplay = 0;
    scr_player.enter(&a);
    run(200); g_st.state = PLAYER_ENDED; run(1100);
    check("Folder: after a.mid ends, loads b_bad.mid", !strcmp(g_loaded, "b_bad.mid"));
    run(2500);
    check("  stays on the error for 3 s", !strcmp(g_loaded, "b_bad.mid"));
    run(600);
    check("  then moves on to c.mid, playing", !strcmp(g_loaded, "c.mid") && g_auto);
    g_st.state = PLAYER_ENDED; run(1100); run(3100);
    check("  d_bad.mid fails too, wraps round to a.mid", !strcmp(g_loaded, "a.mid"));
    g_st.state = PLAYER_ENDED; run(1100);
    check("  next time round, skips b_bad.mid (known bad) -> c.mid", !strcmp(g_loaded, "c.mid"));

    g_settings.play_mode = PLAY_SHUFFLE;
    scr_player.enter(&a);
    int bad_picks = 0, picks = 0;
    for (int k = 0; k < 40; k++) {
        g_st.state = PLAYER_ENDED; run(1100);
        if (g_st.state == PLAYER_ERROR) run(3100);
        picks++;
        if (strstr(g_loaded, "bad") && k > 6) bad_picks++;
    }
    check("Shuffle: once both bad files are known, never picks them", bad_picks == 0);

    /* proper shuffle: one order, made when Shuffle is turned on / a song is opened */
    static const char *six[] = { "s1.mid", "s2.mid", "s3.mid", "s4.mid", "s5.mid", "s6.mid" };
    for (int i = 0; i < 6; i++) g_files[i] = six[i]; g_nfiles = 6;
    g_settings.play_mode = PLAY_SHUFFLE;
    player_args_t sa = { "/sdcard/x/s3.mid", 0 };
    scr_player.enter(&sa);
    char order1[7][16]; int seen[7] = {0};
    snprintf(order1[0], 16, "%s", g_loaded);
    for (int k = 1; k <= 6; k++) { g_st.state = PLAYER_ENDED; run(1100); snprintf(order1[k], 16, "%s", g_loaded); }
    for (int k = 0; k < 6; k++) seen[order1[k][1] - '0']++;
    bool all_once = true; for (int k = 1; k <= 6; k++) all_once &= seen[k] == 1;
    check("Shuffle: 6 songs, each plays exactly once per pass", all_once);
    check("  after the last, wraps to the first of the same order", !strcmp(order1[6], order1[0]));
    btn_event_t up = { .key = K_UP };
    g_st.pos_ms = 0; scr_player.key(&up);
    check("  previous goes back to the song before in the order", !strcmp(g_loaded, order1[5]));
    scr_player.key(&down);
    check("  next goes forward again", !strcmp(g_loaded, order1[0]));
    /* re-opening from the browser: a new order (try a few times, orders can coincide) */
    bool differs = false;
    for (int t = 0; t < 5 && !differs; t++) {
        scr_player.leave(); scr_player.enter(&sa);
        for (int k = 1; k <= 5; k++) { g_st.state = PLAYER_ENDED; run(1100); if (strcmp(g_loaded, order1[k] )) differs = true; }
    }
    check("  opening a song again makes a new order", differs);

    static const char *allbad[] = { "x_bad.mid", "y_bad.mid", "z_bad.mid" };
    for (int i = 0; i < 3; i++) g_files[i] = allbad[i]; g_nfiles = 3;
    g_settings.play_mode = PLAY_FOLDER;
    player_args_t b = { "/sdcard/x/x_bad.mid", 0 };
    g_loads = 0; scr_player.enter(&b); run(20000);
    check("All files bad: tries each once, then stops (3 loads)", g_loads == 3);

    g_settings.play_mode = PLAY_ONCE;
    g_files[0] = "a.mid"; g_files[1] = "b_bad.mid"; g_nfiles = 2;
    player_args_t c = { "/sdcard/x/b_bad.mid", 0 };
    g_loads = 0; scr_player.enter(&c); run(10000);
    check("Once mode: an error stays on screen (no skip)", g_loads == 1 && !strcmp(g_loaded, "b_bad.mid"));
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
