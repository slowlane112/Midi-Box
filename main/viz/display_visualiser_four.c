#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "v1_midi.h"
#include "v1_compat.h"
#include "viz_common.h"


#define COLOR_WHITE      0xFFFF
#define COLOR_YELLOW     0xE0FF
#define COLOR_CYAN       0xFF07
#define COLOR_MAGENTA    0x1FF8
#define COLOR_ORANGE     0x20FF
#define COLOR_GREEN      0xE007
#define COLOR_PURPLE     0x1F8F
#define COLOR_BLUE       0x1F00
#define COLOR_DARK_BG    viz_bg(0)

#define COLOR_F800       0xF800
#define COLOR_FD20       0xFD20
#define COLOR_FA20       0xFA20
#define COLOR_FFE0       0xFFE0

#define BLOCK_SIZE         2
#define BLOCK_COLUMNS      ((DISPLAY_2_WIDTH + BLOCK_SIZE - 1) / BLOCK_SIZE)
#define BLOCK_ROWS         ((DISPLAY_2_HEIGHT + BLOCK_SIZE - 1) / BLOCK_SIZE)

// 1/4 second (250 ms) using FreeRTOS system ticks
#define COLOR_COOLDOWN_TICKS  pdMS_TO_TICKS(250)

// Max concurrent drum waves allowed on screen at once
#define MAX_ACTIVE_WAVES   3

typedef struct {
    float radius;
    uint16_t color;
    uint8_t shape_index;
    bool active;
} drum_wave_t;

bool display_visualiser_four_background_oval = false;
uint8_t display_visualiser_four_background_color = 0;
uint8_t display_visualiser_four_color_set = 0;
uint8_t display_visualiser_four_shape = 0;   /* button 10: 0 auto, 1 circle, 2 diamond, 3 squircle, 4 octagon */

static const uint16_t static_bg_colors[8] = {
    0x0000, 0x0800, 0x0040, 0x0002, 0x0802, 0x0042, 0x0840, 0x0842
};

const size_t display_visualiser_four_background_colors_count = 
    sizeof(static_bg_colors) / sizeof(static_bg_colors[0]);

typedef struct {
    uint16_t bg[4];
    uint16_t drum[5];
} color_set_t;

static const color_set_t color_sets[12] = {
    {
        { COLOR_CYAN, COLOR_BLUE, COLOR_PURPLE, COLOR_MAGENTA },
        { 0xFFFF, 0xFFE0, 0xFD20, 0xF800, 0xFF10 }
    },
    {
        { COLOR_GREEN, 0x07FF, 0xFFE0, COLOR_MAGENTA },
        { 0xFFFF, 0xF81F, 0xFD20, 0x07FF, 0x07E0 }
    },
    {
        { COLOR_F800, COLOR_FD20, COLOR_FA20, COLOR_FFE0 },
        { 0xFFFF, 0xFFE0, 0xFC00, 0xFB20, 0xFF10 }
    },
    {
        { 0x1F8F, 0xF81F, 0x1FF8, 0xC819 },
        { 0xFFFF, 0xFFE0, 0x07FF, 0xF81F, 0xFD20 }
    },
    {
        { 0x0320, 0x07E0, 0x0410, 0x07FE },
        { 0xFFFF, 0xFFE0, 0x07FF, 0x07EE, 0xFD20 }
    },
    {
        { 0x1F8F, 0xF810, 0xFA20, 0x1FF8 },
        { 0xFFFF, 0xFFE0, 0xF81F, 0xFD20, 0x07FF }
    },
    {
        { 0x07FF, 0x07EE, 0x03EF, 0x7FFF },
        { 0xFFFF, 0xFFE0, 0x07FF, 0xF81F, 0xFD20 }
    },
    {
        { 0xFD20, 0xFC00, 0xFBE0, 0xFF20 },
        { 0xFFFF, 0xFFE0, 0xF800, 0xFD20, 0x07EE }
    },
    {
        { 0x1F8F, 0x9019, 0x8811, 0xF81F },
        { 0xFFFF, 0xFFE0, 0x07FF, 0xF81F, 0xFD20 }
    },
    {
        { 0xF81F, 0x07FF, 0xFFE0, 0xF800 },
        { 0xFFFF, 0xFFE0, 0x07FE, 0xF81F, 0x07FF }
    },
    {
        { 0x07E0, 0xFFE0, 0x0400, 0x87E0 },
        { 0xFFFF, 0xF81F, 0xFD20, 0x07FF, 0x07E0 }
    },
    {
        { 0x780F, 0x8010, 0xF81F, 0x1F8F },
        { 0xFFFF, 0xFFE0, 0xFD20, 0xF800, 0x07FF }
    }
};

