/*
 * ln_ui.h - what the Learn screens share: your keyboard range, the Play Along settings,
 * and drawing helpers.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "gfx.h"
#include "ln_engine.h"

enum { LH_RIGHT, LH_LEFT, LH_BOTH };

typedef struct {
    int hand;            /* LH_* */
    int mode;            /* LN_MODE_WAIT / LN_MODE_PLAY */
    int speed;           /* percent */
    int start_bar;       /* 0-based */
    int countin;         /* bars */
    int builtin;         /* index of the built-in song, -1 = a MIDI file */
} ln_setup_t;

extern ln_setup_t g_lset;

/* The Results screen is shared. Play Along leaves this NULL; Exercises set it while they run. */
typedef struct {
    void (*retry)(int faster);     /* start again, faster > 0: a little faster */
    bool (*next)(void);            /* go on to the next one; false if there is none */
    bool (*has_next)(void);
    bool (*can_faster)(void);
    void (*done)(void);            /* leave Results */
} ln_res_ops_t;
extern const ln_res_ops_t *g_lres_ops;
extern uint8_t g_kb_lo, g_kb_hi;     /* your keyboard; 0 = not checked yet */
extern bool g_kb_known;

void ln_ui_load_keyboard(void);
unsigned ln_ui_hands(void);           /* bit mask for the engine */
const char *ln_ui_hand_name(int hand);
void ln_ui_note_name(char *out, int n, int note);
/* spelled as written: "Bb4", "E#5" (acc as ln_note_t.acc) */
void ln_ui_note_name_acc(char *out, int n, int note, int acc);
/* the semitone shift that fits the chosen hands on your keyboard, and how many notes stay out */
int ln_ui_fit(int *out_of_reach, uint8_t *lo, uint8_t *hi);
/* "b:elise|R|play" */
void ln_ui_score_key(char *out, int n);

/* keyboard from lo to hi (widened to whole octaves), colour per key (0 = normal).
 * Returns the key width; x positions via ln_ui_key_x(). */
typedef struct {
    int lo, hi, x0, y, h, kw;
} ln_kb_t;
void ln_ui_kb_layout(ln_kb_t *k, int lo, int hi, int x, int w, int y, int h);
void ln_ui_kb_draw(gfx_t *g, const ln_kb_t *k, const uint16_t *colour /* [128] or NULL */, int dim_lo, int dim_hi);
bool ln_ui_key_x(const ln_kb_t *k, int note, int *x, int *w);   /* false if outside */

/* ------------------------------------------------------------------ grand staff (screen 2)
 * Treble and bass staves 8 px apart per line, middle C halfway (y 70), as in the mockups. */
#define LN_STAFF_C4_Y 70
#define LN_STAFF_STEP 4
#define LN_STAFF_LINE RGB(0x5C, 0x66, 0x75)
#define LN_STAFF_LEDGER RGB(0x9A, 0xA4, 0xB2)
void ln_ui_staff(gfx_t *g, int x0, int x1, uint16_t treble, uint16_t bass);
/* staff position: C4 = 28, one per line or space; acc as ln_note_t.acc */
int ln_ui_staff_pos(int note, int acc);
int ln_ui_staff_y(int pos);
/* note head with ledger lines and its sharp / flat; big = twice the size */
void ln_ui_staff_note(gfx_t *g, int x, int note, int acc, uint16_t c, bool big);
