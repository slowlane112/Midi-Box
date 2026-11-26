#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "theme.h"
#include "widgets.h"
#include "dialog.h"

/* ================================================================ menu */

static struct {
    const char *const *items;
    int count, sel;
    void (*on_pick)(int);
} s_menu;

static void menu_draw(gfx_t *g)
{
    ui_draw_below(g);
    int w = 136, ih = 20;
    int h = 8 + s_menu.count * ih;
    int x = g->w - w - 6;
    int y = CONTENT_Y + (SOFT_Y - CONTENT_Y - h) / 2;
    w_panel(g, x, y, w, h, C_ACC);
    for (int i = 0; i < s_menu.count; i++) {
        int yy = y + 4 + i * ih;
        bool sel = i == s_menu.sel;
        if (sel) {
            gfx_fill(g, x + 3, yy, w - 6, ih - 1, C_ACC);
        }
        bool danger = !strcmp(s_menu.items[i], "Delete");
        gfx_text_box(g, &font_main, x + 10, yy + 3, w - 20, ALIGN_LEFT, s_menu.items[i],
                     sel ? C_BG : (danger ? C_REC : C_TEXT));
    }
    static const char *const keys[5] = { "Close", NULL, NULL, NULL, "Choose" };
    w_softkeys(g, keys, false);
}

static bool menu_key(const btn_event_t *e)
{
    switch (e->key) {
    case K_UP:
        s_menu.sel = (s_menu.sel + s_menu.count - 1) % s_menu.count;
        return true;
    case K_DOWN:
        s_menu.sel = (s_menu.sel + 1) % s_menu.count;
        return true;
    case K_S1:
        ui_pop();
        return true;
    case K_S5:
        if (!e->repeat) {
            int sel = s_menu.sel;
            void (*cb)(int) = s_menu.on_pick;
            ui_pop();
            if (cb) {
                cb(sel);
            }
        }
        return true;
    default:
        return false;
    }
}

static const screen_t s_menu_screen = {
    .name = "menu", .key = menu_key, .draw = menu_draw, .draw2 = ui_draw2_below,
};

void dlg_menu(const char *const *items, int count, int sel, void (*on_pick)(int index))
{
    s_menu.items = items;
    s_menu.count = count;
    s_menu.sel = sel;
    s_menu.on_pick = on_pick;
    ui_push(&s_menu_screen, NULL);
}

/* ================================================================ confirm */

static struct {
    char title[48];
    char text[96];
    char action[16];
    void (*on_yes)(void);
} s_conf;

static void conf_draw(gfx_t *g)
{
    ui_draw_below(g);
    int x = 24, y = 60, w = g->w - 48, h = 96;
    w_panel(g, x, y, w, h, C_ACC);
    gfx_text_box(g, &font_main, x + 10, y + 8, w - 20, ALIGN_LEFT, s_conf.title, C_ACC);
    /* wrap onto two lines at the last space that fits */
    const char *t = s_conf.text;
    int maxw = w - 20;
    char line[96];
    int split = -1;
    if (gfx_text_width(&font_main, t) > maxw) {
        for (int i = 0; t[i]; i++) {
            if (t[i] != ' ') {
                continue;
            }
            snprintf(line, sizeof(line), "%.*s", i, t);
            if (gfx_text_width(&font_main, line) > maxw) {
                break;
            }
            split = i;
        }
    }
    if (split < 0) {
        gfx_text_box(g, &font_main, x + 10, y + 34, maxw, ALIGN_LEFT, t, C_TEXT);
    } else {
        snprintf(line, sizeof(line), "%.*s", split, t);
        gfx_text_box(g, &font_main, x + 10, y + 34, maxw, ALIGN_LEFT, line, C_TEXT);
        gfx_text_box(g, &font_main, x + 10, y + 52, maxw, ALIGN_LEFT, t + split + 1, C_TEXT);
    }
    const char *keys[5] = { "Cancel", NULL, NULL, NULL, s_conf.action };
    w_softkeys(g, keys, false);
}

