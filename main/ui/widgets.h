/* widgets.h - shared pieces of every screen (see tools/mockup.py for the same layout). */
#pragma once
#include "gfx.h"
#include "theme.h"

/* Status bar: title left, small text right. Shows the toast / volume bar when active. */
void w_status(gfx_t *g, const char *title, const char *right, uint16_t title_col);

/* Soft-key bar over buttons 4..8. 5 labels; NULL = empty. "@play" "@pause" "@playpause"
 * "@stop" "@ff" "@rew" "@rec" draw transport shapes. Shift-held labels are drawn by
 * passing the shift set and shift=true (accent bar). */
void w_softkeys(gfx_t *g, const char *const labels[5], bool shift);
/* convenience: picks normal or shift labels from the current Shift state */
void w_softkeys2(gfx_t *g, const char *const normal[5], const char *const shifted[5]);

void w_row(gfx_t *g, int y, int h, bool selected);
void w_stat(gfx_t *g, int x, int y, int w, const char *label, const char *value, uint16_t vcol);
void w_progress(gfx_t *g, int x, int y, int w, int h, uint32_t pos, uint32_t total, uint16_t c);
void w_header2(gfx_t *g, const char *left, const char *right, uint16_t col);
void w_panel(gfx_t *g, int x, int y, int w, int h, uint16_t border);
void w_star(gfx_t *g, int cx, int cy, int r, uint16_t c);
void w_stars(gfx_t *g, int x, int cy, int r, int n, int lit, uint16_t on, uint16_t off);
/* scrolling list: returns first visible index so 'sel' stays on screen */
int w_list_top(int sel, int count, int rows);
/* scroll bar on the right edge of a list */
void w_scrollbar(gfx_t *g, int y, int h, int top, int rows, int count);

void fmt_time(char *out, int n, uint32_t ms);
void fmt_size(char *out, int n, uint64_t bytes);

/* internal: toast state, set by ui_toast */
void w_toast_set(const char *text, uint32_t until_ms, int volume);
