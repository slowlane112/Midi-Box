/* Renders every screen-2 visualiser through the real viz.c dispatcher with a short
 * piece of music (chords, bass, melody, drums), dumps frames and times each one. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "settings.h"
#include "theme.h"
#include "monitor.h"
#include "v1_compat.h"
#include "viz.h"
settings_t g_settings;
static int64_t g_t = 1000000;
static uint32_t g_frame2;
int64_t esp_timer_get_time(void) { return g_t; }
uint32_t xTaskGetTickCount(void) { return (uint32_t)(g_t / 1000); }
void vTaskDelay(uint32_t t) { (void)t; }
void settings_changed(void) {}
uint32_t ui_frame2(void) { return g_frame2; }
void *heap_caps_malloc(size_t n, uint32_t caps) { (void)caps; return malloc(n); }
static uint16_t fb[428 * 142];

static void dump(const char *name)
{
    FILE *f = fopen(name, "wb"); fwrite(fb, 2, sizeof(fb) / 2, f); fclose(f);
}

/* a 2-bar loop at 120 bpm (60 frames per beat-pair): C - Am - F - G7 */
static const uint8_t chords[4][4] = { { 48, 60, 64, 67 }, { 45, 57, 60, 64 }, { 41, 57, 60, 65 }, { 43, 55, 59, 65 } };
static void music(int f)
{
    int beat = f / 30, in = f % 30;
    int ch = (beat / 4) % 4;
    if (in == 0 && beat % 4 == 0) {
        const uint8_t *p = chords[(ch + 3) % 4];
        for (int i = 0; i < 4; i++) monitor_note(i ? 0 : 1, p[i], 0, false);
        p = chords[ch];
        for (int i = 0; i < 4; i++) monitor_note(i ? 0 : 1, p[i], (uint8_t)(70 + i * 10), true);
    }
    static const uint8_t mel[8] = { 72, 74, 76, 79, 77, 76, 74, 71 };
    if (in == 0) monitor_note(3, mel[beat % 8], 110, true);
    if (in == 20) monitor_note(3, mel[beat % 8], 0, false);
    if (in == 0) { monitor_note(9, beat % 2 ? 38 : 36, 120, true); monitor_note(9, beat % 2 ? 38 : 36, 0, false); }
    if (in == 15) { monitor_note(9, 42, 80, true); monitor_note(9, 42, 0, false); }
}

int main(int argc, char **argv)
{
    int only = argc > 1 ? atoi(argv[1]) : -1;
    framebuffer_2 = fb;
    theme_apply(0);
    g_settings.viz_opt[0] = 1; g_settings.viz_opt[1] = 1; g_settings.viz_opt[4] = 1; g_settings.viz_opt[5] = 1; g_settings.viz_opt[10] = 1;
    monitor_init();
    {   /* instruments: piano chords, finger bass, flute melody */
        const uint8_t m[] = { 0xC0, 0, 0xC1, 33, 0xC3, 73 };
        monitor_channel_msgs(m, sizeof m);
    }
    viz_init();
    viz_set_active(true);
    gfx_t g = { fb, 428, 142 };
    for (int m = 0; m < 14; m++) {
        if (only >= 0 && m != only) continue;
        g_settings.viz = (uint8_t)m;
        monitor_clear();
        viz_step(0);
        double total = 0, worst = 0;
        for (int f = 0; f < 300; f++) {
            g_t += 16667; g_frame2++;
            music(f);
            clock_t c0 = clock();
            viz_draw(&g);
            double ms = (double)(clock() - c0) * 1000.0 / CLOCKS_PER_SEC;
            total += ms; if (ms > worst) worst = ms;
            if (f == 20 || f == 200 || f == 245) {
                char n[64]; snprintf(n, sizeof n, "viz_%02d_f%d.bin", m, f); dump(n);
            }
        }
        printf("   %-18s avg %.3f ms  worst %.3f ms (PC)\n", viz_name(), total / 300, worst);
    }
    return 0;
}