static bool conf_key(const btn_event_t *e)
{
    if (e->repeat) {
        return true;
    }
    if (e->key == K_S1) {
        ui_pop();
        return true;
    }
    if (e->key == K_S5) {
        void (*cb)(void) = s_conf.on_yes;
        ui_pop();
        if (cb) {
            cb();
        }
        return true;
    }
    return false;
}

static const screen_t s_conf_screen = {
    .name = "confirm", .key = conf_key, .draw = conf_draw, .draw2 = ui_draw2_below,
};

void dlg_confirm(const char *title, const char *text, const char *action, void (*on_yes)(void))
{
    snprintf(s_conf.title, sizeof(s_conf.title), "%s", title);
    snprintf(s_conf.text, sizeof(s_conf.text), "%s", text);
    snprintf(s_conf.action, sizeof(s_conf.action), "%s", action);
    s_conf.on_yes = on_yes;
    ui_push(&s_conf_screen, NULL);
}

/* ================================================================ value */

static dlg_value_t s_val;

static void val_draw(gfx_t *g)
{
    ui_draw_below(g);
    int x = 40, y = 64, w = g->w - 80, h = 92;
    w_panel(g, x, y, w, h, C_ACC);
    gfx_text_box(g, &font_main, x + 10, y + 8, w - 20, ALIGN_LEFT, s_val.title, C_ACC);
    char v[48];
    s_val.format(v, sizeof(v));
    gfx_text_box(g, &font_large, x + 10, y + 34, w - 20, ALIGN_CENTER, v, C_TEXT);
    gfx_tri_left(g, x + 12, y + 38, 11, C_DIM);
    gfx_tri_right(g, x + w - 18, y + 38, 11, C_DIM);
    if (s_val.hint) {
        gfx_text_box(g, &font_small, x + 10, y + 66, w - 20, ALIGN_CENTER, s_val.hint, C_DIM);
    }
    static const char *const keys[5] = { "Cancel", "-", "+", NULL, "OK" };
    w_softkeys(g, keys, false);
}

static bool val_key(const btn_event_t *e)
{
    switch (e->key) {
    case K_S2:
    case K_DOWN:
        s_val.change(e->shift ? -10 : -1);
        return true;
    case K_S3:
    case K_UP:
        s_val.change(e->shift ? 10 : 1);
        return true;
    case K_S1:
    case K_S5:
        if (!e->repeat) {
            bool ok = e->key == K_S5;
            void (*cb)(bool) = s_val.on_done;
            ui_pop();
            if (cb) {
                cb(ok);
            }
        }
        return true;
    default:
        return false;
    }
}

static const screen_t s_val_screen = {
    .name = "value", .key = val_key, .draw = val_draw, .draw2 = ui_draw2_below,
};

void dlg_value(const dlg_value_t *v)
{
    s_val = *v;
    ui_push(&s_val_screen, NULL);
}

/* ================================================================ list */

static dlg_list_t s_list;

static void list_draw(gfx_t *g)
{
    w_status(g, s_list.title, NULL, C_ACC);
    int top = w_list_top(s_list.sel, s_list.count, LIST_ROWS);
    for (int r = 0; r < LIST_ROWS && top + r < s_list.count; r++) {
        int i = top + r, y = CONTENT_Y + r * ROW_H;
        bool sel = i == s_list.sel;
        w_row(g, y, ROW_H, sel);
        char t[64];
        s_list.item(i, t, sizeof(t));
        gfx_text_box(g, &font_main, 10, y + 4, g->w - 20, ALIGN_LEFT, t, sel ? C_ACC : C_TEXT);
    }
    w_scrollbar(g, CONTENT_Y, LIST_ROWS * ROW_H, top, LIST_ROWS, s_list.count);
    static const char *const keys[5] = { "Back", "Page up", "Page dn", NULL, "Choose" };
    w_softkeys(g, keys, false);
}

static void list_move(int d)
{
    int s = s_list.sel + d;
    if (s < 0) {
        s = 0;
    }
    if (s >= s_list.count) {
        s = s_list.count - 1;
    }
    if (s != s_list.sel) {
        s_list.sel = s;
        if (s_list.on_move) {
            s_list.on_move(s);
        }
    }
}

