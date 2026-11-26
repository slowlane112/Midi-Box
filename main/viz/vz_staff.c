/*
 * Staff: the notes being played, written on a scrolling grand staff (treble + bass),
 * with sharps, ledger lines and a line showing how long each note is held.
 *
 * Button 10: which channel to show (All channels except drums, then 1-16).
 * Button 11: note colour.
 */
#include <stdlib.h>
#include <string.h>
#include "monitor.h"
#include "theme.h"
#include "icons.h"
#include "viz_common.h"
#include "vz.h"

#define EVENTS   200
#define Y_C4     76          /* middle C */
#define STEP     4           /* pixels per diatonic step (half a line gap) */
#define LEFT     38          /* staff content starts after the clefs */
#define NOW_X    (VW - 24)
#define TOP_Y    6
#define BOT_Y    (VH - 4)

typedef struct {
    int32_t start, end;      /* frames; end < 0 while held */
    int8_t d;                /* diatonic position, C4 = 28 */
    uint8_t sharp, ch, vel, shift;
} sev_staff_t;

typedef struct {
    sev_staff_t ev[EVENTS];
    vz_scan_t scan;
} staff_mem_t;
static staff_mem_t *s_m;   /* only while Staff is on screen */
#define s_ev   (s_m->ev)
#define s_scan (s_m->scan)
static int s_head;
static int32_t s_frame;
static int s_filter;         /* -1 = all melodic channels */

