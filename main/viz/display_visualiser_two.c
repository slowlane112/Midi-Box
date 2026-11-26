#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <math.h>

#include "v1_midi.h"
#include "v1_compat.h"
#include "viz_common.h"


#define COLOR_WHITE    0xFFFF
#define COLOR_YELLOW   0xE0FF
#define COLOR_CYAN     0xFF07
#define COLOR_MAGENTA  0x1FF8
#define COLOR_ORANGE   0x20FF
#define COLOR_GREEN    0xE007
#define COLOR_PURPLE   0x3FBB
#define COLOR_BLUE     0x1F00
#define COLOR_DIM_BLUE 0x0841

#define COLOR_RED      0x42f9

#define MAX_EMBERS 64
#define TWO_PI 6.28318530718f

bool display_visualiser_two_background_oval = false;
uint8_t display_visualiser_two_background_color = 0;





const size_t display_visualiser_two_background_colors_count = VIZ_COLOURS + 1;   /* + Cycle */

typedef struct {
    float x, y, vx, vy;
    uint8_t life;
    uint16_t color;
} ember_t;

static ember_t *embers = NULL;
static uint8_t *previous_notes = NULL;
static uint64_t *previous_drum_timestamps = NULL;
static uint8_t *drum_life = NULL;
static uint8_t ember_cursor = 0;
static uint32_t frame_count = 0;
static bool initialized = false;

static const uint16_t note_colors[7] = {
    COLOR_CYAN, COLOR_MAGENTA, COLOR_YELLOW,
    COLOR_ORANGE, COLOR_GREEN, COLOR_PURPLE, COLOR_BLUE
};

static uint16_t note_color(unsigned note)
{
    return note_colors[(note / 12) % 7];
}

static void put_pixel(int x, int y, uint16_t color)
{
    if ((unsigned)x < DISPLAY_2_WIDTH && (unsigned)y < DISPLAY_2_HEIGHT)
        framebuffer_2[y * DISPLAY_2_WIDTH + x] = color;
}

static void draw_disc(int cx, int cy, int radius, uint16_t color)
{
    if (radius < 1) {
        put_pixel(cx, cy, color);
        return;
    }

    int r2 = radius * radius;
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x * x + y * y <= r2)
                put_pixel(cx + x, cy + y, color);
        }
    }
}

static void draw_ellipse_ring(int cx, int cy, int rx, int ry, uint16_t color, float phase)
{
    if (rx < 2 || ry < 2) return;

    for (int a = 0; a < 360; a += 2) {
        float angle = (float)a * (TWO_PI / 360.0f) + phase;
        int x = cx + (int)(cosf(angle) * rx);
        int y = cy + (int)(sinf(angle) * ry);
        uint16_t pixel = ((a / 2) % 8 < 3) ? color : COLOR_DIM_BLUE;
        put_pixel(x, y, pixel);
    }

    float highlight = phase * 2.4f;
    for (int i = 0; i < 13; i++) {
        float angle = highlight + (float)i * 0.035f;
        put_pixel(cx + (int)(cosf(angle) * rx), cy + (int)(sinf(angle) * ry), COLOR_CYAN);
    }
}

static float bg_phase = 0.0f;

static void draw_background(uint16_t bg, bool display_active)
{
    for (int i = 0; i < DISPLAY_2_WIDTH * DISPLAY_2_HEIGHT; i++)
        framebuffer_2[i] = bg;

    if (!display_visualiser_two_background_oval) return;

    int cx = DISPLAY_2_WIDTH / 2;
    int cy = DISPLAY_2_HEIGHT / 2;
    int rx = DISPLAY_2_WIDTH * 2 / 5;
    int ry = DISPLAY_2_HEIGHT * 2 / 5;
    
    if (display_active) {
        bg_phase = (float)frame_count * 0.015f;
    }
    
    draw_ellipse_ring(cx, cy, rx, ry, COLOR_WHITE, bg_phase);
    draw_ellipse_ring(cx, cy, rx * 3 / 4, ry * 3 / 4, viz_bg2(display_visualiser_two_background_color), -bg_phase * 1.7f);
    draw_ellipse_ring(cx, cy, rx * 1 / 2, ry * 1/ 2, viz_bg2(display_visualiser_two_background_color), -bg_phase * 2.7f);
}

