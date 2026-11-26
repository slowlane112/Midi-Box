#include <string.h> 
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "v1_midi.h"
#include "v1_compat.h"
#include "viz_common.h"

#include <stdlib.h>
#include <math.h>

#define COLOR_WHITE    0xFFFF
#define COLOR_YELLOW   0xE0FF
#define COLOR_CYAN     0xFF07
#define COLOR_MAGENTA  0x1FF8
#define COLOR_ORANGE   0x20FF
#define COLOR_GREEN    0xE007
#define COLOR_PURPLE   0x1F8F
#define COLOR_BLUE     0x1F00
#define COLOR_DIM_BLUE 0x0841

#define MAX_STARS 60

bool display_visualiser_one_background_oval = false;
uint8_t display_visualiser_one_background_color = 0;






const size_t display_visualiser_one_background_colors_count = VIZ_COLOURS;

typedef struct {
    float x, y;
    float speed;
    uint16_t color;
} star_t;

typedef struct {
    int x;
    int y;
    int style;          
    float anim_timer;   
    uint16_t primary_color;
    uint16_t secondary_color;
} note_dynamic_state_t;

static star_t *stars = NULL;
static note_dynamic_state_t *note_states = NULL;
static note_dynamic_state_t *drum_states = NULL;
static uint8_t *last_note_vals = NULL;
static uint64_t *last_drum_timestamps = NULL;

static float bg_phase = 0.0f;
static bool initialized = false;


/* Pick a spot far from the notes and drum shapes already on screen:
 * try a few random places and keep the one with the most room around it. */
static void pick_spot(const midi_values_t *mv, int self, bool drum, int *ox, int *oy)
{
    int best_x = VW / 2, best_y = VH / 2;
    int32_t best = -1;
    for (int c = 0; c < 12; c++) {
        int x = 30 + (rand() % (VW - 60));
        int y = 20 + (rand() % (VH - 40));
        int32_t nearest = INT32_MAX;
        for (int i = 0; i < 128; i++) {
            if (mv->note_values[i] > 0 && !(i == self && !drum)) {
                int dx = x - note_states[i].x, dy = (y - note_states[i].y) * 2;   /* the screen is wide: y counts double */
                int32_t d = dx * dx + dy * dy;
                if (d < nearest) {
                    nearest = d;
                }
            }
            if (drum_states[i].anim_timer < 20.0f && last_drum_timestamps[i] != 0 && !(i == self && drum)) {
                int dx = x - drum_states[i].x, dy = (y - drum_states[i].y) * 2;
                int32_t d = dx * dx + dy * dy;
                if (d < nearest) {
                    nearest = d;
                }
            }
        }
        if (nearest > best) {
            best = nearest;
            best_x = x;
            best_y = y;
        }
    }
    *ox = best_x;
    *oy = best_y;
}

static void init_visualiser(void) {
    if (stars == NULL) {
        stars = (star_t *)malloc(MAX_STARS * sizeof(star_t));
    }
    if (note_states == NULL) {
        note_states = (note_dynamic_state_t *)malloc(128 * sizeof(note_dynamic_state_t));
        if (note_states != NULL) {
            memset(note_states, 0, 128 * sizeof(note_dynamic_state_t));
        }
    }
    if (drum_states == NULL) {
        drum_states = (note_dynamic_state_t *)malloc(128 * sizeof(note_dynamic_state_t));
        if (drum_states != NULL) {
            memset(drum_states, 0, 128 * sizeof(note_dynamic_state_t));
        }
    }
    if (last_note_vals == NULL) {
        last_note_vals = (uint8_t *)malloc(128 * sizeof(uint8_t));
        if (last_note_vals != NULL) {
            memset(last_note_vals, 0, 128 * sizeof(uint8_t));
        }
    }
    if (last_drum_timestamps == NULL) {
        last_drum_timestamps = (uint64_t *)malloc(128 * sizeof(uint64_t));
        if (last_drum_timestamps != NULL) {
            memset(last_drum_timestamps, 0, 128 * sizeof(uint64_t));
        }
    }

    if (stars != NULL) {
        uint16_t star_colors[] = {COLOR_WHITE, COLOR_CYAN, COLOR_BLUE, COLOR_YELLOW};
        for (int i = 0; i < MAX_STARS; i++) {
            stars[i].x = (float)(rand() % DISPLAY_2_WIDTH);
            stars[i].y = (float)(rand() % DISPLAY_2_HEIGHT);
            stars[i].speed = 0.5f + ((float)(rand() % 15) / 10.0f);
            stars[i].color = star_colors[rand() % 4];
        }
    }

    bg_phase = 0.0f;
    initialized = true;
}

