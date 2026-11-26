#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

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

#define DISPLAY_2_WIDTH    428
#define DISPLAY_2_HEIGHT   142
#define BLOCK_SIZE         2
#define BLOCK_COLUMNS      (DISPLAY_2_WIDTH / BLOCK_SIZE)
#define BLOCK_ROWS         (DISPLAY_2_HEIGHT / BLOCK_SIZE)
#define BLOCK_COUNT        (BLOCK_COLUMNS * BLOCK_ROWS)

#define PI_F               3.14159265f
#define TWO_PI_F           6.28318531f

bool display_visualiser_three_background_oval = false;
uint8_t display_visualiser_three_background_color = 0;
uint8_t display_visualiser_three_shape = 0;   /* button 10: 0 auto (changes on drum hits), 1 circle, 2 square, 3 triangle */



const size_t display_visualiser_three_background_colors_count = 
    VIZ_COLOURS;

static const uint16_t palette[8] = {
    COLOR_CYAN, COLOR_MAGENTA, COLOR_YELLOW, COLOR_ORANGE,
    COLOR_GREEN, COLOR_PURPLE, COLOR_BLUE, COLOR_WHITE
};

/* Ultra-compact 3-byte geometry struct (~45 KB total) */
typedef struct __attribute__((packed)) {
    uint16_t dist_q4;     /* Distance * 16 (avoids sqrtf in loop) */
    uint8_t angle_index;  /* Quantized angle 0..255 */
    uint8_t triangle_q8;  /* Triangle modifier */
    uint8_t square_q8;    /* max(|cos|,|sin|): turns rings into squares */
} block_geometry_t;

static block_geometry_t *geometry = NULL;
static bool geometry_initialized = false;

/* Static reusable angle cache (only 256 bytes) */
static uint8_t zone_angle_cache[256];

static float tunnel_depth = 0.0f;
static float tunnel_twist = 0.0f;
static float shockwave_r = 0.0f;
static uint64_t last_drum_timestamps[128] = {0};
static int current_shape_mode = 0;
static float velocity_momentum = 0.0f;
static uint32_t star_anim_tick = 0;
static bool initialized = false;

static void init_geometry(void)
{
    if (geometry == NULL) {
        geometry = (block_geometry_t *)malloc(BLOCK_COUNT * sizeof(block_geometry_t));
        if (geometry == NULL) {
            return;
        }
    }

    const int cx = DISPLAY_2_WIDTH / 2;
    const int cy = DISPLAY_2_HEIGHT / 2;

    for (int block_y = 0; block_y < BLOCK_ROWS; ++block_y) {
        const int y = block_y * BLOCK_SIZE;
        const float dy = (float)(y - cy);

        for (int block_x = 0; block_x < BLOCK_COLUMNS; ++block_x) {
            const int x = block_x * BLOCK_SIZE;
            const float dx = (float)(x - cx);
            const size_t index = (size_t)block_y * BLOCK_COLUMNS + (size_t)block_x;

            block_geometry_t *g = &geometry[index];

            const float distance = sqrtf(dx * dx + dy * dy);
            const float angle = atan2f(dy, dx);
            const float triangle_mod = fabsf(cosf(angle * 1.5f));

            g->dist_q4 = (uint16_t)(distance * 16.0f + 0.5f);

            int angle_index = (int)((angle + PI_F) * (256.0f / TWO_PI_F));
            if (angle_index < 0) angle_index = 0;
            if (angle_index > 255) angle_index = 255;

            g->angle_index = (uint8_t)angle_index;
            g->triangle_q8 = (uint8_t)(triangle_mod * 255.0f + 0.5f);
            const float ca = fabsf(cosf(angle)), sa = fabsf(sinf(angle));
            g->square_q8 = (uint8_t)((ca > sa ? ca : sa) * 255.0f + 0.5f);
        }
    }

    geometry_initialized = true;
}

void display_visualiser_three_init(void)
{
    tunnel_depth = 0.0f;
    tunnel_twist = 0.0f;
    shockwave_r = 0.0f;
    velocity_momentum = 0.0f;
    current_shape_mode = 0;
    star_anim_tick = 0;

    if (!geometry_initialized) {
        init_geometry();
    }

    initialized = true;
}

void display_visualiser_three_deinit(void)
{
    if (geometry != NULL) {
        free(geometry);
        geometry = NULL;
    }
    geometry_initialized = false;
    initialized = false;
}

static inline int wrap_mod_8(int value)
{
    value %= 8;
    return (value < 0) ? value + 8 : value;
}

