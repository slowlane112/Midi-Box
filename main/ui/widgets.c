#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "widgets.h"

static const int s_soft_cx[5] = { 22, 91, 160, 229, 298 };   /* centre of buttons 4..8 */
#define SOFT_EDGE 4   /* smallest gap between a label and the screen edge */

static char s_toast[48];
static uint32_t s_toast_until;
static int s_toast_volume = -1;

void w_toast_set(const char *text, uint32_t until_ms, int volume)
{
    snprintf(s_toast, sizeof(s_toast), "%s", text);
    s_toast_until = until_ms;
    s_toast_volume = volume;
}

void w_status(gfx_t *g, const char *title, const char *right, uint16_t title_col)
{
    gfx_fill(g, 0, 0, g->w, STATUS_H, C_PANEL);
    gfx_hline(g, 0, STATUS_H, g->w, C_LINE);
    if ((int32_t)(s_toast_until - ui_now()) > 0) {
        if (s_toast_volume >= 0) {
            gfx_text(g, &font_main, 6, 2, "Volume", C_ACC);
            for (int i = 0; i < 20; i++) {
                int x = 64 + i * 11;
                if (i < s_toast_volume) {
                    gfx_fill(g, x, 5, 8, 9, C_ACC);
                } else {
                    gfx_rect(g, x, 5, 8, 9, C_LINE);
                }
            }
            char v[8];
            snprintf(v, sizeof(v), "%d", s_toast_volume);
            gfx_text_right(g, &font_main, g->w - 6, 2, v, C_TEXT);
        } else {
            gfx_text_box(g, &font_main, 6, 2, g->w - 12, ALIGN_LEFT, s_toast, C_AMBER);
        }
        return;
    }
    int tw = gfx_text_box(g, &font_main, 6, 2, 200, ALIGN_LEFT, title, title_col);
    if (right && right[0]) {
        int x = 6 + tw + 8;
        gfx_text_box(g, &font_small, x, 4, g->w - x - 6, ALIGN_RIGHT, right, C_DIM);
    }
}

static void shape(gfx_t *g, const char *name, int cx, int y, uint16_t c)
{
    const int h = 11;
    if (!strcmp(name, "play")) {
        gfx_tri_right(g, cx - 3, y, h, c);
    } else if (!strcmp(name, "pause")) {
        gfx_fill(g, cx - 5, y, 3, h, c);
        gfx_fill(g, cx + 2, y, 3, h, c);
    } else if (!strcmp(name, "playpause")) {
        gfx_tri_right(g, cx - 9, y, h, c);
        gfx_fill(g, cx + 2, y, 3, h, c);
        gfx_fill(g, cx + 7, y, 3, h, c);
    } else if (!strcmp(name, "stop")) {
        gfx_fill(g, cx - 5, y, 10, h - 1, c);
    } else if (!strcmp(name, "ff")) {
        gfx_tri_right(g, cx - 7, y, h, c);
        gfx_tri_right(g, cx, y, h, c);
    } else if (!strcmp(name, "rew")) {
        gfx_tri_left(g, cx - 6, y, h, c);
        gfx_tri_left(g, cx + 1, y, h, c);
    } else if (!strcmp(name, "rec")) {
        gfx_circle(g, cx, y + 5, 5, C_REC);
    }
}

void w_softkeys(gfx_t *g, const char *const labels[5], bool shift)
{
    gfx_fill(g, 0, SOFT_Y, g->w, SOFT_H, shift ? C_ACC : C_PANEL);
    gfx_hline(g, 0, SOFT_Y, g->w, C_LINE);
    uint16_t col = shift ? C_BG : C_TEXT;
    for (int i = 0; i < 5; i++) {
        const char *k = labels[i];
        if (!k || !k[0]) {
            continue;
        }
        int cx = s_soft_cx[i];
        if (k[0] == '@') {
            shape(g, k + 1, cx, SOFT_Y + 6, col);
            continue;
        }
        /* centre the label on its button; only move it in if that would bring the
         * text closer than SOFT_EDGE px to the edge of the screen */
        int tw = gfx_text_width(&font_main, k);
        if (tw > SOFT_W) {
            tw = SOFT_W;
        }
        int x0 = cx - tw / 2;
        if (x0 < SOFT_EDGE) {
            x0 = SOFT_EDGE;
        }
        if (x0 + tw > g->w - SOFT_EDGE) {
            x0 = g->w - SOFT_EDGE - tw;
        }
        uint16_t c = (!shift && !strncmp(k, "Rec", 3)) ? C_REC : col;
        gfx_text_box(g, &font_main, x0, SOFT_Y + 4, tw, ALIGN_LEFT, k, c);
    }
}

