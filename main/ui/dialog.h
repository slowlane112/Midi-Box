/*
 * dialog.h - pop-ups drawn over the current screen.
 * All are screens pushed on the UI stack; callbacks run after the dialog has closed,
 * so a callback may open another dialog or screen.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Action menu (Files). on_pick(index) after closing; not called on cancel. */
void dlg_menu(const char *const *items, int count, int sel, void (*on_pick)(int index));

/* Yes/No. 'action' is the label on button 8, e.g. "Delete". */
void dlg_confirm(const char *title, const char *text, const char *action, void (*on_yes)(void));

/* Value with - / +. format() writes the value text; change(delta) changes it live;
 * on_done(ok) when closed (ok=false: caller should restore the old value). */
typedef struct {
    const char *title;
    const char *hint;                 /* optional small text */
    void (*format)(char *out, int n);
    void (*change)(int delta);
    void (*on_done)(bool ok);
} dlg_value_t;
void dlg_value(const dlg_value_t *v);

/* Full-screen pick list (e.g. 128 programs). */
typedef struct {
    const char *title;
    int count;
    int sel;
    void (*item)(int index, char *out, int n);
    void (*on_pick)(int index);       /* not called on cancel */
    void (*on_move)(int index);       /* optional: preview while moving */
} dlg_list_t;
void dlg_list(const dlg_list_t *l);

/* Text entry for file names. on_done(text) only on OK. */
void dlg_text(const char *title, const char *initial, void (*on_done)(const char *text));

/* Message drawn immediately (for blocking work); dlg_busy_end() redraws normally. */
void dlg_busy(const char *text, uint32_t done, uint32_t total);
void dlg_busy_end(void);

/* called by the UI after drawing screen 1 (draws the busy message if active) */
#include "gfx.h"
void dlg_draw_overlay(gfx_t *g);
