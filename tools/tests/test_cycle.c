/* Buttons 10 and 11 on every visualiser: 11 ends with Cycle, the "Cycle" heading stays,
 * the automatic colour changes show no heading, and Shift + 10 / 11 go back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
void *heap_caps_malloc(size_t n, uint32_t c) { (void)c; return malloc(n); }
static uint16_t fb[428 * 142];
static void frames(gfx_t *g, int n) { for (int i = 0; i < n; i++) { g_t += 16667; g_frame2++; viz_draw(g); } }
int main(void)
{
    framebuffer_2 = fb; theme_apply(0); monitor_init(); viz_init(); viz_set_active(true);
    gfx_t g = { fb, 428, 142 };
    int fails = 0;
    for (int m = 0; m < 14; m++) {
        g_settings.viz = (uint8_t)m; g_settings.viz_col[m] = 0; viz_step(0); frames(&g, 3);
        int presses = 0; bool found = false;
        for (; presses < 12 && !found; presses++) {
            viz_colour(1); frames(&g, 1);
            found = strstr(viz_heading(), "Cycle") != NULL;
        }
        frames(&g, 60);   /* 1 s later the Cycle heading is still the one shown (or gone), not a colour */
        const char *h = viz_heading();
        bool kept = h[0] == 0 || strstr(h, "Cycle");
        frames(&g, 7 * 60 + 10);   /* 8 s after Cycle started the colour changes, without a heading */
        h = viz_heading();
        bool next = h[0] == 0;
        bool ok = m == 8 ? !found : (found && kept && next);
        printf("  %-18s %s after %d presses%s\n", viz_name(), found ? "Cycle" : "no Cycle", presses, ok ? "" : "  FAIL");
        fails += !ok;
    }
    /* Shift + 11 goes back: from the first colour it wraps to Cycle, and Piano skips White */
    for (int m = 0; m < 14; m++) {
        if (m == 8) continue;   /* Psychedelic: its own colours */
        g_settings.viz = (uint8_t)m; g_settings.viz_col[m] = 0; viz_step(0); frames(&g, 3);
        viz_colour(-1); frames(&g, 1);
        bool cyc = strstr(viz_heading(), "Cycle") != NULL;
        viz_colour(-1); frames(&g, 1);
        int c = g_settings.viz_col[m];
        bool ok = cyc && !(m == 0 && c == 7) && (m != 0 || c == 6);
        if (!ok) printf("  %-18s back from the first colour: %s, then %d  FAIL\n", viz_name(), cyc ? "Cycle" : "not Cycle", c);
        fails += !ok;
        viz_colour(1); frames(&g, 1);
        ok = strstr(viz_heading(), "Cycle") != NULL;
        if (!ok) printf("  %-18s forward again does not reach Cycle  FAIL\n", viz_name());
        fails += !ok;
    }
    /* Shift + 10 goes back through the options */
    for (int m = 0; m < 14; m++) {
        g_settings.viz = (uint8_t)m; g_settings.viz_opt[m] = 0; viz_step(0);
        viz_option(-1); int back = g_settings.viz_opt[m];
        viz_option(1); int again = g_settings.viz_opt[m];
        bool ok = again == 0 && back != 0 || (back == 0 && again == 0);
        if (!ok) printf("  %-18s option back %d then forward %d  FAIL\n", viz_name(), back, again);
        fails += !ok;
    }
    printf(" Shift + 10 and Shift + 11 go back%s\n", fails ? "" : ": ok");
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