void w_softkeys2(gfx_t *g, const char *const normal[5], const char *const shifted[5])
{
    bool sh = ui_shift() && shifted;
    w_softkeys(g, sh ? shifted : normal, sh);
}

void w_row(gfx_t *g, int y, int h, bool selected)
{
    if (selected) {
        gfx_fill(g, 0, y, g->w, h, C_ACC_BG);
        gfx_fill(g, 0, y, 3, h, C_ACC);
    }
}

void w_stat(gfx_t *g, int x, int y, int w, const char *label, const char *value, uint16_t vcol)
{
    gfx_fill(g, x, y, w, 30, C_PANEL);
    gfx_rect(g, x, y, w, 30, C_LINE);
    gfx_text_box(g, &font_small, x + 5, y + 2, w - 8, ALIGN_LEFT, label, C_DIM);
    gfx_text_box(g, &font_main, x + 5, y + 14, w - 8, ALIGN_LEFT, value, vcol);
}

void w_progress(gfx_t *g, int x, int y, int w, int h, uint32_t pos, uint32_t total, uint16_t c)
{
    gfx_fill(g, x, y, w, h, C_LINE);
    if (total) {
        uint64_t f = (uint64_t)pos * (uint64_t)w / total;
        gfx_fill(g, x, y, (int)(f > (uint64_t)w ? (uint64_t)w : f), h, c);
    }
}

void w_header2(gfx_t *g, const char *left, const char *right, uint16_t col)
{
    gfx_text_box(g, &font_small, 6, 3, 300, ALIGN_LEFT, left, col);
    if (right && right[0]) {
        gfx_text_right(g, &font_small, g->w - 6, 3, right, C_DIM);
    }
}

void w_panel(gfx_t *g, int x, int y, int w, int h, uint16_t border)
{
    gfx_fill(g, x, y, w, h, C_PANEL);
    gfx_rect(g, x, y, w, h, border);
}

int w_list_top(int sel, int count, int rows)
{
    if (count <= rows) {
        return 0;
    }
    int top = sel - rows / 2;
    if (top < 0) {
        top = 0;
    }
    if (top > count - rows) {
        top = count - rows;
    }
    return top;
}

void w_scrollbar(gfx_t *g, int y, int h, int top, int rows, int count)
{
    if (count <= rows) {
        return;
    }
    int bh = h * rows / count;
    if (bh < 8) {
        bh = 8;
    }
    int by = y + (h - bh) * top / (count - rows);
    gfx_fill(g, g->w - 3, y, 3, h, C_PANEL);
    gfx_fill(g, g->w - 3, by, 3, bh, C_DIM);
}