void display_visualiser_one_deinit(void) {
    if (stars != NULL) {
        free(stars);
        stars = NULL;
    }
    if (note_states != NULL) {
        free(note_states);
        note_states = NULL;
    }
    if (drum_states != NULL) {
        free(drum_states);
        drum_states = NULL;
    }
    if (last_note_vals != NULL) {
        free(last_note_vals);
        last_note_vals = NULL;
    }
    if (last_drum_timestamps != NULL) {
        free(last_drum_timestamps);
        last_drum_timestamps = NULL;
    }
    initialized = false;
}

void display_visualiser_one_draw(const midi_values_t *midi_values, char *message, bool display_active) {
    
    if (!initialized) {
        init_visualiser();
    }
    
    uint16_t color_bg = viz_bg(display_visualiser_one_background_color);
    
    if (display_active) {
        bg_phase += 0.05f;
    }

    // count active notes
    int active_notes = 0;
    for (int i = 0; i < 128; i++) {
        if (midi_values->note_values[i] > 0) active_notes++;
        if (midi_values->drum_values[i] > 0) active_notes++;
    }

    

    if (display_visualiser_one_background_oval) {
    
        uint16_t active_bg_col = viz_bg2(display_visualiser_one_background_color);
    
        float x_wave_cache[DISPLAY_2_WIDTH];
        float bg_phase_x = bg_phase * 0.7f;
        for (int x = 0; x < DISPLAY_2_WIDTH; x++) {
            x_wave_cache[x] = cosf(bg_phase_x + ((float)x * 0.01f));
        }

        for (int y = 0; y < DISPLAY_2_HEIGHT; y++) {
            int row_offset = y * DISPLAY_2_WIDTH;
            float wave = sinf(bg_phase + ((float)y * 0.0003f));
            
            for (int x = 0; x < DISPLAY_2_WIDTH; x++) {
                uint16_t bg_col = color_bg;
                if ((wave + x_wave_cache[x]) > 0.8f) {
                    bg_col = active_bg_col;
                }
                framebuffer_2[row_offset + x] = bg_col;
            }
        }
    
    } else {
        for (int i = 0; i < DISPLAY_2_HEIGHT * DISPLAY_2_WIDTH; i++) {
            framebuffer_2[i] = color_bg;
        }
    }
    
    // update and render drifting background stars
    if (stars != NULL) {
        for (int i = 0; i < MAX_STARS; i++) {
            
            if (display_active) {
                stars[i].x -= stars[i].speed;
                if (stars[i].x < 0) {
                    stars[i].x = (float)DISPLAY_2_WIDTH;
                    stars[i].y = (float)(rand() % DISPLAY_2_HEIGHT);
                }
            }
            int sx = (int)stars[i].x;
            int sy = (int)stars[i].y;
            if ((unsigned int)sx < DISPLAY_2_WIDTH && (unsigned int)sy < DISPLAY_2_HEIGHT) {
                framebuffer_2[sy * DISPLAY_2_WIDTH + sx] = stars[i].color;
            }
        }
    }

    uint16_t palette[] = {
        COLOR_CYAN, COLOR_MAGENTA, COLOR_YELLOW, 
        COLOR_ORANGE, COLOR_GREEN, COLOR_PURPLE, COLOR_BLUE
    };

    // process and render active midi note shapes
    if (note_states != NULL && last_note_vals != NULL) {
        for (int i = 0; i < 128; i++) {
            uint8_t current_val = midi_values->note_values[i];
            uint8_t last_val = last_note_vals[i];

            if (current_val > 0) {
                if (last_val == 0) {
                    pick_spot(midi_values, i, false, &note_states[i].x, &note_states[i].y);
                    note_states[i].style = rand() % 3;
                    note_states[i].anim_timer = 0.0f;
                    note_states[i].primary_color = palette[rand() % 7];
                    note_states[i].secondary_color = palette[rand() % 7];
                } else {
                    note_states[i].anim_timer += 0.15f;
                }

                int cx = note_states[i].x;
                int cy = note_states[i].y;
                
                float base_radius = 6.0f + ((float)current_val / 127.0f) * 14.0f;
                float pulse = sinf(note_states[i].anim_timer) * 4.0f;
                int max_radius = (int)(base_radius + pulse);
                if (max_radius < 4) max_radius = 4;

                int max_r_sq = max_radius * max_radius;

                int y_min = cy - max_radius; if (y_min < 0) y_min = 0;
                int y_max = cy + max_radius; if (y_max >= DISPLAY_2_HEIGHT) y_max = DISPLAY_2_HEIGHT - 1;
                
                int x_min = cx - max_radius; if (x_min < 0) x_min = 0;
                int x_max = cx + max_radius; if (x_max >= DISPLAY_2_WIDTH) x_max = DISPLAY_2_WIDTH - 1;

                for (int y = y_min; y <= y_max; y++) {
                    int row_offset = y * DISPLAY_2_WIDTH;
                    int dy = y - cy;
                    int dy_sq = dy * dy;

                    for (int x = x_min; x <= x_max; x++) {
                        int dx = x - cx;
                        int dist_sq = dx * dx + dy_sq;

                        if (dist_sq <= max_r_sq) {
                            uint16_t pixel_color = color_bg;

                            if (note_states[i].style == 0) {
                                if (dist_sq < (int)(max_r_sq * 0.09f)) {
                                    pixel_color = COLOR_WHITE;
                                } else if (dist_sq < (int)(max_r_sq * 0.36f)) {
                                    pixel_color = note_states[i].primary_color;
                                } else if (dist_sq < (int)(max_r_sq * 0.72f)) {
                                    pixel_color = note_states[i].secondary_color;
                                } else {
                                    pixel_color = COLOR_YELLOW;
                                }
                            } 
                            else if (note_states[i].style == 1) {
                                int ring = ((int)(sqrtf((float)dist_sq)) / 3) & 1;
                                if (dist_sq < (int)(max_r_sq * 0.06f)) {
                                    pixel_color = COLOR_YELLOW;
                                } else {
                                    pixel_color = (ring == 0) ? note_states[i].primary_color : note_states[i].secondary_color;
                                }
                            } 
                            else {
                                if (abs(dx) + abs(dy) <= max_radius) {
                                    if ((dx * dy) > 0) {
                                        pixel_color = note_states[i].primary_color;
                                    } else {
                                        pixel_color = note_states[i].secondary_color;
                                    }
                                    if (dist_sq < (int)(max_r_sq * 0.12f)) {
                                        pixel_color = COLOR_WHITE;
                                    }
                                } else {
                                    continue;
                                }
                            }

                            framebuffer_2[row_offset + x] = pixel_color;
                        }
                    }
                }
            }
            last_note_vals[i] = current_val;
        }
    }

    // process and render active midi drum shapes
    if (drum_states != NULL && last_drum_timestamps != NULL) {
        for (int i = 0; i < 128; i++) {
            uint8_t current_drum_val = midi_values->drum_values[i];
            uint64_t current_timestamp = midi_values->drum_timestamp_us[i];

            if (current_timestamp != last_drum_timestamps[i] && current_timestamp > 0) {
                pick_spot(midi_values, i, true, &drum_states[i].x, &drum_states[i].y);
                drum_states[i].style = rand() % 3;
                drum_states[i].anim_timer = 0.0f;
                drum_states[i].primary_color = palette[rand() % 7];
                drum_states[i].secondary_color = palette[rand() % 7];
                last_drum_timestamps[i] = current_timestamp;
            } else if (drum_states[i].anim_timer < 20.0f && current_timestamp != 0) {
                drum_states[i].anim_timer += 0.15f;
            }

            uint8_t render_val = (current_timestamp != 0 && drum_states[i].anim_timer < 5.0f) ? (current_drum_val > 0 ? current_drum_val : 100) : 0;
            if (render_val > 0) {
                int cx = drum_states[i].x;
                int cy = drum_states[i].y;

                float base_radius = 6.0f + ((float)render_val / 127.0f) * 14.0f;
                float pulse = sinf(drum_states[i].anim_timer) * 4.0f;
                int max_radius = (int)(base_radius + pulse);
                if (max_radius < 4) max_radius = 4;

                int max_r_sq = max_radius * max_radius;

                int y_min = cy - max_radius; if (y_min < 0) y_min = 0;
                int y_max = cy + max_radius; if (y_max >= DISPLAY_2_HEIGHT) y_max = DISPLAY_2_HEIGHT - 1;

                int x_min = cx - max_radius; if (x_min < 0) x_min = 0;
                int x_max = cx + max_radius; if (x_max >= DISPLAY_2_WIDTH) x_max = DISPLAY_2_WIDTH - 1;

                for (int y = y_min; y <= y_max; y++) {
                    int row_offset = y * DISPLAY_2_WIDTH;
                    int dy = y - cy;
                    int dy_sq = dy * dy;

                    for (int x = x_min; x <= x_max; x++) {
                        int dx = x - cx;
                        int dist_sq = dx * dx + dy_sq;

                        if (dist_sq <= max_r_sq) {
                            uint16_t pixel_color = color_bg;

                            if (drum_states[i].style == 0) {
                                if (dist_sq < (int)(max_r_sq * 0.09f)) {
                                    pixel_color = COLOR_WHITE;
                                } else if (dist_sq < (int)(max_r_sq * 0.36f)) {
                                    pixel_color = drum_states[i].primary_color;
                                } else if (dist_sq < (int)(max_r_sq * 0.72f)) {
                                    pixel_color = drum_states[i].secondary_color;
                                } else {
                                    pixel_color = COLOR_YELLOW;
                                }
                            } 
                            else if (drum_states[i].style == 1) {
                                int ring = ((int)(sqrtf((float)dist_sq)) / 3) & 1;
                                if (dist_sq < (int)(max_r_sq * 0.06f)) {
                                    pixel_color = COLOR_YELLOW;
                                } else {
                                    pixel_color = (ring == 0) ? drum_states[i].primary_color : drum_states[i].secondary_color;
                                }
                            } 
                            else {
                                if (abs(dx) + abs(dy) <= max_radius) {
                                    if ((dx * dy) > 0) {
                                        pixel_color = drum_states[i].primary_color;
                                    } else {
                                        pixel_color = drum_states[i].secondary_color;
                                    }
                                    if (dist_sq < (int)(max_r_sq * 0.12f)) {
                                        pixel_color = COLOR_WHITE;
                                    }
                                } else {
                                    continue;
                                }
                            }

                            framebuffer_2[row_offset + x] = pixel_color;
                        }
                    }
                }
            }
        }
    }
        
    

    if (message != NULL) {
        for (int y = 0; y < 24; y++) {
            for (int x = 0; x < DISPLAY_2_WIDTH; x++) {
                framebuffer_2[(y * DISPLAY_2_WIDTH) + x] = color_bg;
            }
        }
        graphic_display_text(framebuffer_2, message, 4, 4, 0xffff, color_bg);
    }

    display_2_draw();
}