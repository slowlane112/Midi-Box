/*
 * Live In: play a keyboard through the SAM2695 from DIN 1 (back) or DIN 2 (front).
 * Bluetooth MIDI and recording into Studio arrive in phase 2.
 */
#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "midi_out.h"
#include "midi_in.h"
#include "monitor.h"
#include "gm.h"
#include "theme.h"
#include "widgets.h"
#include "dialog.h"
#include "viz.h"
#include "ble.h"
#include "ble_midi.h"
#include "app.h"

static int s_sel;          /* 0 = DIN 1, 1 = DIN 2, 2 = Bluetooth */
static uint8_t s_program;
static uint32_t s_last;

static void start_input(void)
{
    midi_in_set_thru(g_settings.thru);
    midi_in_start(g_settings.din_port);
}

static void enter(void *arg)
{
    (void)arg;
    s_sel = g_settings.din_port;
    monitor_clear();
    midi_out_reset(g_settings.bank, g_settings.volume);
    s_program = 0;
    start_input();
    viz_set_active(true);
}

static void leave(void)
{
    midi_in_stop();
    midi_out_all_notes_off();
    monitor_clear();
    viz_set_active(false);
}

static void prog_item(int i, char *out, int n) { snprintf(out, (size_t)n, "%03d  %s", i + 1, gm_program_name((uint8_t)i)); }
static void prog_preview(int i) { midi_out_program(0, (uint8_t)i); }
static void prog_pick(int i)
{
    s_program = (uint8_t)i;
    midi_out_program(0, s_program);
}

static bool key(const btn_event_t *e)
{
    if (e->repeat && e->key != K_UP && e->key != K_DOWN) {
        return true;
    }
    switch (e->key) {
    case K_UP: s_sel = (s_sel + 2) % 3; return true;
    case K_DOWN: s_sel = (s_sel + 1) % 3; return true;
    case K_S1: ui_pop(); return true;
    case K_S2:
        if (s_sel == 2) {
            bool conn = ble_midi_state == BLE_MIDI_CONNECTED || ble_midi_state == BLE_MIDI_CONNECTING;
            if (conn) {
                ble_midi_disconnect();
            } else if (!e->shift && g_settings.bt_name[0]) {
                ble_addr.type = g_settings.bt_addr_type;   /* reconnect to the last device */
                memcpy(ble_addr.val, g_settings.bt_addr, 6);
                snprintf(ble_name, sizeof(ble_name), "%s", g_settings.bt_name);
                ble_midi_connect(&ble_addr, ble_own_addr_type);
            } else {
                ui_push(&scr_bt, NULL);
            }
        } else if (e->shift) {
            ui_push(&scr_bt, NULL);
        } else if (midi_in_running() && g_settings.din_port == s_sel) {
            midi_in_stop();
            midi_out_all_notes_off();
        } else {
            g_settings.din_port = (uint8_t)s_sel;   /* one UART: DIN 1 and DIN 2 take turns */
            settings_changed();
            start_input();
        }
        return true;
    case K_S3:
        g_settings.bank = g_settings.bank ? BANK_GM : BANK_MT32;
        settings_changed();
        midi_out_set_bank(g_settings.bank);
        ui_toast("Sound bank: %s", g_settings.bank ? "Roland MT-32" : "General MIDI");
        return true;
    case K_S4:
        if (e->shift) {
            midi_out_all_notes_off();
            monitor_clear();
            ui_toast("All notes off");
        } else {
            static dlg_list_t l;
            l = (dlg_list_t){ .title = "Program (channel 1)", .count = 128, .sel = s_program,
                              .item = prog_item, .on_pick = prog_pick, .on_move = prog_preview };
            dlg_list(&l);
        }
        return true;
    case K_S5:
        if (e->shift) {
            g_settings.thru = !g_settings.thru;
            settings_changed();
            midi_in_set_thru(g_settings.thru);
            ui_toast("Keyboard sound %s", g_settings.thru ? "on" : "off");
        } else {
            ui_push(&scr_projects, NULL);
        }
        return true;
    default:
        return false;
    }
}

static void tick(uint32_t now)
{
    if (now - s_last > 100) {
        s_last = now;
        ui_invalidate();
    }
}