static const uint8_t s_step[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
static const uint8_t s_black[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };

static int y_of(int d) { return Y_C4 - (d - 28) * STEP; }

static void on_note(int ch, int note, int vel, bool on, void *ctx)
{
    (void)ctx;
    if ((s_filter < 0 && ch == 9) || (s_filter >= 0 && ch != s_filter)) {
        return;
    }
    int d = (note / 12 - 1) * 7 + s_step[note % 12];
    if (on) {
        sev_staff_t *e = &s_ev[s_head];
        s_head = (s_head + 1) % EVENTS;
        *e = (sev_staff_t){ .start = s_frame, .end = -1, .d = (int8_t)d, .sharp = s_black[note % 12],
                            .ch = (uint8_t)ch, .vel = (uint8_t)vel, .shift = 0 };
        /* a second next to another note of the same chord is moved aside, as in printed music */
        for (int i = 0; i < EVENTS; i++) {
            const sev_staff_t *o = &s_ev[i];
            if (o != e && o->start == s_frame && (o->d == d + 1 || o->d == d - 1) && !o->shift) {
                e->shift = 9;
                break;
            }
        }
        return;
    }
    for (int i = 0; i < EVENTS; i++) {
        sev_staff_t *e = &s_ev[i];
        if (e->end < 0 && e->start >= 0 && e->d == d && e->ch == ch && e->sharp == s_black[note % 12]) {
            e->end = s_frame;
        }
    }
}

static void head(int x, int y, uint16_t c)
{
    /* 9 x 7 note head, slightly tilted like engraved music */
    static const uint16_t rows[7] = { 0x03C, 0x0FE, 0x1FF, 0x1FF, 0x1FF, 0x0FE, 0x078 };
    for (int r = 0; r < 7; r++) {
        for (int b = 0; b < 9; b++) {
            if (rows[r] & (0x100 >> b)) {
                viz_px(x - 4 + b, y - 3 + r, c);
            }
        }
    }
}

static void ledgers(int x, int d, uint16_t c)
{
    if (d == 28) {
        viz_fill(x - 7, y_of(28), 15, 1, c);
    }
    for (int l = 40; l <= d; l += 2) {           /* above the treble staff: A5, C6, E6 ... */
        viz_fill(x - 7, y_of(l), 15, 1, c);
    }
    for (int l = 16; l >= d; l -= 2) {          /* below the bass staff: E2, C2, A1 ... */
        viz_fill(x - 7, y_of(l), 15, 1, c);
    }
}

void vz_staff_free(void)
{
    free(s_m);
    s_m = NULL;
}

void vz_staff_draw(int colour, int opt, bool active, bool fresh)
{
    (void)active;
    int filter = opt == 0 ? -1 : opt - 1;
    if (!s_m) {
        if (!(s_m = viz_alloc(sizeof(staff_mem_t)))) {
            viz_clear(viz_bg(0));
            return;
        }
        fresh = true;
    }
    if (fresh || filter != s_filter) {
        memset(s_ev, 0, sizeof(s_ev));
        for (int i = 0; i < EVENTS; i++) {
            s_ev[i].start = -1;
        }
        vz_scan_reset(&s_scan);
        s_frame = 0;
        s_filter = filter;
    }
    s_frame++;
    vz_scan(&s_scan, on_note, NULL);

    uint16_t bg = viz_bg(0);
    uint16_t line = gfx_blend(0xFFFF, bg, 110);
    uint16_t ledger = gfx_blend(0xFFFF, bg, 150);
    viz_clear(bg);

    /* staves, clefs */
    for (int i = 0; i < 5; i++) {
        viz_fill(4, y_of(30 + i * 2), VW - 8, 1, line);    /* treble: E4 G4 B4 D5 F5 */
        viz_fill(4, y_of(18 + i * 2), VW - 8, 1, line);    /* bass: G2 B2 D3 F3 A3 */
    }
    viz_fill(4, y_of(38), 1, y_of(18) - y_of(38) + 1, line);   /* brace line joining the staves */
    gfx_t g = { framebuffer_2, VW, VH };
    gfx_icon(&g, &icon_clef_treble, 9, y_of(38) - 9, C_TEXT);
    gfx_icon(&g, &icon_clef_bass, 7, y_of(26) - 1, C_TEXT);
    viz_fill(NOW_X + 6, y_of(38), 1, y_of(18) - y_of(38) + 1, gfx_blend(0xFFFF, bg, 60));

    /* notes */
    for (int i = 0; i < EVENTS; i++) {
        const sev_staff_t *e = &s_ev[i];
        if (e->start < 0) {
            continue;
        }
        int x = NOW_X - (int)(s_frame - e->start) + e->shift;
        int32_t end = e->end < 0 ? s_frame : e->end;
        int xe = NOW_X - (int)(s_frame - end);
        if (xe < LEFT) {
            continue;
        }
        int d = e->d;
        int y = y_of(d);
        bool above = y < TOP_Y, below = y > BOT_Y;
        if (above) {
            y = TOP_Y;
        }
        if (below) {
            y = BOT_Y;
        }
        uint16_t c = viz_col(colour == 0 ? viz_hue_of_channel(e->ch) : colour);
        uint16_t tail = gfx_blend(c, bg, 90);
        if (xe > x + 4) {
            viz_fill(x + 4, y - 1, xe - x - 4, 3, tail);          /* how long it is held */
        }
        if (x >= LEFT) {
            if (!above && !below) {
                ledgers(x, d, ledger);
            }
            head(x, y, gfx_blend(c, bg, (uint8_t)(120 + e->vel)));
            if (e->sharp) {
                gfx_text(&g, &font_main, x - 14, y - 8, "#", c);
            }
            if (above || below) {
                gfx_text(&g, &font_small, x - 3, above ? y - 2 : y - 11, above ? "^" : "v", C_DIM);
            }
        }
    }

    char lbl[20];
    if (s_filter < 0) {
        strcpy(lbl, "All channels");
    } else {
        lbl[0] = 'C'; lbl[1] = 'h'; lbl[2] = ' ';
        int n = s_filter + 1;
        if (n >= 10) {
            lbl[3] = (char)('0' + n / 10);
            lbl[4] = (char)('0' + n % 10);
            lbl[5] = 0;
        } else {
            lbl[3] = (char)('0' + n);
            lbl[4] = 0;
        }
    }
    gfx_text_right(&g, &font_small, VW - 6, 3, lbl, C_DIM);
}