static int16_t sin_lut[256];
static bool lut_initialized = false;
static bool initialized = false;

static uint32_t frame_tick = 0;
static TickType_t last_color_switch_tick = 0;
static float ambient_phase = 0.0f;
static uint64_t last_frame_time_us = 0;

static drum_wave_t active_waves[MAX_ACTIVE_WAVES];
static int drum_color_index = 0;
static int drum_shape_index = 0;

static uint64_t last_drum_timestamps[128] = {0};
static uint8_t last_note_values[128] = {0};
static float smoothed_velocity = 0.0f;
static uint64_t last_spawn_time_us = 0; // Time-based debounce tracker

static void init_sine_lut(void)
{
    if (lut_initialized) return;
    for (int i = 0; i < 256; ++i) {
        float angle = ((float)i / 256.0f) * 6.28318531f;
        sin_lut[i] = (int16_t)(sinf(angle) * 1000.0f);
    }
    lut_initialized = true;
}

void display_visualiser_four_init(void)
{
    init_sine_lut();
    frame_tick = 0;
    last_color_switch_tick = 0;
    ambient_phase = 0.0f;
    drum_color_index = 0;
    drum_shape_index = 0;
    smoothed_velocity = 0.0f;
    last_spawn_time_us = 0;
    last_frame_time_us = esp_timer_get_time();

    for (int i = 0; i < MAX_ACTIVE_WAVES; ++i) {
        active_waves[i].active = false;
        active_waves[i].radius = 0.0f;
    }

    memset(last_drum_timestamps, 0, sizeof(last_drum_timestamps));
    memset(last_note_values, 0, sizeof(last_note_values));
    initialized = true;
}

void display_visualiser_four_deinit(void)
{
    initialized = false;
    frame_tick = 0;
    ambient_phase = 0.0f;
    smoothed_velocity = 0.0f;
    last_spawn_time_us = 0;
    for (int i = 0; i < MAX_ACTIVE_WAVES; ++i) {
        active_waves[i].active = false;
        active_waves[i].radius = 0.0f;
    }
    memset(last_drum_timestamps, 0, sizeof(last_drum_timestamps));
    memset(last_note_values, 0, sizeof(last_note_values));
}

// Blazing fast integer square root (Zero float overhead)
static inline uint32_t fast_isqrt(uint32_t val)
{
    if (val <= 1) return val;
    uint32_t op = val;
    uint32_t res = 0;
    uint32_t one = 1u << 30;
    while (one > op) one >>= 2;
    while (one != 0) {
        if (op >= res + one) {
            op -= res + one;
            res = (res >> 1) + one;
        } else {
            res >>= 1;
        }
        one >>= 2;
    }
    return res;
}

// Shape distance evaluator using 100% integer math
static inline int get_shape_dist(int dx, int dy, uint8_t shape_index)
{
    switch (shape_index) {
        case 0: // Circle
            return (int)fast_isqrt((uint32_t)(dx * dx + dy * dy));
        case 1: // Diamond
            return abs(dx) + abs(dy);
        case 2: // Squircle
        {
            int ax = abs(dx);
            int ay = abs(dy);
            int max_d = (ax > ay) ? ax : ay;
            int min_d = (ax < ay) ? ax : ay;
            return max_d + (min_d / 4);
        }
        case 3: // Rounded Octagon
        {
            int ax = abs(dx);
            int ay = abs(dy);
            int max_d = (ax > ay) ? ax : ay;
            int diag = (ax + ay) * 3 / 4;
            return (max_d > diag) ? max_d : diag;
        }
        default:
            return (int)fast_isqrt((uint32_t)(dx * dx + dy * dy));
    }
}