static bool list_key(const btn_event_t *e)
{
    switch (e->key) {
    case K_UP: list_move(-1); return true;
    case K_DOWN: list_move(1); return true;
    case K_S2: list_move(-LIST_ROWS); return true;
    case K_S3: list_move(LIST_ROWS); return true;
    case K_S1:
        if (!e->repeat) {
            ui_pop();
        }
        return true;
    case K_S5:
        if (!e->repeat) {
            int sel = s_list.sel;
            void (*cb)(int) = s_list.on_pick;
            ui_pop();
            if (cb) {
                cb(sel);
            }
        }
        return true;
    default:
        return false;
    }
}

static const screen_t s_list_screen = {
    .name = "list", .key = list_key, .draw = list_draw, .draw2 = ui_draw2_below,
};

void dlg_list(const dlg_list_t *l)
{
    s_list = *l;
    ui_push(&s_list_screen, NULL);
}

/* ================================================================ text entry */

#define TXT_MAX 64
static const char s_charset[] =
    " abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    "-_.()&!'+,#";
static const int s_groups[] = { 1, 27, 53, 63 };   /* a, A, 0, symbols */

static struct {
    char title[32];
    char buf[TXT_MAX + 1];
    int cur;
    void (*on_done)(const char *);
} s_txt;

static int charset_index(char c)
{
    const char *p = strchr(s_charset, c);
    return (p && c) ? (int)(p - s_charset) : 0;
}

static void txt_draw(gfx_t *g)
{
    w_status(g, s_txt.title, "2/3 letter " UI_MIDDOT " Shift+2/3 a/A/0", C_ACC);
    int len = (int)strlen(s_txt.buf);
    const font_t *f = &font_large;
    int x0 = 10, maxw = g->w - 20;

    /* scroll so the cursor stays visible */
    char head[TXT_MAX + 1];
    snprintf(head, sizeof(head), "%.*s", s_txt.cur, s_txt.buf);
    int cx = gfx_text_width(f, head);
    int shift = cx > maxw - 24 ? cx - (maxw - 24) : 0;

    gfx_fill(g, 4, 80, g->w - 8, 30, C_PANEL);
    gfx_rect(g, 4, 80, g->w - 8, 30, C_LINE);
    gfx_text(g, f, x0 - shift, 86, s_txt.buf, C_TEXT);

    /* cursor box on the current character (or after the end) */
    char cur[2] = { s_txt.cur < len ? s_txt.buf[s_txt.cur] : ' ', 0 };
    int cw = gfx_text_width(f, cur);
    if (cw < 8) {
        cw = 8;
    }
    int bx = x0 - shift + cx - 1;
    gfx_rect(g, bx, 83, cw + 2, 24, C_ACC);

    /* character wheel above / below the cursor */
    int ci = charset_index(cur[0]);
    int n = (int)sizeof(s_charset) - 1;
    char up[2] = { s_charset[(ci + n - 1) % n], 0 }, dn[2] = { s_charset[(ci + 1) % n], 0 };
    gfx_text(g, &font_main, bx + 2, 62, up[0] == ' ' ? "_" : up, C_DIM);
    gfx_text(g, &font_main, bx + 2, 114, dn[0] == ' ' ? "_" : dn, C_DIM);

    char info[32];
    snprintf(info, sizeof(info), "%d / %d", len, TXT_MAX);
    gfx_text(g, &font_small, 10, 150, info, C_DIM);
    gfx_text(g, &font_small, 10, 166, "Shift+Del clears the name", C_DIM);

    static const char *const keys[5] = { "Cancel", "@rew", "@ff", "Del", "OK" };
    w_softkeys(g, keys, false);
}

static void txt_set_char(int idx)
{
    int len = (int)strlen(s_txt.buf);
    if (s_txt.cur >= len) {
        if (len >= TXT_MAX) {
            return;
        }
        s_txt.buf[len] = s_charset[idx];
        s_txt.buf[len + 1] = '\0';
    } else {
        s_txt.buf[s_txt.cur] = s_charset[idx];
    }
}

