/* Drive seq.c's engine with a fake clock: count-in, loop recording, overdub playback, undo round, keep. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "settings.h"
#include "midi_in.h"
settings_t g_settings;
static int64_t g_now = 0;
int64_t esp_timer_get_time(void) { return g_now; }
/* tiny queue */
static uint8_t q_buf[256][16]; static int q_h, q_t; static size_t q_sz;
QueueHandle_t xQueueCreate(UBaseType_t n, UBaseType_t sz) { (void)n; q_sz = sz; return (QueueHandle_t)1; }
BaseType_t xQueueSend(QueueHandle_t q, const void *it, TickType_t t) { (void)q; (void)t; memcpy(q_buf[q_t++ & 255], it, q_sz); return 1; }
BaseType_t xQueueReceive(QueueHandle_t q, void *it, TickType_t t) { (void)q; (void)t; if (q_h == q_t) return 0; memcpy(it, q_buf[q_h++ & 255], q_sz); return 1; }
BaseType_t xQueueReset(QueueHandle_t q) { (void)q; q_h = q_t; return 1; }
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t f, const char *n, uint32_t s, void *a, UBaseType_t p, TaskHandle_t *h, BaseType_t c) { (void)f;(void)n;(void)s;(void)a;(void)p;(void)h;(void)c; return 1; }
void vTaskDelay(TickType_t t) { (void)t; }
static int g_log = 1;
void midi_out_send(const uint8_t *m, size_t n) { (void)n; if (g_log) printf("  %6.3fs out %02X %3d %3d\n", g_now / 1e6, m[0], m[1], m[2]); }
void midi_out_note(uint8_t ch, uint8_t n, uint8_t v, bool on) { uint8_t m[3] = { (uint8_t)((on ? 0x90 : 0x80) | ch), n, v }; midi_out_send(m, 3); }
void midi_out_cc(uint8_t ch, uint8_t c, uint8_t v) { (void)ch; (void)c; (void)v; }
void midi_out_program(uint8_t ch, uint8_t p) { (void)ch; (void)p; }
void midi_out_set_bank(uint8_t b) { (void)b; }
void monitor_note(uint8_t c, uint8_t n, uint8_t v, bool on) { (void)c; (void)n; (void)v; (void)on; }
void midi_in_set_listener(midi_in_listener_t f) { (void)f; }
void midi_in_set_thru(bool on) { (void)on; }
#include "../../main/studio/seq.c"
static void run_until(double sec) { while (g_now < (int64_t)(sec * 1e6)) { g_now += 1000; if (s_cmd != C_NONE) { cmd_t c = s_cmd; s_cmd = C_NONE; if (c == C_RECORD) begin(0, true); else if (c == C_PLAY) begin(s_cmd_from, false); else stop_engine(tick_at(g_now)); } if (s_state != SEQ_STOPPED) step(); } }
static void key(double at, int note, int vel) { run_until(at); uint8_t m[3] = { (uint8_t)(vel ? 0x90 : 0x80), (uint8_t)note, (uint8_t)vel }; printf("  %6.3fs IN  key %d %s\n", g_now / 1e6, note, vel ? "down" : "up"); on_input(m, 3, 0); }
static void dump(int t) { strack_t *tr = &g_song.tr[t]; printf("  track %d: %d events:", t, tr->n); for (int i = 0; i < tr->n; i++) printf(" [t%u len%u n%d r%d]", tr->ev[i].tick, tr->ev[i].len, tr->ev[i].a, tr->ev[i].round); printf("\n"); }
int main(void) {
    song_init(); song_new("sim"); g_song.bpm = 120;   /* 1 bar = 2 s */
    sev_t e = { 0, 240, EV_NOTE, 48, 100, 0 }; ev_insert(&g_song.tr[0], &e);   /* existing bass note at bar start */
    int t1 = song_track_add("Lead", 1, 80);
    s_activated = true; s_armed = t1; s_click = true;
    q_sz = sizeof(in_msg_t);
    printf("== record: 1 bar count-in, loop bar 1, overdub (clicks ch10 = 89/99)\n");
    seq_rec_t r = { .loop = true, .loop_start = 0, .loop_end = 1920, .start = 0, .overdub = true, .countin_bars = 1 };
    s_cmd_rec = r; s_overdub = true; s_cmd = C_RECORD;
    key(2.010, 60, 100); key(2.400, 60, 0);          /* round 1: C at ~beat 1 of the loop */
    key(3.000, 64, 90);  key(3.500, 64, 0);          /* E on beat 3 */
    g_log = 1;
    run_until(4.6);                                   /* loop wraps at 4.0s: round 2 should replay C and E */
    key(4.500, 67, 80);  key(4.800, 67, 0);          /* round 2: G */
    run_until(6.2);
    printf("== status: state=%d round=%d notes=%d\n", s_state, s_round, s_take_notes);
    dump(t1);
    printf("== undo last finished round (round 2's G) while round 3 plays\n");
    g_log = 0; seq_undo_round(); dump(t1);
    printf("== stop + keep\n");
    s_cmd = C_STOP; run_until(6.3); seq_keep(); dump(t1);
    printf("pending=%d dirty=%d bars=%u\n", s_status.take_pending, g_song.dirty, g_song.bars);
    return 0;
}
