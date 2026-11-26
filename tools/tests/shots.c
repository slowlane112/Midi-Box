/* Render real firmware screens (phase 2) on the PC: the screens' own draw() into a framebuffer. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "settings.h"
#include "sd.h"
#include "song.h"
#include "seq.h"
#include "ft.h"
#include "ble.h"
#include "ble_scan.h"
#include "ble_midi.h"
#include "midi_in.h"
#include "monitor.h"
#include "theme.h"
#include "gfx.h"
#include "ui.h"
#include "app.h"
settings_t g_settings;
int64_t esp_timer_get_time(void) { return 100000000; }
uint32_t xTaskGetTickCount(void) { return 100000; }
void vTaskDelay(uint32_t t) { (void)t; }
uint32_t ui_frame2(void) { static uint32_t f; return ++f; }
/* --- ui stubs --- */
static uint32_t g_ms = 100000; static bool g_shift;
uint32_t ui_now(void) { return g_ms; }
bool ui_shift(void) { return g_shift; }
void ui_invalidate(void) {}
void ui_render_now(void) {}
void ui_toast(const char *f, ...) { (void)f; }
void ui_push(const screen_t *s, void *a) { (void)s; (void)a; }
void ui_pop(void) {}
void ui_draw_below(gfx_t *g) { (void)g; }
void ui_draw2_below(gfx_t *g) { (void)g; }
bool buttons_shift_held(void) { return g_shift; }
/* --- seq stub --- */
static seq_status_t g_seq;
void seq_get(seq_status_t *o) { *o = g_seq; }
void seq_stop(void) {} void seq_play(uint32_t f) { (void)f; } void seq_record(const seq_rec_t *r) { (void)r; }
void seq_keep(void) {} void seq_discard(void) {} void seq_undo_round(void) {}
void seq_activate(bool on) { (void)on; } void seq_set_key_hook(seq_key_hook_t fn) { (void)fn; } void seq_set_armed(int t) { (void)t; } void seq_set_click(bool on) { (void)on; }
void seq_set_overdub(bool on) { (void)on; } void seq_send_setup(void) {} void seq_init(void) {}
/* --- midi / ble / ft / fs stubs --- */
void midi_out_send(const uint8_t *m, size_t n) { (void)m; (void)n; }
void midi_out_note(uint8_t c, uint8_t n, uint8_t v, bool o) { (void)c; (void)n; (void)v; (void)o; }
void midi_out_cc(uint8_t c, uint8_t n, uint8_t v) { (void)c; (void)n; (void)v; }
void midi_out_program(uint8_t c, uint8_t p) { (void)c; (void)p; }
void midi_out_reset(uint8_t b, uint8_t v) { (void)b; (void)v; }
void midi_out_all_notes_off(void) {} void midi_out_set_bank(uint8_t b) { (void)b; }
bool midi_in_running(void) { return true; } void midi_in_start(uint8_t p) { (void)p; } void midi_in_stop(void) {}
void midi_in_set_thru(bool on) { (void)on; }
void midi_in_get_stats(midi_in_stats_t *o) { memset(o, 0, sizeof(*o)); o->msg_count = 5; o->last_ms = g_ms; o->last[0] = 0x90; o->last[1] = 60; o->last[2] = 96; o->last_len = 3; o->last_src = MIDI_SRC_BLE; }
ble_midi_state_t ble_midi_state = BLE_MIDI_CONNECTED; char ble_name[48] = "Roland A-49"; ble_addr_t ble_addr; uint8_t ble_own_addr_type;
ble_scan_device_t ble_scan_devices[BLE_SCAN_MAX_DEVICES]; int8_t ble_scan_device_count;
void ble_scan_start(void) {} void ble_scan_stop(void) {}
void ble_midi_connect(const ble_addr_t *a, uint8_t t) { (void)a; (void)t; } void ble_midi_disconnect(void) {}
static ft_status_t g_ft;
void ft_get(ft_status_t *o) { *o = g_ft; } void ft_start(void) {} void ft_stop(void) {} void ft_accept(void) {} void ft_decline(void) {} void ft_service(void) {} void ft_ack(void) {}
bool sd_mounted(void) { return true; }
bool sd_space(uint64_t *t, uint64_t *f) { *t = 15900000000ULL; *f = 14200000000ULL; return true; }
bool fs_is_dir(const char *p) { (void)p; return true; } bool fs_mkdir(const char *p) { (void)p; return true; } size_t fs_read_head(const char *p, void *b, size_t n) { (void)p; (void)b; (void)n; return 0; }
bool fs_exists(const char *p) { (void)p; return true; } bool fs_delete(const char *p) { (void)p; return true; }
fs_list_t fs_list(const char *d, const char *e) { (void)d; (void)e; fs_list_t l = {0}; static const char *n[] = { "jam_session.mbp", "my_song_03.mbp", "song_04.mbp" }; l.items = calloc(3, sizeof(fs_entry_t)); for (int i = 0; i < 3; i++) { l.items[i].name = strdup(n[i]); l.items[i].size = 2400 + i * 900; } l.count = 3; l.ok = true; return l; }
void fs_list_free(fs_list_t *l) { for (int i = 0; i < l->count; i++) free(l->items[i].name); free(l->items); l->count = 0; l->items = NULL; }
void path_join(char *o, size_t n, const char *d, const char *f) { snprintf(o, n, "%s/%s", d, f); }
void path_unique(char *p, size_t n) { (void)p; (void)n; }
void settings_changed(void) {}
size_t heap_caps_get_free_size(uint32_t c) { (void)c; return 4000000; }
void backlight_set(int l) { (void)l; }
/* --- output --- */
static uint16_t fb1[320 * 240], fb2[428 * 142];
static void save(const char *name, uint16_t *fb, int w, int h) {
    char p[64]; snprintf(p, sizeof p, "shot_%s.raw", name); FILE *f = fopen(p, "wb"); fputc(w & 255, f); fputc(w >> 8, f); fputc(h & 255, f); fputc(h >> 8, f); fwrite(fb, 2, (size_t)(w * h), f); fclose(f); }
