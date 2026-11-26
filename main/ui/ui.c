#include <stdarg.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "board.h"
#include "lcd.h"
#include "settings.h"
#include "midi_out.h"
#include "theme.h"
#include "widgets.h"
#include "viz.h"
#include "dialog.h"
#include "v1_compat.h"
#include "ui.h"

#define STACK_MAX     8
#define POLL_MS       5
#define FRAME_MS      33    /* screen 1 */
#define FRAME2_MS     16    /* screen 2: the v1 visualisers animate per frame */

typedef struct {
    const screen_t *s;
} entry_t;

static entry_t s_stack[STACK_MAX];
static int s_depth;
static volatile bool s_dirty = true;
static bool s_last_shift;
static uint32_t s_toast_until;
static gfx_t s_g1, s_g2;
static int s_below;
static uint32_t s_frame2;   /* screen-2 frame counter: visualisers detect gaps */

uint32_t ui_frame2(void) { return s_frame2; }   /* stack index used by ui_draw_below during nested draws */

uint32_t ui_now(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
bool ui_shift(void) { return buttons_shift_held(); }
void ui_invalidate(void) { s_dirty = true; }
const screen_t *ui_top(void) { return s_depth ? s_stack[s_depth - 1].s : NULL; }

void ui_push(const screen_t *s, void *arg)
{
    if (s_depth >= STACK_MAX) {
        ESP_LOGE("ui", "screen stack full");
        return;
    }
    s_stack[s_depth++].s = s;
    if (s->enter) {
        s->enter(arg);
    }
    s_dirty = true;
}

void ui_pop(void)
{
    if (s_depth <= 1) {
        return;
    }
    const screen_t *s = s_stack[--s_depth].s;
    if (s->leave) {
        s->leave();
    }
    const screen_t *top = ui_top();
    if (top && top->resume) {
        top->resume();
    }
    s_dirty = true;
}

void ui_replace(const screen_t *s, void *arg)
{
    if (s_depth > 0) {
        const screen_t *old = s_stack[--s_depth].s;
        if (old->leave) {
            old->leave();
        }
    }
    ui_push(s, arg);
}

void ui_toast(const char *fmt, ...)
{
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    s_toast_until = ui_now() + 1800;
    w_toast_set(buf, s_toast_until, -1);
    s_dirty = true;
}

static void volume_key(int delta)
{
    int v = g_settings.volume + delta;
    if (v < 0) {
        v = 0;
    }
    if (v > 20) {
        v = 20;
    }
    g_settings.volume = (uint8_t)v;
    settings_changed();
    midi_out_set_volume(g_settings.volume);
    s_toast_until = ui_now() + 1500;
    w_toast_set("", s_toast_until, v);
    s_dirty = true;
}

void ui_draw_below(gfx_t *g)
{
    int saved = s_below;
    if (s_below > 0) {
        s_below--;
        const screen_t *s = s_stack[s_below].s;
        if (s->draw) {
            s->draw(g);
        }
    }
    s_below = saved;
}

void ui_draw2_below(gfx_t *g)
{
    int saved = s_below;
    if (s_below > 0) {
        s_below--;
        const screen_t *s = s_stack[s_below].s;
        if (s->draw2) {
            s->draw2(g);
        } else {
            viz_draw(g);
        }
    }
    s_below = saved;
}

static void draw1(void)
{
    const screen_t *s = ui_top();
    if (!s || !s->draw) {
        return;
    }
    lcd1_begin_frame();   /* waits for the previous DMA and any SD access */
    gfx_clear(&s_g1, C_BG);
    s_below = s_depth - 1;
    s->draw(&s_g1);
    dlg_draw_overlay(&s_g1);
    lcd1_flush();
}

void ui_render_now(void)
{
    draw1();
    s_dirty = false;
}

static void draw2(void)
{
    const screen_t *s = ui_top();
    s_frame2++;
    s_below = s_depth - 1;
    if (s && s->draw2) {
        s->draw2(&s_g2);
    } else {
        viz_draw(&s_g2);
    }
    lcd2_flush();
}

static void handle_key(const btn_event_t *e)
{
    if (e->key == K_VOL_UP || e->key == K_VOL_DN) {
        volume_key(e->key == K_VOL_UP ? 1 : -1);
        return;
    }
    const screen_t *s = ui_top();
    bool used = s && s->key && s->key(e);
    if (!used && !e->repeat) {
        /* same as v1: 9 = next visualiser (Shift: previous), 10 = option, 11 = colour */
        if (e->key == K_VIZ_L) {
            viz_step(e->shift ? -1 : 1);
        } else if (e->key == K_VIZ_M) {
            viz_option();
        } else if (e->key == K_VIZ_R) {
            viz_colour();
        }
    }
    s_dirty = true;
}

static void ui_task(void *arg)
{
    (void)arg;
    uint32_t next_frame = ui_now();
    uint32_t next_frame2 = next_frame;
    bool first = true;
    for (;;) {
        uint32_t now = ui_now();
        btn_event_t ev[8];
        int n;
        while ((n = buttons_poll(now, ev, 8)) > 0) {   /* every press since the last pass */
            for (int i = 0; i < n; i++) {
                handle_key(&ev[i]);
            }
        }
        bool sh = buttons_shift_held();
        if (sh != s_last_shift) {
            s_last_shift = sh;
            s_dirty = true;   /* soft-key labels change while Shift is held */
        }
        if (s_toast_until && (int32_t)(now - s_toast_until) >= 0) {
            s_toast_until = 0;
            s_dirty = true;
        }
        const screen_t *s = ui_top();
        if (s && s->tick) {
            s->tick(now);
        }
        settings_service(now);

        if ((int32_t)(now - next_frame) >= 0) {
            next_frame = now + FRAME_MS;
            if (s_dirty) {
                s_dirty = false;
                draw1();
            }
        }
        if ((int32_t)(now - next_frame2) >= 0) {
            next_frame2 = now + FRAME2_MS;
            draw2();
            if (first) {
                first = false;
                lcd1_wait_idle();                        /* the first frame has fully arrived */
                backlight_set(g_settings.brightness);   /* only now light it */
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void ui_start(const screen_t *first, void *arg)
{
    s_g1 = (gfx_t){ .px = lcd1_framebuffer(), .w = LCD1_W, .h = LCD1_H };
    s_g2 = (gfx_t){ .px = lcd2_framebuffer(), .w = LCD2_W, .h = LCD2_H };
    framebuffer_1 = s_g1.px;   /* for the v1 visualiser text helper */
    viz_init();
    ui_push(first, arg);
    xTaskCreatePinnedToCore(ui_task, "ui", 12288, NULL, 5, NULL, 0);
}
