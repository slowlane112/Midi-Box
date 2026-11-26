/* Run player.c's task loop against a fake clock and record what goes to the synth. */
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "settings.h"
settings_t g_settings;
static int64_t g_now = 1000000;
static jmp_buf g_end;
static int64_t g_stop_at;
/* scripted commands */
typedef struct { int64_t at; int kind; int arg; } script_t;
static script_t g_script[16]; static int g_ns, g_si;
static void *g_q;
int64_t esp_timer_get_time(void) { return g_now; }
void esp_rom_delay_us(uint32_t us) { g_now += us; }
void vTaskDelay(TickType_t t) { g_now += (int64_t)t * 1000; }
/* queue: one pending cmd slot */
static unsigned char g_slot[512]; static int g_has; static size_t g_isz;
QueueHandle_t xQueueCreate(UBaseType_t n, UBaseType_t sz) { (void)n; g_isz = sz; return (QueueHandle_t)1; }
BaseType_t xQueueSend(QueueHandle_t q, const void *it, TickType_t t) { (void)q; (void)t; memcpy(g_slot, it, g_isz); g_has = 1; return pdTRUE; }
void run_script_until(int64_t t);
BaseType_t xQueueReceive(QueueHandle_t q, void *it, TickType_t t) {
    (void)q;
    int64_t deadline = (t == portMAX_DELAY) ? g_stop_at : g_now + (int64_t)t * 1000;
    run_script_until(deadline);
    if (g_has) { memcpy(it, g_slot, g_isz); g_has = 0; return pdTRUE; }
    g_now = deadline;
    if (g_now >= g_stop_at) longjmp(g_end, 1);
    return pdFALSE;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t t) { (void)s; (void)t; return 1; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t s) { (void)s; return 1; }
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t f, const char *n, uint32_t s, void *a, UBaseType_t p, TaskHandle_t *h, BaseType_t c) { (void)f;(void)n;(void)s;(void)a;(void)p;(void)h;(void)c; return 1; }
/* synth output log */
void midi_out_send(const uint8_t *m, size_t n) {
    if ((m[0] & 0xF0) == 0x90 || (m[0] & 0xF0) == 0x80)
        printf("%7.3fs  %s ch%-2d note %3d vel %3d\n", (g_now - 1000000) / 1e6, (m[0] & 0xF0) == 0x90 && m[2] ? "ON " : "off", (m[0] & 15) + 1, m[1], m[2]);
    else if ((m[0] & 0xF0) == 0xC0) printf("%7.3fs  PROG ch%-2d %d\n", (g_now - 1000000) / 1e6, (m[0]&15)+1, m[1]);
}
void midi_out_note(uint8_t ch, uint8_t n, uint8_t v, bool on) { uint8_t m[3] = { (uint8_t)((on ? 0x90 : 0x80) | ch), n, v }; midi_out_send(m, 3); }
void midi_out_cc(uint8_t ch, uint8_t c, uint8_t v) { (void)ch; (void)c; (void)v; }
void midi_out_program(uint8_t ch, uint8_t p) { uint8_t m[2] = { (uint8_t)(0xC0 | ch), p }; midi_out_send(m, 2); }
void midi_out_pitch(uint8_t ch, uint16_t v) { (void)ch; (void)v; }
void midi_out_reset(uint8_t b, uint8_t v) { (void)b; (void)v; }
void midi_out_set_bank(uint8_t b) { (void)b; }
void midi_out_set_volume(uint8_t v) { (void)v; }
void monitor_note(uint8_t c, uint8_t n, uint8_t v, bool on) { (void)c; (void)n; (void)v; (void)on; }
void monitor_clear(void) {}
void monitor_cc(uint8_t c, uint8_t n, uint8_t v) { (void)c; (void)n; (void)v; }
#include "../../main/midi/player.c"
enum { K_LOAD, K_MUTE0, K_SEEK, K_PAUSE, K_PLAY };
void run_script_until(int64_t t) {
    while (!g_has && g_si < g_ns && g_script[g_si].at <= t) {
        script_t *s = &g_script[g_si++];
        if (s->at > g_now) g_now = s->at;
        printf("%7.3fs  ---- %s\n", (g_now - 1000000) / 1e6, (const char *[]){"load","mute part 0 (Piano)","seek","pause","play"}[s->kind]);
        switch (s->kind) {
        case K_LOAD: player_load("tools/tests/t1.mid", 0, true); break;
        case K_MUTE0: s_flags[0] |= PART_MUTE; player_parts_changed(); break;
        case K_SEEK: player_seek_abs((uint32_t)s->arg); break;
        case K_PAUSE: player_pause(); break;
        case K_PLAY: player_play(); break;
        }
    }
}
int main(int argc, char **argv) {
    int scenario = argc > 1 ? argv[1][0] - '0' : 0;
    g_settings.volume = 16;
    player_init();
    g_ns = 0;
    g_script[g_ns++] = (script_t){ 1000000, K_LOAD, 0 };
    if (scenario == 1) { g_script[g_ns++] = (script_t){ 1000000 + 600000, K_MUTE0, 0 }; }
    if (scenario == 2) { g_script[g_ns++] = (script_t){ 1000000 + 300000, K_SEEK, 4500 }; }
    if (scenario == 4) { player_set_repeat(true); }
    if (scenario == 5) { player_set_repeat(true); g_script[g_ns++] = (script_t){ 1000000 + 7500000, K_SEEK, 5900 }; }
    if (scenario == 3) { g_script[g_ns++] = (script_t){ 1000000 + 250000, K_PAUSE, 0 }; g_script[g_ns++] = (script_t){ 1000000 + 2250000, K_PLAY, 0 }; }
    g_stop_at = 1000000 + (scenario >= 4 ? 9500000 : 12000000);
    if (!setjmp(g_end)) player_task(NULL);
    player_status_t st; player_get_status(&st);
    printf("end: state=%d pos=%lums len=%lums\n", st.state, (unsigned long)st.pos_ms, (unsigned long)st.len_ms);
    return 0;
}