static void describe(const midi_in_stats_t *st, char *out, int n)
{
    if (!st->msg_count) {
        snprintf(out, (size_t)n, "Nothing received yet");
        return;
    }
    static const char *const notes[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    uint8_t s = st->last[0], t = s & 0xF0;
    int ch = (s & 0x0F) + 1;
    const char *src = st->last_src == MIDI_SRC_DIN2 ? "DIN 2" : (st->last_src == MIDI_SRC_BLE ? "BT" : "DIN 1");
    if ((t == 0x90 || t == 0x80) && st->last_len == 3) {
        int nn = st->last[1];
        snprintf(out, (size_t)n, "Last: Note %s %s%d vel %d " UI_MIDDOT " ch %d " UI_MIDDOT " %s",
                 (t == 0x90 && st->last[2]) ? "On" : "Off", notes[nn % 12], nn / 12 - 1, st->last[2], ch, src);
    } else if (t == 0xB0) {
        snprintf(out, (size_t)n, "Last: CC %d = %d " UI_MIDDOT " ch %d " UI_MIDDOT " %s", st->last[1], st->last[2], ch, src);
    } else if (t == 0xC0) {
        snprintf(out, (size_t)n, "Last: Program %d " UI_MIDDOT " ch %d " UI_MIDDOT " %s", st->last[1] + 1, ch, src);
    } else {
        snprintf(out, (size_t)n, "Last: %02X " UI_MIDDOT " %s", s, src);
    }
}

static void draw(gfx_t *g)
{
    char right[32];
    snprintf(right, sizeof(right), "Sound %s", g_settings.thru ? "on" : "off");
    w_status(g, "Live In", right, C_ACC);

    midi_in_stats_t st;
    midi_in_get_stats(&st);
    uint32_t now = ui_now();
    bool recent = st.msg_count && now - st.last_ms < 400;
    bool running = midi_in_running();

    static const char *const names[3] = { "DIN 1 (back)", "DIN 2 (front)", "Bluetooth" };
    for (int i = 0; i < 3; i++) {
        int y = CONTENT_Y + 4 + i * 26;
        bool sel = i == s_sel;
        bool on = i < 2 && running && g_settings.din_port == i;
        if (i == 2) {
            on = ble_midi_state == BLE_MIDI_CONNECTED;
        }
        const char *state;
        if (i < 2) {
            state = !on ? "off" : (recent && st.last_src == i ? "receiving" : "listening");
        } else if (ble_midi_state == BLE_MIDI_CONNECTED) {
            state = ble_name;
        } else if (ble_midi_state == BLE_MIDI_CONNECTING) {
            state = "connecting" UI_ELLIPSIS;
        } else {
            state = g_settings.bt_name[0] ? "off (5 reconnects)" : "off (5 to pair)";
        }
        gfx_fill(g, 6, y, 308, 23, sel ? C_ACC_BG : C_PANEL);
        gfx_rect(g, 6, y, 308, 23, sel ? C_ACC : C_LINE);
        gfx_circle(g, 18, y + 11, 4, on ? C_ACC : C_LINE);
        gfx_text_box(g, &font_main, 30, y + 5, 110, ALIGN_LEFT, names[i], C_TEXT);
        gfx_text_box(g, &font_main, 142, y + 5, 164, ALIGN_RIGHT, state, on ? C_ACC : C_DIM);
    }
    char prog[40];
    snprintf(prog, sizeof(prog), "%03d %s", s_program + 1, gm_program_name(s_program));
    w_stat(g, 6, 106, 150, "Sound bank", g_settings.bank ? "Roland MT-32" : "General MIDI", C_TEXT);
    w_stat(g, 162, 106, 152, "Program (ch 1)", prog, C_TEXT);

    gfx_text(g, &font_small, 6, 144, "Channel activity", C_DIM);
    for (int c = 0; c < 16; c++) {
        bool lit = g_mon.last_ms[c] && now - g_mon.last_ms[c] < 300;
        int x = 6 + c * 19;
        gfx_fill(g, x, 158, 16, 16, lit ? C_ACC : C_PANEL);
        gfx_rect(g, x, 158, 16, 16, C_LINE);
        char n[4];
        snprintf(n, sizeof(n), "%d", c + 1);
        gfx_text_box(g, &font_small, x, 160, 16, ALIGN_CENTER, n, lit ? C_BG : C_DIM);
    }
    char last[64];
    describe(&st, last, sizeof(last));
    gfx_text_box(g, &font_small, 6, 182, 308, ALIGN_LEFT, last, C_DIM);

    bool sel_on = s_sel < 2 ? (running && g_settings.din_port == s_sel)
                            : (ble_midi_state == BLE_MIDI_CONNECTED || ble_midi_state == BLE_MIDI_CONNECTING);
    const char *normal[5] = { "Back", sel_on ? "Off" : "On", "Bank", "Program", "Rec" };
    static const char *const shifted[5] = { NULL, "Pair", NULL, "Panic", "Sound" };
    w_softkeys2(g, normal, shifted);
}

const screen_t scr_livein = {
    .name = "livein", .enter = enter, .leave = leave, .key = key, .tick = tick, .draw = draw,
};