void fmt_time(char *out, int n, uint32_t ms)
{
    uint32_t s = ms / 1000;
    if (s >= 3600) {
        snprintf(out, (size_t)n, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    } else {
        snprintf(out, (size_t)n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    }
}

void fmt_size(char *out, int n, uint64_t b)
{
    if (b < 1024) {
        snprintf(out, (size_t)n, "%u B", (unsigned)b);
    } else if (b < 1024 * 1024) {
        snprintf(out, (size_t)n, "%u KB", (unsigned)((b + 1023) / 1024));
    } else if (b < 1024ULL * 1024 * 1024) {
        snprintf(out, (size_t)n, "%u.%u MB", (unsigned)(b >> 20), (unsigned)((b * 10 >> 20) % 10));
    } else {
        snprintf(out, (size_t)n, "%u.%u GB", (unsigned)(b >> 30), (unsigned)((b * 10 >> 30) % 10));
    }
}

/* 5-point star, filled (scanline fill of the 10-point outline; integer maths only) */
void w_star(gfx_t *g, int cx, int cy, int r, uint16_t c)
{
    static const int16_t ux[10] = { 0, 588, 951, 951, 588, 0, -588, -951, -951, -588 };
    static const int16_t uy[10] = { -1000, -809, -309, 309, 809, 1000, 809, 309, -309, -809 };
    int px[10], py[10];
    for (int k = 0; k < 10; k++) {
        int rr = (k % 2 == 0) ? r * 1000 : r * 450;
        px[k] = cx * 1000 + ux[k] * rr / 1000;
        py[k] = cy * 1000 + uy[k] * rr / 1000;
    }
    for (int y = cy - r; y <= cy + r; y++) {
        int ys = y * 1000 + 500, xs[10], n = 0;
        for (int k = 0; k < 10; k++) {
            int x1 = px[k], y1 = py[k], x2 = px[(k + 1) % 10], y2 = py[(k + 1) % 10];
            if ((y1 <= ys && ys < y2) || (y2 <= ys && ys < y1)) {
                xs[n++] = x1 + (int)((int64_t)(ys - y1) * (x2 - x1) / (y2 - y1));
            }
        }
        for (int i = 1; i < n; i++) {   /* sort (n <= 10) */
            for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) {
                int t = xs[j];
                xs[j] = xs[j - 1];
                xs[j - 1] = t;
            }
        }
        for (int i = 0; i + 1 < n; i += 2) {
            int a = (xs[i] + 500) / 1000, b = (xs[i + 1] + 500) / 1000;
            gfx_hline(g, a, y, b - a + 1, c);
        }
    }
}

/* row of n stars of which 'lit' are gold */
void w_stars(gfx_t *g, int x, int cy, int r, int n, int lit, uint16_t on, uint16_t off)
{
    for (int i = 0; i < n; i++) {
        w_star(g, x + r + i * (r * 2 + r / 2 + 1), cy, r, i < lit ? on : off);
    }
}

/* ---- scrolling names ---- */
#define SCROLL_HOLD_MS  3000
#define SCROLL_STEP_MS  400
enum { SCROLL_HOLD_START, SCROLL_RUN, SCROLL_HOLD_END };

void w_scroll_reset(w_scroll_t *s)
{
    s->key[0] = '\0';
    s->pos = 0;
}

bool w_scroll_tick(w_scroll_t *s, const font_t *f, const char *text, int w, uint32_t now)
{
    if (!text) {
        text = "";
    }
    if (strncmp(s->key, text, sizeof(s->key) - 1) != 0) {
        snprintf(s->key, sizeof(s->key), "%s", text);
        s->pos = 0;
        s->state = SCROLL_HOLD_START;
        s->at = now;
        return true;
    }
    if (gfx_text_width(f, text) <= w) {
        s->pos = 0;
        return false;   /* fits: nothing to scroll */
    }
    int len = (int)strlen(text), max_pos = 0;
    while (max_pos < len && gfx_text_width(f, text + max_pos) > w) {
        max_pos++;
    }
    switch (s->state) {
    case SCROLL_HOLD_START:
        if (now - s->at >= SCROLL_HOLD_MS) {
            s->state = SCROLL_RUN;
            s->at = now;
        }
        return false;
    case SCROLL_RUN:
        if (now - s->at < SCROLL_STEP_MS) {
            return false;
        }
        s->at = now;
        if (s->pos < max_pos) {
            s->pos++;
        }
        if (s->pos >= max_pos) {
            s->state = SCROLL_HOLD_END;
        }
        return true;
    default:   /* SCROLL_HOLD_END */
        if (now - s->at < SCROLL_HOLD_MS) {
            return false;
        }
        s->pos = 0;
        s->state = SCROLL_HOLD_START;
        s->at = now;
        return true;
    }
}

void w_text_scroll(gfx_t *g, const font_t *f, int x, int y, int w, const w_scroll_t *s, const char *text, uint16_t c)
{
    if (!text) {
        return;
    }
    int len = (int)strlen(text);
    int pos = (s && strncmp(s->key, text, sizeof(s->key) - 1) == 0 && s->pos < len) ? s->pos : 0;
    /* only the characters that fit: a plain cut, no "..." */
    char part[128];
    int n = 0;
    part[0] = '\0';
    while (text[pos + n] && n < (int)sizeof(part) - 1) {
        part[n] = text[pos + n];
        part[n + 1] = '\0';
        if (gfx_text_width(f, part) > w) {
            part[n] = '\0';
            break;
        }
        n++;
    }
    gfx_text(g, f, x, y, part, c);
}