void display_visualiser_three_draw(
    const midi_values_t *midi_values,
    char *message,
    bool display_active)
{
    if (midi_values == NULL) {
        return;
    }

    /* Critical safety guard against null pointer panics */
    if (framebuffer_2 == NULL) {
        return;
    }

    if (!initialized) {
        display_visualiser_three_init();
    }

    star_anim_tick++;

    uint8_t background_index = display_visualiser_three_background_color;
    if (background_index > VIZ_CYCLE) {
        background_index = 0;
    }

    const uint16_t color_bg = viz_bg(background_index);

    uint8_t max_velocity = 0;
    float bass_energy = 0.0f;
    float treble_energy = 0.0f;

    for (int i = 0; i < 128; ++i) {
        const uint8_t vel = midi_values->note_values[i];
        if (vel > 15) {
            if (vel > max_velocity) max_velocity = vel;
            if (i < 48) bass_energy += (float)vel;
            else treble_energy += (float)vel;
        }
    }

    for (int i = 0; i < 128; ++i) {
        const uint64_t current_timestamp = midi_values->drum_timestamp_us[i];
        if (current_timestamp != last_drum_timestamps[i] && current_timestamp > 0) {
            last_drum_timestamps[i] = current_timestamp;
            shockwave_r = 2.0f;
            current_shape_mode = (current_shape_mode + 1) % 3;   /* 0 circle, 1 square, 2 triangle */
        }
    }

    if (display_visualiser_three_shape > 0) {
        current_shape_mode = display_visualiser_three_shape - 1;
    }
    const float target_momentum = (float)max_velocity / 127.0f;
    velocity_momentum += (target_momentum - velocity_momentum) * 0.3f;

    if (display_active) {
        tunnel_depth += velocity_momentum * 0.35f;
        tunnel_twist += (treble_energy > 0.0f)
            ? (0.002f + (treble_energy / 2000.0f))
            : 0.001f;
    }

    if (shockwave_r > 0.0f) {
        shockwave_r += 4.0f + (bass_energy / 50.0f);
        if (shockwave_r > (DISPLAY_2_WIDTH / 2)) {
            shockwave_r = 0.0f;
        }
    }

    /* PRECOMPUTED FRAME CONSTANTS */
    const int bass_shift = (int)(bass_energy / 40.0f);
    const uint32_t star_drift_y = star_anim_tick / 12;
    const uint32_t color_time_offset = star_anim_tick >> 3;

    /* PRECOMPUTE EXACT ZONE ANGLES ONCE PER FRAME */
    for (int i = 0; i < 256; ++i) {
        const float angle = ((float)i * (TWO_PI_F / 256.0f)) - PI_F;
        zone_angle_cache[i] = (uint8_t)wrap_mod_8((int)((angle + tunnel_twist) * 2.54f));
    }

    /* Optimized render loop with zero memory bloat */
    for (int block_y = 0; block_y < BLOCK_ROWS; ++block_y) {
        const int y = block_y * BLOCK_SIZE;
        const int next_y = y + 1;
        const bool has_second_row = next_y < DISPLAY_2_HEIGHT;

        uint32_t *row1_ptr = (uint32_t *)&framebuffer_2[y * DISPLAY_2_WIDTH];
        uint32_t *row2_ptr = has_second_row ? (uint32_t *)&framebuffer_2[next_y * DISPLAY_2_WIDTH] : NULL;

        for (int block_x = 0; block_x < BLOCK_COLUMNS; ++block_x) {
            const size_t geom_index = (size_t)block_y * BLOCK_COLUMNS + (size_t)block_x;
            const block_geometry_t *g = &geometry[geom_index];

            float distance = (float)g->dist_q4 * 0.0625f; /* Fast equivalent of / 16.0f */

            if (current_shape_mode == 1) {
                distance *= (float)g->square_q8 / 255.0f;
            } else if (current_shape_mode == 2) {
                const float triangle_mod = (float)g->triangle_q8 / 255.0f;
                distance *= (0.75f + triangle_mod * 0.35f);
            }

            uint16_t pixel_color = color_bg;

            if (distance < 3.0f) {
                pixel_color = (max_velocity > 0) ? COLOR_WHITE : color_bg;
            } else {
                const int zone_z = (int)(24.0f / distance + tunnel_depth);
                const int zone_angle = (int)zone_angle_cache[g->angle_index];
                const int pattern = (zone_z + zone_angle) & 7;

                if (shockwave_r > 0.0f && fabsf(distance - shockwave_r) < 3.0f) {
                    pixel_color = COLOR_WHITE;
                } else if (max_velocity > 0) {
                    if (pattern == 0 || pattern == 3) {
                        pixel_color = palette[(zone_z + bass_shift) & 7];
                    } else if (pattern == 5 && velocity_momentum > 0.5f) {
                        pixel_color = palette[zone_angle % 7];
                    } else {
                        pixel_color = color_bg; 
                    }
                } else if (pattern == 0) {
                    pixel_color = color_bg;
                }
            }

            uint16_t p00 = pixel_color;
            uint16_t p10 = pixel_color;
            uint16_t p01 = pixel_color;
            uint16_t p11 = pixel_color;

            /* Sparse, slow, upward-moving 1x1 sub-pixel stars with correct % 211 density and unsigned wrap */
            if (pixel_color == color_bg) {
                uint32_t hash_y = (uint32_t)block_y - star_drift_y;
                unsigned int star_seed = (unsigned int)((uint32_t)block_x * 1973U + hash_y * 9277U) ^ 0x55AA55AAU;
                
                if ((star_seed % 211) == 0) {
                    int color_idx = (int)((star_seed + color_time_offset) & 7);
                    uint16_t star_color = palette[color_idx];
                    int sub_pos = (int)(star_seed & 3);

                    if (sub_pos == 0) p00 = star_color;
                    else if (sub_pos == 1) p10 = star_color;
                    else if (sub_pos == 2) p01 = star_color;
                    else p11 = star_color;
                }
            }

            row1_ptr[block_x] = ((uint32_t)p10 << 16) | p00;
            if (row2_ptr != NULL) {
                row2_ptr[block_x] = ((uint32_t)p11 << 16) | p01;
            }
        }
    }

    if (message != NULL) {
        const int bar_height = (DISPLAY_2_HEIGHT < 24) ? DISPLAY_2_HEIGHT : 24;
        for (int y = 0; y < bar_height; ++y) {
            const int row_offset = y * DISPLAY_2_WIDTH;
            for (int x = 0; x < DISPLAY_2_WIDTH; ++x) {
                framebuffer_2[row_offset + x] = COLOR_DARK_BG;
            }
        }
        graphic_display_text(framebuffer_2, message, 4, 4, COLOR_WHITE, COLOR_DARK_BG);
    }

    display_2_draw();
}