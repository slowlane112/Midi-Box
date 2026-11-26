/*
 * Bluetooth MIDI: scan for devices, connect, see the connection state.
 * Incoming notes go through midi_in (thru to the synth, visualisers, Studio recording).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ble.h"
#include "ble_scan.h"
#include "ble_midi.h"
#include "settings.h"
#include "theme.h"
#include "widgets.h"
#include "app.h"

static int s_sel;
static int s_order[BLE_SCAN_MAX_DEVICES];
static int s_count;
static uint32_t s_last;
static bool s_connecting_here;

static int by_rssi(const void *a, const void *b)
{
    const ble_scan_device_t *x = &ble_scan_devices[*(const int *)a], *y = &ble_scan_devices[*(const int *)b];
    return y->rssi - x->rssi;
}

static void refresh(void)
{
    s_count = 0;
    for (int i = 0; i < BLE_SCAN_MAX_DEVICES; i++) {
        if (ble_scan_devices[i].used) {
            s_order[s_count++] = i;
        }
    }
    qsort(s_order, (size_t)s_count, sizeof(int), by_rssi);
    if (s_sel >= s_count) {
        s_sel = s_count ? s_count - 1 : 0;
    }
}

static void enter(void *arg)
{
    (void)arg;
    s_sel = 0;
    s_connecting_here = false;
    if (ble_midi_state != BLE_MIDI_CONNECTED && ble_midi_state != BLE_MIDI_CONNECTING) {
        ble_scan_start();
    }
}

static void leave(void)
{
    ble_scan_stop();
}

static void connect_selected(void)
{
    if (s_count == 0) {
        return;
    }
    ble_scan_device_t *d = &ble_scan_devices[s_order[s_sel]];
    ble_scan_stop();
    snprintf(ble_name, sizeof(ble_name), "%s", d->name);
    ble_addr = d->addr;
    g_settings.bt_addr_type = d->addr.type;
    memcpy(g_settings.bt_addr, d->addr.val, 6);
    snprintf(g_settings.bt_name, sizeof(g_settings.bt_name), "%s", d->name);
    settings_changed();
    s_connecting_here = true;
    ble_midi_connect(&ble_addr, ble_own_addr_type);
}

static bool key(const btn_event_t *e)
{
    switch (e->key) {
    case K_UP:
        if (s_count) {
            s_sel = (s_sel + s_count - 1) % s_count;
        }
        return true;
    case K_DOWN:
        if (s_count) {
            s_sel = (s_sel + 1) % s_count;
        }
        return true;
    case K_S1:
        if (!e->repeat) {
            ui_pop();
        }
        return true;
    case K_S2:
        if (!e->repeat) {
            ble_scan_stop();
            ble_scan_start();
            s_sel = 0;
        }
        return true;
    case K_S4:
        if (!e->repeat && (ble_midi_state == BLE_MIDI_CONNECTED || ble_midi_state == BLE_MIDI_CONNECTING)) {
            ble_midi_disconnect();
        }
        return true;
    case K_S5:
        if (!e->repeat) {
            connect_selected();
        }
        return true;
    default:
        return false;
    }
}

static void tick(uint32_t now)
{
    if (now - s_last > 250) {
        s_last = now;
        refresh();
        ui_invalidate();
    }
    if (s_connecting_here && ble_midi_state == BLE_MIDI_CONNECTED) {
        s_connecting_here = false;
        ui_toast("Connected to %s", ble_name);
        ui_pop();
    }
}

static const char *state_text(void)
{
    switch (ble_midi_state) {
    case BLE_MIDI_CONNECTING: return "Connecting" UI_ELLIPSIS;
    case BLE_MIDI_CONNECTED: return "Connected";
    case BLE_MIDI_CONNECT_ERROR: return "Could not connect";
    case BLE_MIDI_NO_MIDI_ERROR: return "Not a MIDI device";
    case BLE_MIDI_DISCONNECTED: return "Disconnected";
    default: return "Scanning" UI_ELLIPSIS;
    }
}

static void draw(gfx_t *g)
{
    w_status(g, "Bluetooth MIDI", state_text(), C_ACC);
    if (ble_midi_state == BLE_MIDI_CONNECTED || ble_midi_state == BLE_MIDI_CONNECTING) {
        gfx_icon(g, &icon_bluetooth_64x64, (g->w - 64) / 2, 40, C_ACC);
        gfx_text_box(g, &font_large, 0, 116, g->w, ALIGN_CENTER, ble_name, C_TEXT);
        gfx_text_box(g, &font_main, 0, 142, g->w, ALIGN_CENTER, state_text(), C_DIM);
    } else {
        int top = w_list_top(s_sel, s_count, LIST_ROWS);
        for (int r = 0; r < LIST_ROWS && top + r < s_count; r++) {
            int i = top + r, y = CONTENT_Y + r * ROW_H;
            const ble_scan_device_t *d = &ble_scan_devices[s_order[i]];
            bool sel = i == s_sel;
            w_row(g, y, ROW_H, sel);
            gfx_icon(g, &icon_bluetooth_16x16, 8, y + 3, sel ? C_ACC : C_DIM);
            char rssi[12];
            snprintf(rssi, sizeof(rssi), "%d dB", d->rssi);
            int rw = gfx_text_width(&font_small, rssi);
            gfx_text_right(g, &font_small, g->w - 8, y + 6, rssi, C_DIM);
            gfx_text_box(g, &font_main, 30, y + 4, g->w - 46 - rw, ALIGN_LEFT, d->name, sel ? C_ACC : C_TEXT);
        }
        w_scrollbar(g, CONTENT_Y, LIST_ROWS * ROW_H, top, LIST_ROWS, s_count);
        if (s_count == 0) {
            gfx_text_box(g, &font_main, 0, 90, g->w, ALIGN_CENTER, "Looking for devices" UI_ELLIPSIS, C_DIM);
            gfx_text_box(g, &font_small, 0, 110, g->w, ALIGN_CENTER, "Put your keyboard in Bluetooth pairing mode", C_DIM);
        }
    }
    bool conn = ble_midi_state == BLE_MIDI_CONNECTED || ble_midi_state == BLE_MIDI_CONNECTING;
    const char *keys[5] = { "Back", conn ? NULL : "Rescan", NULL, conn ? "Disconn" : NULL, conn ? NULL : "Connect" };
    w_softkeys(g, keys, false);
}

const screen_t scr_bt = {
    .name = "bt", .enter = enter, .leave = leave, .key = key, .tick = tick, .draw = draw,
};
