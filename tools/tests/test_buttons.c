/* Button debounce: short taps, bouncy contacts, Shift combos, auto-repeat, and a UI that
 * only collects events every 40 ms (as when it is busy drawing). */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "board.h"
#include "buttons.h"

/* tiny queue + gpio stand-ins */
static btn_event_t q[64]; static int qh, qt;
QueueHandle_t xQueueCreate(UBaseType_t n, UBaseType_t sz) { (void)n; (void)sz; return (QueueHandle_t)q; }
BaseType_t xQueueSend(QueueHandle_t h, const void *p, TickType_t t) { (void)h; (void)t; if (qt - qh >= 32) return 0; memcpy(&q[qt++ % 64], p, sizeof(btn_event_t)); return 1; }
BaseType_t xQueueReceive(QueueHandle_t h, void *p, TickType_t t) { (void)h; (void)t; if (qh == qt) return 0; memcpy(p, &q[qh++ % 64], sizeof(btn_event_t)); return 1; }
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t f, const char *n, uint32_t s, void *a, UBaseType_t p, TaskHandle_t *h, BaseType_t c) { (void)f; (void)n; (void)s; (void)a; (void)p; (void)h; (void)c; return 1; }
TickType_t xTaskGetTickCount(void) { return 0; }
void vTaskDelayUntil(TickType_t *l, TickType_t d) { (void)l; (void)d; }
esp_err_t gpio_config(const gpio_config_t *c) { (void)c; return 0; }
int gpio_get_level(gpio_num_t p) { (void)p; return 1; }

static int fails;
static uint32_t now;
static int collected[16], shifted[16], repeats[16];
static void ui_collect(void) {
    btn_event_t e[8]; int n;
    while ((n = buttons_poll(now, e, 8)) > 0)
        for (int i = 0; i < n; i++) { if (e[i].repeat) repeats[e[i].key]++; else { collected[e[i].key]++; if (e[i].shift) shifted[e[i].key]++; } }
}
/* run ms of time with a raw mask function; the UI collects only every 40 ms */
static void run(int ms, uint32_t (*raw)(int t)) {
    for (int t = 0; t < ms; t += 2) { now += 2; buttons_sample(now, raw(t)); if (now % 40 == 0) ui_collect(); }
}
static int g_len, g_key, g_bounce;
static uint32_t tap(int t) {
    if (g_bounce && t < 4) return (t / 2) % 2 ? 0 : 1u << (g_key - 1);        /* bounce at press */
    if (t < g_len) return 1u << (g_key - 1);
    if (g_bounce && t < g_len + 4) return (t / 2) % 2 ? 1u << (g_key - 1) : 0; /* bounce at release */
    return 0;
}
static uint32_t idle(int t) { (void)t; return 0; }
static uint32_t shift_then_5(int t) { return 1u | (t >= 20 && t < 50 ? 1u << 4 : 0); }
static void check(const char *what, int got, int want) { printf("  %-46s %d (want %d)%s\n", what, got, want, got == want ? "" : "  <-- FAIL"); fails += got != want; }

int main(void) {
    buttons_init();
    int lens[] = { 10, 16, 30, 60, 120 };
    for (int i = 0; i < 5; i++) {
        memset(collected, 0, sizeof collected);
        g_key = 8; g_len = lens[i]; g_bounce = 1;
        run(200, tap); run(100, idle); ui_collect();
        char b[64]; snprintf(b, sizeof b, "%d ms tap with contact bounce -> presses", lens[i]);
        check(b, collected[8], 1);
    }
    memset(collected, 0, sizeof collected); g_key = 3; g_len = 4; g_bounce = 0;
    run(100, tap); ui_collect();
    check("4 ms glitch (noise) -> presses", collected[3], 0);
    memset(collected, 0, sizeof collected);
    for (int k = 0; k < 10; k++) { g_key = 2; g_len = 30; g_bounce = 1; run(60, tap); }
    ui_collect();
    check("10 quick taps, 60 ms apart -> presses", collected[2], 10);
    memset(collected, 0, sizeof collected); memset(shifted, 0, sizeof shifted);
    run(100, shift_then_5); run(50, idle); ui_collect();
    check("Shift held + 5 -> presses with shift", shifted[5], 1);
    check("Shift alone makes no event", collected[1], 0);
    memset(collected, 0, sizeof collected); memset(repeats, 0, sizeof repeats);
    g_key = 3; g_len = 1000; g_bounce = 0; run(1000, tap); run(50, idle); ui_collect();
    check("hold 1 s -> first press", collected[3], 1);
    check("hold 1 s -> repeats (450 ms, then every 90)", repeats[3], 7);
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
