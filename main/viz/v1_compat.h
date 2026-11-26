/*
 * v1_compat.h - lets the MIDI Box v1 visualisers (display_*.c, unchanged) run in v2.
 * They draw straight into framebuffer_2; the v2 UI task flushes screen 2 afterwards,
 * so display_2_draw() does nothing here.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define DISPLAY_2_WIDTH   428
#define DISPLAY_2_HEIGHT  142
#define DISPLAY_1_WIDTH   320
#define DISPLAY_1_HEIGHT  240

static const uint16_t BG_COLOR = 0x0000;

extern uint16_t *framebuffer_1;
extern uint16_t *framebuffer_2;
void display_2_draw(void);

/* v1 text call, drawn with the v2 font. y/x order as in v1. */
void graphic_display_text(uint16_t *framebuffer, const char *text, int y_start, int x_start, uint16_t fg, uint16_t bg);

/* v1 NVS ids still read by the piano; values now live in g_settings */
typedef enum {
    DISPLAY_PIANO_KEY_ON_COLOR = 0,
    DISPLAY_PIANO_SHOW_NOTES = 1,
} nvs_id_t;
uint8_t nvs_get_value(nvs_id_t id, uint8_t default_value);
bool nvs_get_value_bool(nvs_id_t id, bool default_value);
