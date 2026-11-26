/* Shuffle with a real folder: open a song, press Next again and again; every song comes up
 * once before any repeats, and files that fail are not tried twice. */
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
static const char *g_files[128]; static int g_nfiles;
fs_list_t fs_list(const char *d, const char *e) { (void)d; (void)e; fs_list_t l = {0}; l.items = calloc((size_t)g_nfiles, sizeof(fs_entry_t)); for (int i = 0; i < g_nfiles; i++) l.items[i].name = strdup(g_files[i]); l.count = g_nfiles; l.ok = true; return l; }
void fs_list_free(fs_list_t *l) { for (int i = 0; i < l->count; i++) free(l->items[i].name); free(l->items); l->items = NULL; l->count = 0; }
void path_parent(char *p) { char *s = strrchr(p, '/'); if (s) *s = 0; }
void path_join(char *o, size_t n, const char *d, const char *f) { snprintf(o, n, "%s/%s", d, f); }
/* fake player */
static player_status_t g_st; static char g_loaded[64]; static int g_loads; static bool g_auto;
static char g_q[16][64]; static int g_qh, g_qt; static bool g_qauto[16]; static uint32_t g_busy_until;
static char g_failed[64];
int g_req[1000]; int g_nreq;
void player_load(const char *p, uint32_t ms, bool a) { (void)ms; g_st.state = PLAYER_LOADING; if (g_nreq < 1000) g_req[g_nreq++] = atoi(path_name(p) + 4); snprintf(g_q[g_qt & 15], 64, "%s", path_name(p)); g_qauto[g_qt & 15] = a; g_qt++; }
static void player_step(void) {   /* the player task: one load every 300 ms, in order */
    if (g_qh == g_qt || g_ms < g_busy_until) return;
    const char *p = g_q[g_qh & 15]; bool a = g_qauto[g_qh & 15]; g_qh++;
    g_busy_until = g_ms + 300;
    g_auto = a; snprintf(g_loaded, sizeof g_loaded, "%s", p); g_loads++;
    if (strstr(p, "bad")) snprintf(g_failed, sizeof g_failed, "/sdcard/x/%s", p);
    g_st.load_id++; g_st.state = strstr(p, "bad") ? PLAYER_ERROR : (a ? PLAYER_PLAYING : PLAYER_STOPPED);
    g_st.error = SMF_ERR_FORMAT; g_st.pos_ms = 0; g_st.len_ms = 5000;
    extern int g_log[400]; extern int g_nlog; g_log[g_nlog++] = atoi(p + 4);
}
int g_log[400]; int g_nlog;
bool player_failed_path(char *b, int n) { snprintf(b, (size_t)n, "%s", g_failed); return g_failed[0] != 0; }
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
static void run(int ms) { for (int t = 0; t < ms; t += 50) { g_ms += 50; player_step(); scr_player.tick(g_ms); } }

int main(void) {
    static char names[60][24];
    int n = 60;
    for (int i = 0; i < n; i++) {
        snprintf(names[i], sizeof names[i], i == 0 ? "song%02d_bad.mid" : (i % 17 == 0 ? "song%02d_bad.mid" : "song%02d.mid"), i);
        g_files[i] = names[i];
    }
    g_nfiles = n;
    g_settings.play_mode = PLAY_SHUFFLE;
    g_settings.autoplay = 1;
    player_args_t a = { "/sdcard/x/song00_bad.mid", 0 };
    btn_event_t down = { .key = K_DOWN };
    for (int pattern = 0; pattern < 4; pattern++) {
        g_nreq = 0;
        scr_player.enter(&a);
        run(pattern == 0 ? 100 : 3500);   /* press Next at once, or after the error was shown */
        for (int k = 0; k < 3 * n; k++) {
            if (pattern == 2 && g_st.state == PLAYER_ERROR) { run(3100); }   /* let it skip by itself */
            else { scr_player.key(&down); run(pattern == 1 ? 1500 : (pattern == 3 ? 100 : 400)); }
        }
        run(4000);
        /* go through the songs the screen asked for, until every good song has come up */
        int seen[60] = { 0 }, repeats = 0, bad_twice = 0, good_seen = 0, goods = 0;
        for (int j = 0; j < n; j++) goods += !strstr(g_files[j], "bad");
        for (int q = 0; q < g_nreq && good_seen < goods; q++) {
            int i = g_req[q];
            bool bad = strstr(g_files[i], "bad") != NULL;
            if (seen[i]) { if (bad) bad_twice++; else repeats++; }
            else if (!bad) good_seen++;
            seen[i]++;
        }
        char what[100];
        snprintf(what, sizeof what, "pattern %d: every good song once before a repeat (rep %d, bad again %d, got %d/%d)",
                 pattern, repeats, bad_twice, good_seen, goods);
        check(what, !repeats && !bad_twice && good_seen == goods);
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
