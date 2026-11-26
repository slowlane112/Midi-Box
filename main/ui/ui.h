/*
 * ui.h - screen stack and the UI task.
 *
 * One task owns both displays. Each loop it polls the buttons, gives events to
 * the top screen, runs tick(), and redraws screen 1 when invalidated and
 * screen 2 at ~30 fps.
 *
 * key() returns true if it used the event. Unused visualiser keys (9-11)
 * go to the visualiser; volume (12/13) is always global.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "gfx.h"
#include "buttons.h"

typedef struct screen {
    const char *name;
    void (*enter)(void *arg);        /* pushed */
    void (*resume)(void);            /* back on top after a child closed (optional) */
    void (*leave)(void);             /* popped (optional) */
    bool (*key)(const btn_event_t *e);
    void (*tick)(uint32_t now_ms);   /* optional */
    void (*draw)(gfx_t *g);          /* screen 1 */
    void (*draw2)(gfx_t *g);         /* screen 2; NULL = visualiser */
} screen_t;

void ui_start(const screen_t *first, void *arg);

void ui_push(const screen_t *s, void *arg);
void ui_pop(void);
void ui_replace(const screen_t *s, void *arg);
const screen_t *ui_top(void);

void ui_invalidate(void);
void ui_render_now(void);
uint32_t ui_frame2(void);            /* increments every screen-2 frame */            /* redraw screen 1 immediately (progress during long work) */
bool ui_shift(void);
uint32_t ui_now(void);

void ui_toast(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* for overlays (dialogs): draw the screen under the top one */
void ui_draw_below(gfx_t *g);
void ui_draw2_below(gfx_t *g);