void display_visualiser_four_draw(
    const midi_values_t *midi_values,
    char *message,
    bool display_active)
{
    if (midi_values == NULL || framebuffer_2 == NULL) {
        return;
    }

    if (!initialized) {
        display_visualiser_four_init();
    }

    frame_tick++;

    uint64_t current_us = esp_timer_get_time();
    float dt = (float)(current_us - last_frame_time_us) / 1000000.0f;
    if (dt > 0.1f) dt = 0.016f;
    last_frame_time_us = current_us;

    float target_velocity = 0.0f;
    float bass_vel = 0.0f;
    TickType_t current_ticks = xTaskGetTickCount();
    bool trigger_color_change = false;
    bool frame_has_drum_trigger = false;

    uint8_t set_idx = display_visualiser_four_color_set;
    if (set_idx >= 12) set_idx = 0;
    const color_set_t *active_set = &color_sets[set_idx];

    for (int i = 0; i < 128; ++i) {
        uint8_t vel = midi_values->note_values[i];
        
        if (vel != last_note_values[i]) {
            if (abs((int)vel - (int)last_note_values[i]) > 2) {
                if (i >= 60) {
                    trigger_color_change = true;
                }
            }
            last_note_values[i] = vel;
        }

        if (vel > 2) {
            target_velocity += (float)vel;
            if (i < 45) bass_vel += (float)vel;
        }

        uint64_t ts = midi_values->drum_timestamp_us[i];
        if (ts != last_drum_timestamps[i] && ts > 0) {
            last_drum_timestamps[i] = ts;
            frame_has_drum_trigger = true;
            trigger_color_change = true;
        }
    }

    // Strict time-based debounce: Requires at least 120ms between spawned shapes
    // to completely prevent double-spawns or overlapping shapes on rapid hits.
    if (frame_has_drum_trigger && (current_us - last_spawn_time_us > 150000)) {
        last_spawn_time_us = current_us;
        
        drum_color_index = (drum_color_index + 1) % 5;
        drum_shape_index = (drum_shape_index + 1) % 4;

        int spawn_slot = -1;
        for (int w = 0; w < MAX_ACTIVE_WAVES; ++w) {
            if (!active_waves[w].active) {
                spawn_slot = w;
                break;
            }
        }
        if (spawn_slot == -1) {
            spawn_slot = 0;
        }

        active_waves[spawn_slot].active = true;
        active_waves[spawn_slot].radius = 2.0f;
        active_waves[spawn_slot].color = active_set->drum[drum_color_index];
        active_waves[spawn_slot].shape_index = display_visualiser_four_shape ? (uint8_t)(display_visualiser_four_shape - 1) : (uint8_t)drum_shape_index;
    }

    if (trigger_color_change) {
        if ((current_ticks - last_color_switch_tick) >= COLOR_COOLDOWN_TICKS || last_color_switch_tick == 0) {
            last_color_switch_tick = current_ticks;
            display_visualiser_four_color_set = (display_visualiser_four_color_set + 1) % 12;
        }
    }

    for (int w = 0; w < MAX_ACTIVE_WAVES; ++w) {
        if (active_waves[w].active) {
            active_waves[w].radius += dt * 360.0f;
            if (active_waves[w].radius > 180.0f) {
                active_waves[w].active = false;
            }
        }
    }

    smoothed_velocity += (target_velocity - smoothed_velocity) * 0.1f;

    if (display_active) {
        float speed = 0.015f + (smoothed_velocity / 900.0f);
        if (speed > 0.60f) {
            speed = 0.60f;
        }
        ambient_phase += speed;
    }

    int bg_time_q = (int)(ambient_phase * 24.0f) & 255;
    int bass_offset = (int)(bass_vel / 20.0f);

    int cx = DISPLAY_2_WIDTH / 2;
    int cy = DISPLAY_2_HEIGHT / 2;

    float music_level = fminf(smoothed_velocity / 1200.0f, 1.0f);
    float pattern_scale = 1.0f + (music_level * 0.15f);

    int note_color_shift = (int)(smoothed_velocity / 150.0f);
    int bg_phase = bg_time_q / 24;

    for (int block_y = 0; block_y < BLOCK_ROWS; ++block_y) {
        int y = block_y * BLOCK_SIZE;
        int next_y = y + 1;
        bool has_second_row = next_y < DISPLAY_2_HEIGHT;

        int pattern_y = cy + (int)((y - cy) * pattern_scale);
        int pattern_y_next = cy + (int)((next_y - cy) * pattern_scale);

        uint32_t *row1_ptr = (uint32_t *)&framebuffer_2[y * DISPLAY_2_WIDTH];
        uint32_t *row2_ptr = has_second_row ? (uint32_t *)&framebuffer_2[next_y * DISPLAY_2_WIDTH] : NULL;

        for (int block_x = 0; block_x < BLOCK_COLUMNS; ++block_x) {
            int x = block_x * BLOCK_SIZE;
            int x_next = x + 1;

            int pattern_x = cx + (int)((x - cx) * pattern_scale);
            int pattern_x_next = cx + (int)((x_next - cx) * pattern_scale);

            int v00 = sin_lut[(pattern_x * 3 + bg_time_q + bass_offset) & 255] + sin_lut[(pattern_y * 4 - bg_time_q + bass_offset) & 255];
            int v10 = sin_lut[(pattern_x_next * 3 + bg_time_q + bass_offset) & 255] + sin_lut[(pattern_y * 4 - bg_time_q + bass_offset) & 255];

            uint16_t p00 = active_set->bg[(abs(v00) / 250 + bg_phase + note_color_shift) & 3];
            uint16_t p10 = active_set->bg[(abs(v10) / 250 + bg_phase + note_color_shift) & 3];
            uint16_t p01 = p00;
            uint16_t p11 = p10;

            if (has_second_row) {
                int v01 = sin_lut[(pattern_x * 3 + bg_time_q + bass_offset) & 255] + sin_lut[(pattern_y_next * 4 - bg_time_q + bass_offset) & 255];
                int v11 = sin_lut[(pattern_x_next * 3 + bg_time_q + bass_offset) & 255] + sin_lut[(pattern_y_next * 4 - bg_time_q + bass_offset) & 255];
                
                p01 = active_set->bg[(abs(v01) / 250 + bg_phase + note_color_shift) & 3];
                p11 = active_set->bg[(abs(v11) / 250 + bg_phase + note_color_shift) & 3];
            }

            for (int w = 0; w < MAX_ACTIVE_WAVES; ++w) {
                if (!active_waves[w].active) continue;

                float r = active_waves[w].radius;
                uint16_t w_color = active_waves[w].color;
                uint8_t shape_idx = active_waves[w].shape_index;
                int int_r = (int)r;

                // Sub-pixel (0,0)
                int dx0 = x - cx;
                int dy0 = y - cy;
                if (abs(dx0) <= int_r + 12 && abs(dy0) <= int_r + 12) {
                    int dist = get_shape_dist(dx0, dy0, shape_idx);
                    int diff = abs(dist - int_r);
                    if (diff < 3) p00 = COLOR_WHITE;
                    else if (diff < 8) p00 = w_color;
                }

                // Sub-pixel (1,0)
                int dx1 = x_next - cx;
                int dy1 = y - cy;
                if (abs(dx1) <= int_r + 12 && abs(dy1) <= int_r + 12) {
                    int dist = get_shape_dist(dx1, dy1, shape_idx);
                    int diff = abs(dist - int_r);
                    if (diff < 3) p10 = COLOR_WHITE;
                    else if (diff < 8) p10 = w_color;
                }

                if (has_second_row) {
                    // Sub-pixel (0,1)
                    int dx2 = x - cx;
                    int dy2 = next_y - cy;
                    if (abs(dx2) <= int_r + 12 && abs(dy2) <= int_r + 12) {
                        int dist = get_shape_dist(dx2, dy2, shape_idx);
                        int diff = abs(dist - int_r);
                        if (diff < 3) p01 = COLOR_WHITE;
                        else if (diff < 8) p01 = w_color;
                    }

                    // Sub-pixel (1,1)
                    int dx3 = x_next - cx;
                    int dy3 = next_y - cy;
                    if (abs(dx3) <= int_r + 12 && abs(dy3) <= int_r + 12) {
                        int dist = get_shape_dist(dx3, dy3, shape_idx);
                        int diff = abs(dist - int_r);
                        if (diff < 3) p11 = COLOR_WHITE;
                        else if (diff < 8) p11 = w_color;
                    }
                }
            }

            row1_ptr[block_x] = ((uint32_t)p10 << 16) | p00;
            if (row2_ptr != NULL) {
                row2_ptr[block_x] = ((uint32_t)p11 << 16) | p01;
            }
        }
    }

    if (message != NULL) {
        int bar_height = (DISPLAY_2_HEIGHT < 24) ? DISPLAY_2_HEIGHT : 24;
        for (int y = 0; y < bar_height; ++y) {
            int row_offset = y * DISPLAY_2_WIDTH;
            for (int x = 0; x < DISPLAY_2_WIDTH; ++x) {
                framebuffer_2[row_offset + x] = COLOR_DARK_BG;
            }
        }
        graphic_display_text(framebuffer_2, message, 4, 4, COLOR_WHITE, COLOR_DARK_BG);
    }

    display_2_draw();
}