static bool txt_key(const btn_event_t *e)
{
    int len = (int)strlen(s_txt.buf);
    int n = (int)sizeof(s_charset) - 1;
    char c = s_txt.cur < len ? s_txt.buf[s_txt.cur] : 0;
    int ci = c ? charset_index(c) : 0;
    switch (e->key) {
    case K_UP:
    case K_DOWN: {
        int dir = e->key == K_UP ? -1 : 1;
        if (e->shift) {
            int gcount = (int)(sizeof(s_groups) / sizeof(s_groups[0]));
            int gi = 0;
            for (int i = 0; i < gcount; i++) {
                if (ci >= s_groups[i]) {
                    gi = i;
                }
            }
            gi = (gi + gcount + dir) % gcount;
            txt_set_char(s_groups[gi]);
        } else {
            txt_set_char(c ? (ci + n + dir) % n : (dir > 0 ? 1 : n - 1));
        }
        return true;
    }
    case K_S2:
        if (s_txt.cur > 0) {
            s_txt.cur--;
        }
        return true;
    case K_S3:
        if (s_txt.cur < len) {
            s_txt.cur++;
        }
        return true;
    case K_S4:
        if (e->shift) {
            s_txt.buf[0] = '\0';
            s_txt.cur = 0;
        } else if (s_txt.cur > 0) {
            memmove(s_txt.buf + s_txt.cur - 1, s_txt.buf + s_txt.cur, (size_t)(len - s_txt.cur + 1));
            s_txt.cur--;
        }
        return true;
    case K_S1:
        if (!e->repeat) {
            ui_pop();
        }
        return true;
    case K_S5:
        if (!e->repeat) {
            /* trim spaces at both ends */
            char *b = s_txt.buf;
            while (*b == ' ') {
                b++;
            }
            int l = (int)strlen(b);
            while (l > 0 && b[l - 1] == ' ') {
                b[--l] = '\0';
            }
            if (l == 0) {
                ui_toast("Name can't be empty");
                return true;
            }
            char out[TXT_MAX + 1];
            snprintf(out, sizeof(out), "%s", b);
            void (*cb)(const char *) = s_txt.on_done;
            ui_pop();
            if (cb) {
                cb(out);
            }
        }
        return true;
    default:
        return false;
    }
}

static const screen_t s_txt_screen = {
    .name = "text", .key = txt_key, .draw = txt_draw, .draw2 = ui_draw2_below,
};

void dlg_text(const char *title, const char *initial, void (*on_done)(const char *text))
{
    snprintf(s_txt.title, sizeof(s_txt.title), "%s", title);
    snprintf(s_txt.buf, sizeof(s_txt.buf), "%s", initial ? initial : "");
    s_txt.cur = (int)strlen(s_txt.buf);
    s_txt.on_done = on_done;
    ui_push(&s_txt_screen, NULL);
}

/* ================================================================ busy */

static struct {
    bool on;
    char text[48];
    uint32_t done, total;
} s_busy;

void dlg_busy(const char *text, uint32_t done, uint32_t total)
{
    s_busy.on = true;
    snprintf(s_busy.text, sizeof(s_busy.text), "%s", text);
    s_busy.done = done;
    s_busy.total = total;
    ui_render_now();
}

void dlg_busy_end(void)
{
    s_busy.on = false;
    ui_invalidate();
}

void dlg_draw_overlay(gfx_t *g)
{
    if (!s_busy.on) {
        return;
    }
    int x = 40, y = 80, w = g->w - 80, h = 60;
    w_panel(g, x, y, w, h, C_ACC);
    gfx_text_box(g, &font_main, x + 10, y + 10, w - 20, ALIGN_LEFT, s_busy.text, C_TEXT);
    if (s_busy.total) {
        w_progress(g, x + 10, y + 36, w - 20, 8, s_busy.done, s_busy.total, C_ACC);
    }
}
