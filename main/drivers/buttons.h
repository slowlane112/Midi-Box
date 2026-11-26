/*
 * buttons.h - 13 front-panel buttons, read every 2 ms by their own task and debounced.
 *
 * Key numbers match the panel: 1 = Shift, 2/3 = up/down, 4..8 = soft keys
 * under screen 1 (left to right), 9..11 = visualiser keys, 12/13 = volume.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

enum {
    K_SHIFT = 1,
    K_UP = 2,       /* previous item */
    K_DOWN = 3,     /* next item */
    K_S1 = 4,       /* soft keys, left to right */
    K_S2 = 5,
    K_S3 = 6,
    K_S4 = 7,
    K_S5 = 8,
    K_VIZ_L = 9,
    K_VIZ_M = 10,
    K_VIZ_R = 11,
    K_VOL_UP = 12,
    K_VOL_DN = 13,
};

typedef struct {
    uint8_t key;     /* 1..13 */
    bool shift;      /* Shift (button 1) was held */
    bool repeat;     /* auto-repeat while held (first press has repeat=false) */
} btn_event_t;

void buttons_init(void);   /* starts the 2 ms button task */
/* Takes queued events (never blocks). Returns the number written to out (max n). */
int buttons_poll(uint32_t now_ms, btn_event_t *out, int n);
bool buttons_shift_held(void);
void buttons_sample(uint32_t now_ms, uint32_t raw);   /* used by the task; exposed for tests */