static void shot(const char *name, const screen_t *s, bool both) {
    gfx_t g1 = { fb1, 320, 240 }, g2 = { fb2, 428, 142 };
    gfx_clear(&g1, C_BG); s->draw(&g1); save(name, fb1, 320, 240);
    if (both && s->draw2) { char n2[64]; snprintf(n2, sizeof n2, "%s2", name); gfx_clear(&g2, C_BG); s->draw2(&g2); save(n2, fb2, 428, 142); }
}
static void note(int t, uint32_t tick, uint32_t len, int n, int v) { sev_t e = { tick, len, EV_NOTE, (uint8_t)n, (uint8_t)v, 0 }; ev_insert(&g_song.tr[t], &e); }
static void press(int k, bool sh) { btn_event_t e = { .key = (uint8_t)k, .shift = sh }; scr_notes.key(&e); scr_notes.tick(g_ms); }
int main(void) {
    theme_apply(0); g_settings.click = 1; g_settings.countin = 1; g_settings.thru = 1;
    song_init(); song_new("my_song_03"); g_song.bars = 16;
    snprintf(g_song.tr[0].name, 16, "Piano");
    int b = song_track_add("Bass", 1, 33), d = song_track_add("Drums", 9, 0), s = song_track_add("Strings", 3, 48), l = song_track_add("Lead", 4, 80);
    song_track_add("Pad", 5, 89);
    srand(3);
    for (int bar = 0; bar < 16; bar++) {
        uint32_t t0 = (uint32_t)bar * 1920;
        if (bar < 4 || (bar >= 5 && bar < 12)) for (int k = 0; k < 4; k++) note(0, t0 + k * 480, 400, 60 + (k * 4 + bar) % 12, 90);
        note(b, t0, 900, 36 + bar % 5, 100); note(b, t0 + 960, 900, 38 + bar % 5, 90);
        if (bar < 10) for (int k = 0; k < 8; k++) note(d, t0 + k * 240, 60, k % 4 == 0 ? 36 : (k % 2 ? 42 : 38), 110);
        if (bar >= 3) note(s, t0, 1900, 55 + bar % 7, 70);
        if (bar >= 4 && bar < 7) for (int k = 0; k < 6; k++) note(l, t0 + k * 320 + (rand() % 40), 200 + rand() % 200, 70 + rand() % 12, 80);
    }
    g_song.dirty = true;
    g_studio_track = s; g_studio_playhead = 4 * 1920;
    g_seq.state = SEQ_STOPPED;
    shot("projects", &scr_projects, false);
    scr_projects.enter(NULL); shot("projects", &scr_projects, true);
    { extern const screen_t scr_projects; } scr_projects.key(&(btn_event_t){ .key = 3 }); shot("projects_sel", &scr_projects, true);
    shot("studio", &scr_studio, true);
    g_shift = true; shot("studio_shift", &scr_studio, false); g_shift = false;
    g_seq.state = SEQ_PLAYING; g_seq.tick = 6 * 1920 + 700; g_mon.level[0] = 100; g_mon.level[1] = 60; g_mon.level[3] = 80;
    shot("studio_play", &scr_studio, true); shot("studio_play_b", &scr_studio, true);   /* second frame comes from the cache */
    g_seq.state = SEQ_STOPPED; memset((void *)g_mon.level, 0, 16);
    scr_record.enter(NULL); shot("record_ready", &scr_record, false);
    g_seq.state = SEQ_RECORDING; g_seq.tick = 11 * 1920 + 2 * 480 + 100; g_seq.round = 2; g_seq.take_notes = 14;
    g_mon.level[0] = 90;
    shot("record", &scr_record, true);
    g_seq.state = SEQ_STOPPED;
    g_studio_track = l; g_studio_playhead = 4 * 1920;
    scr_edit.enter(NULL); shot("edit", &scr_edit, true);

    /* Notes: type Happy Birthday in with the buttons, then edit it */
    {
        song_new("birthday"); g_song.beats = 3; g_studio_track = 0; g_studio_playhead = 0;
        scr_notes.enter(NULL);
        static const struct { int p, len; } hb[] = { {67,360},{67,120},{69,480},{67,480},{72,480},{71,960},{67,360},{67,120},{69,480},{67,480},{74,480},{72,960} };
        static const int lens[] = { 60, 120, 160, 240, 320, 360, 480, 720, 960, 1440, 1920 };
        int cur_p = 60, cur_l = 6;
        for (int i = 0; i < 12; i++) {
            while (cur_p < hb[i].p) { press(K_UP, false); cur_p++; }
            while (cur_p > hb[i].p) { press(K_DOWN, false); cur_p--; }
            int want = 0; while (lens[want] != hb[i].len) want++;
            while (cur_l < want) { press(K_S3, false); cur_l++; }
            while (cur_l > want) { press(K_S2, false); cur_l--; }
            press(K_S5, false);
            if (i == 8) shot("notes_add", &scr_notes, true);
        }
        const strack_t *tr = &g_song.tr[0];
        uint32_t t = 0; bool ok = tr->n == 12;
        for (int i = 0; i < 12 && ok; i++) { ok &= tr->ev[i].tick == t && tr->ev[i].len == (uint32_t)hb[i].len && tr->ev[i].a == hb[i].p && tr->ev[i].b == 90; t += (uint32_t)hb[i].len; }
        printf("notes: Happy Birthday typed in, 12 notes, 4 bars of 3/4: %s\n", ok && g_song.bars >= 4 ? "ok" : "FAIL");
        /* back to the C5 in bar 2 and make it a D5 */
        for (int i = 0; i < 8; i++) press(K_VIZ_M, true);
        int idx = 4;
        bool on_c = true;
        press(K_UP, false); press(K_UP, false);
        on_c = tr->ev[idx].a == 74 && tr->ev[idx].tick == 1440;
        shot("notes_edit", &scr_notes, true);
        g_shift = true; shot("notes_shift", &scr_notes, false); g_shift = false;
        press(K_S1, true);   /* undo: back to C5? (one level: undoes the last semitone) */
        bool undo1 = tr->ev[idx].a == 73;
        press(K_S2, true);   /* earlier by 1/16 */
        bool moved = tr->ev[idx].tick == 1440 - 120 && tr->ev[idx].a == 73;
        press(K_S3, true);
        press(K_S4, false);  /* delete */
        bool del = tr->n == 11;
        press(K_S1, true);
        bool back = tr->n == 12;
        press(K_S4, true);   /* velocity: 90 -> 110 */
        bool vel = tr->ev[idx].b == 110;
        printf("notes: pitch, undo, move, delete, velocity: %s\n", on_c && undo1 && moved && del && back && vel ? "ok" : "FAIL");
        scr_notes.leave();
    }
    g_ft.state = FT_ASK; snprintf(g_ft.name, sizeof g_ft.name, "zelda_overworld.mid"); g_ft.size = 18432;
    shot("transfer_ask", &scr_transfer, true);
    g_ft.state = FT_RECEIVING; g_ft.received = 11200; shot("transfer_rx", &scr_transfer, true);
    g_ft.state = FT_WAITING; shot("transfer_wait", &scr_transfer, true);
    { extern const screen_t scr_settings; shot("settings", &scr_settings, true);
      for (int k = 0; k < 4; k++) scr_settings.key(&(btn_event_t){ .key = K_DOWN });
      shot("settings_auto", &scr_settings, true); }
    ble_midi_state = BLE_MIDI_IDLE;
    const char *names[] = { "Roland A-49", "WIDI Master", "CME Xkey Air", "Pixel 8" }; int8_t rs[] = { -48, -61, -70, -82 };
    for (int i = 0; i < 4; i++) { ble_scan_devices[i].used = true; snprintf(ble_scan_devices[i].name, 48, "%s", names[i]); ble_scan_devices[i].rssi = rs[i]; }
    scr_bt.tick(g_ms + 1000); shot("bt", &scr_bt, false);
    ble_midi_state = BLE_MIDI_CONNECTED; g_settings.bt_name[0] = 0; snprintf(g_settings.bt_name, 48, "Roland A-49");
    g_mon.last_ms[0] = g_ms; g_mon.last_ms[9] = g_ms;
    shot("livein", &scr_livein, false);
    puts("shots ok");
    return 0;
}
