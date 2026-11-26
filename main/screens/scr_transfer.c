/* Transfer: receive files from a PC over Bluetooth into /sdcard/downloads (v1 protocol). */
#include <stdio.h>
#include <string.h>
#include "ft.h"
#include "sd.h"
#include "theme.h"
#include "widgets.h"
#include "app.h"

static uint32_t s_last;

static void enter(void *arg)
{
    (void)arg;
    ft_start();
}

static void leave(void)
{
    ft_stop();
}

static bool key(const btn_event_t *e)
{
    if (e->repeat) {
        return true;
    }
    ft_status_t st;
    ft_get(&st);
    switch (e->key) {
    case K_S1:
        if (st.state == FT_ASK) {
            ft_decline();
        }
        ui_pop();
        return true;
    case K_S4:
        if (st.state == FT_ASK) {
            ft_decline();
        }
        return true;
    case K_S5:
        if (st.state == FT_ASK) {
            ft_accept();
        } else if (st.state == FT_DONE || st.state == FT_FAILED) {
            ft_ack();
        }
        return true;
    default:
        return false;
    }
}

static void tick(uint32_t now)
{
    ft_status_t st;
    ft_get(&st);
    if (st.state == FT_SAVING) {
        ui_render_now();
        ft_service();
        ui_invalidate();
    }
    if (now - s_last > 150) {
        s_last = now;
        ui_invalidate();
    }
}

static void draw(gfx_t *g)
{
    ft_status_t st;
    ft_get(&st);
    static const char *const states[] = { "Off", "Waiting for your PC", "PC connected", "Incoming file",
                                          "Receiving", "Saving", "Saved", "Failed" };
    w_status(g, "Transfer", states[st.state], C_ACC);
    char size[16];
    fmt_size(size, sizeof(size), st.size);
    switch (st.state) {
    case FT_WAITING:
    case FT_CONNECTED:
    case FT_OFF:
        gfx_icon(g, &icon_file_transfer_64x64, (g->w - 64) / 2, 34, st.state == FT_CONNECTED ? C_ACC : C_DIM);
        gfx_text_box(g, &font_large, 0, 108, g->w, ALIGN_CENTER, st.state == FT_CONNECTED ? "PC connected" : "Ready for transfer", C_TEXT);
        gfx_text_box(g, &font_main, 0, 134, g->w, ALIGN_CENTER, "Send from your PC to \"MIDI BOX\"", C_DIM);
        gfx_text_box(g, &font_small, 0, 156, g->w, ALIGN_CENTER, "Files are saved in /downloads", C_DIM);
        break;
    case FT_ASK:
        gfx_text_box(g, &font_main, 10, 40, g->w - 20, ALIGN_LEFT, "Your PC wants to send:", C_DIM);
        gfx_text_box(g, &font_large, 10, 62, g->w - 20, ALIGN_LEFT, st.name, C_TEXT);
        gfx_text(g, &font_main, 10, 90, size, C_DIM);
        gfx_text_box(g, &font_main, 10, 130, g->w - 20, ALIGN_LEFT, "Accept to save it in /downloads", C_ACC);
        break;
    case FT_RECEIVING:
    case FT_SAVING: {
        gfx_text_box(g, &font_large, 10, 50, g->w - 20, ALIGN_LEFT, st.name, C_TEXT);
        w_progress(g, 10, 90, g->w - 20, 10, (uint32_t)st.received, (uint32_t)st.size, C_ACC);
        char line[48], got[16];
        fmt_size(got, sizeof(got), st.received);
        snprintf(line, sizeof(line), "%s of %s", got, size);
        gfx_text(g, &font_main, 10, 108, st.state == FT_SAVING ? "Saving to the SD card" UI_ELLIPSIS : line, C_DIM);
        break;
    }
    case FT_DONE:
        gfx_text_box(g, &font_large, 0, 70, g->w, ALIGN_CENTER, "Saved", C_ACC);
        gfx_text_box(g, &font_main, 10, 100, g->w - 20, ALIGN_CENTER, path_name(st.saved_path), C_TEXT);
        gfx_text_box(g, &font_small, 0, 122, g->w, ALIGN_CENTER, "in /downloads " UI_MIDDOT " open Files to play or move it", C_DIM);
        break;
    case FT_FAILED:
        gfx_text_box(g, &font_large, 0, 70, g->w, ALIGN_CENTER, "Transfer failed", C_REC);
        gfx_text_box(g, &font_main, 0, 100, g->w, ALIGN_CENTER, st.error[0] ? st.error : "The connection dropped", C_DIM);
        break;
    }
    const char *k4 = st.state == FT_ASK ? "Decline" : NULL;
    const char *k5 = st.state == FT_ASK ? "Accept" : ((st.state == FT_DONE || st.state == FT_FAILED) ? "OK" : NULL);
    const char *keys[5] = { "Back", NULL, NULL, k4, k5 };
    w_softkeys(g, keys, false);
}

static void draw2(gfx_t *g)
{
    ft_status_t st;
    ft_get(&st);
    gfx_clear(g, C_BG);
    gfx_text(g, &font_huge, 10, 8, "Transfer", C_ACC);
    char size[16], got[16], line[64];
    fmt_size(size, sizeof(size), st.size);
    switch (st.state) {
    case FT_ASK:
        gfx_text(g, &font_main, 12, 52, "Incoming file " UI_ENDASH " accept with 8", C_AMBER);
        gfx_text_box(g, &font_large, 12, 72, g->w - 24, ALIGN_LEFT, st.name, C_TEXT);
        gfx_text(g, &font_main, 12, 98, size, C_DIM);
        break;
    case FT_RECEIVING:
    case FT_SAVING: {
        gfx_text_box(g, &font_large, 12, 56, g->w - 24, ALIGN_LEFT, st.name, C_TEXT);
        w_progress(g, 12, 86, g->w - 24, 12, (uint32_t)st.received, (uint32_t)st.size, C_ACC);
        fmt_size(got, sizeof(got), st.received);
        unsigned pct = st.size ? (unsigned)((uint64_t)st.received * 100 / st.size) : 100;
        snprintf(line, sizeof(line), "%s of %s  " UI_MIDDOT "  %u%%", got, size, pct);
        gfx_text(g, &font_main, 12, 106, st.state == FT_SAVING ? "Saving to the SD card" UI_ELLIPSIS : line, C_DIM);
        break;
    }
    case FT_DONE:
        gfx_text(g, &font_large, 12, 56, "Saved", C_ACC);
        gfx_text_box(g, &font_main, 12, 82, g->w - 24, ALIGN_LEFT, path_name(st.saved_path), C_TEXT);
        gfx_text(g, &font_small, 12, 104, "in /downloads", C_DIM);
        break;
    case FT_FAILED:
        gfx_text(g, &font_large, 12, 56, "Transfer failed", C_REC);
        gfx_text_box(g, &font_main, 12, 82, g->w - 24, ALIGN_LEFT, st.error[0] ? st.error : "The connection dropped", C_DIM);
        break;
    default:
        gfx_text(g, &font_large, 12, 56, st.state == FT_CONNECTED ? "PC connected" : "Waiting for your PC", C_TEXT);
        gfx_text(g, &font_main, 12, 82, "Send to \"MIDI BOX\" with midi_box_transfer.py", C_DIM);
        gfx_text(g, &font_small, 12, 104, "Files are saved in /downloads", C_DIM);
        break;
    }
}

const screen_t scr_transfer = {
    .name = "transfer", .enter = enter, .leave = leave, .key = key, .tick = tick, .draw = draw, .draw2 = draw2,
};