static void spawn_embers(int x, int y, unsigned note, uint16_t color)
{
    if (embers == NULL) return;
    for (int i = 0; i < 4; i++) {
        ember_t *e = &embers[ember_cursor++ % MAX_EMBERS];
        float angle = (float)((note * 37u + (unsigned)i * 83u) % 360u) * (TWO_PI / 360.0f);
        float speed = 1.0f + (float)((note + (unsigned)i) % 4u) * 0.4f;

        e->x = (float)x;
        e->y = (float)y;
        e->vx = cosf(angle) * speed;
        e->vy = sinf(angle) * speed;
        e->life = 20;
        e->color = color;
    }
}

static void draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    for (;;) {
        put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

void display_visualiser_two_init(void)
{
    if (embers == NULL) {
        embers = (ember_t *)malloc(MAX_EMBERS * sizeof(ember_t));
        if (embers != NULL) {
            memset(embers, 0, MAX_EMBERS * sizeof(ember_t));
        }
    }
    if (previous_notes == NULL) {
        previous_notes = (uint8_t *)malloc(128 * sizeof(uint8_t));
        if (previous_notes != NULL) {
            memset(previous_notes, 0, 128 * sizeof(uint8_t));
        }
    }
    if (previous_drum_timestamps == NULL) {
        previous_drum_timestamps = (uint64_t *)malloc(128 * sizeof(uint64_t));
        if (previous_drum_timestamps != NULL) {
            memset(previous_drum_timestamps, 0, 128 * sizeof(uint64_t));
        }
    }
    if (drum_life == NULL) {
        drum_life = (uint8_t *)malloc(128 * sizeof(uint8_t));
        if (drum_life != NULL) {
            memset(drum_life, 0, 128 * sizeof(uint8_t));
        }
    }

    ember_cursor = 0;
    frame_count = 0;
    bg_phase = 0.0f;
    initialized = true;
}

void display_visualiser_two_deinit(void)
{
    if (embers != NULL) {
        free(embers);
        embers = NULL;
    }
    if (previous_notes != NULL) {
        free(previous_notes);
        previous_notes = NULL;
    }
    if (previous_drum_timestamps != NULL) {
        free(previous_drum_timestamps);
        previous_drum_timestamps = NULL;
    }
    if (drum_life != NULL) {
        free(drum_life);
        drum_life = NULL;
    }
    initialized = false;
}

void display_visualiser_two_draw(const midi_values_t *midi_values, char *message, bool display_active) {

    if (midi_values == NULL) return;

    if (!initialized) {
        display_visualiser_two_init();
    }

    frame_count++;

    uint8_t bg_index = display_visualiser_two_background_color;
    if (bg_index >= display_visualiser_two_background_colors_count) bg_index = 0;
    uint16_t bg = viz_bg(bg_index);

    draw_background(bg, display_active);

    for (unsigned note = 0; note < 128; note++) {
        uint8_t velocity = midi_values->note_values[note];
        if (velocity == 0) continue;

        const float spread = 2.4f;
        int x = DISPLAY_2_WIDTH / 2 + (int)(((float)note - 58.5f) * (DISPLAY_2_WIDTH - 8) / 127.0f * spread);
        if (x < 12) {x = 12;}
        if (x > DISPLAY_2_WIDTH - 12) {x = DISPLAY_2_WIDTH - 12;}
        int bands = DISPLAY_2_HEIGHT > 1 ? DISPLAY_2_HEIGHT : 1;
        int y = (int)((note * 37u + note / 12u * 11u) % (unsigned)bands);
        y += (int)(sinf((float)frame_count * 0.08f + note * 0.31f) * 3.0f);
        if (y < 12) {y = 12;}
        if (y > DISPLAY_2_HEIGHT - 12) {y = DISPLAY_2_HEIGHT - 12;}

        uint16_t color = note_color(note);
        int radius = 2 + velocity * 6 / 127;
        int pulse = (int)((frame_count + note * 3u) % 5u);

        draw_disc(x, y, radius + 3, COLOR_DIM_BLUE);
        draw_disc(x, y, radius + pulse / 2, color);
        draw_disc(x, y, radius / 2 + 1, COLOR_WHITE);

        if (previous_notes != NULL && previous_notes[note] == 0) spawn_embers(x, y, note, color);
    }

    for (unsigned note = 0; note < 128; note++) {
        uint8_t velocity = midi_values->drum_values[note];
        uint64_t timestamp = midi_values->drum_timestamp_us[note];

        if (previous_drum_timestamps != NULL && drum_life != NULL) {
            if (timestamp != previous_drum_timestamps[note] && velocity > 0) {
                drum_life[note] = 16;
                previous_drum_timestamps[note] = timestamp;
                
                const float spread = 2.4f;
                int x = DISPLAY_2_WIDTH / 2 + (int)(((float)note - 58.5f) * (DISPLAY_2_WIDTH - 8) / 127.0f * spread);
                if (x < 12) {x = 12;}
                if (x > DISPLAY_2_WIDTH - 12) {x = DISPLAY_2_WIDTH - 12;}
                
                int bands = DISPLAY_2_HEIGHT > 1 ? DISPLAY_2_HEIGHT : 1;
                int y = (int)((note * 41u + 13u) % (unsigned)bands);
                if (y < 12) {y = 12;}
                if (y > DISPLAY_2_HEIGHT - 12) {y = DISPLAY_2_HEIGHT - 12;}
                
                spawn_embers(x, y, note, COLOR_ORANGE);
            }

            if (drum_life[note] == 0) continue;
            drum_life[note]--;

            const float spread = 2.4f;
            int x = DISPLAY_2_WIDTH / 2 + (int)(((float)note - 58.5f) * (DISPLAY_2_WIDTH - 8) / 127.0f * spread);
            if (x < 12) {x = 12;}
            if (x > DISPLAY_2_WIDTH - 12) {x = DISPLAY_2_WIDTH - 12;}

            int bands = DISPLAY_2_HEIGHT > 1 ? DISPLAY_2_HEIGHT : 1;
            int y = (int)((note * 41u + 13u) % (unsigned)bands);
            if (y < 12) {y = 12;}
            if (y > DISPLAY_2_HEIGHT - 12) {y = DISPLAY_2_HEIGHT - 12;}

            uint16_t color = (note % 2 == 0) ? COLOR_PURPLE : COLOR_RED;
            int radius = 2 + velocity * 6 / 127;
            int pulse = (int)((frame_count + note * 3u) % 5u);

            draw_disc(x, y, radius + 3, COLOR_DIM_BLUE);
            draw_disc(x, y, radius + pulse / 2, color);
            draw_disc(x, y, radius / 2 + 1, COLOR_WHITE);
        }
    }

    if (embers != NULL) {
        for (int i = 0; i < MAX_EMBERS; i++) {
            ember_t *e = &embers[i];
            if (e->life == 0) continue;

            int old_x = (int)e->x;
            int old_y = (int)e->y;

            e->x += e->vx;
            e->y += e->vy;
            e->vx *= 0.94f;
            e->vy *= 0.94f;
            e->life--;

            uint16_t color = e->life > 13 ? e->color : e->life > 6 ? COLOR_CYAN : COLOR_DIM_BLUE;
            draw_line(old_x, old_y, (int)e->x, (int)e->y, color);
        }
    }

    if (previous_notes != NULL) {
        for (unsigned note = 0; note < 128; note++) {
            previous_notes[note] = midi_values->note_values[note];
        }
    }
    
    if (message != NULL) {
        for (int y = 0; y < 24; y++) {
            for (int x = 0; x < DISPLAY_2_WIDTH; x++) {
                framebuffer_2[(y * DISPLAY_2_WIDTH) + x] = bg;
            }
        }
        graphic_display_text(framebuffer_2, message, 4, 4, 0xffff, bg);
    }
    
    display_2_draw();
}