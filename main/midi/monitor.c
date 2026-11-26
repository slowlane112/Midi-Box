#include <string.h>
#include "esp_timer.h"
#include "monitor.h"
#include "v1_state.h"

monitor_t g_mon;

void monitor_note(uint8_t ch, uint8_t note, uint8_t vel, bool on)
{
    ch &= 0x0F;
    note &= 0x7F;
    if (on && vel) {
        g_mon.vel[ch][note] = vel;
        g_mon.on_vel[ch][note] = vel;
        g_mon.ons[ch][note]++;
        if (vel > g_mon.level[ch]) {
            g_mon.level[ch] = vel;
        }
        uint16_t e = g_mon.energy[note] + vel;
        g_mon.energy[note] = e > 127 ? 127 : (uint8_t)e;
        g_mon.last_ms[ch] = (uint32_t)(esp_timer_get_time() / 1000);
    } else {
        g_mon.vel[ch][note] = 0;
    }
    v1_state_note(ch, note, vel, on && vel);
}

void monitor_cc(uint8_t ch, uint8_t cc, uint8_t value)
{
    (void)ch;
    v1_state_cc(cc, value);
}

void monitor_init(void)
{
    for (int c = 0; c < 16; c++) {
        g_mon.program[c] = 0;
        g_mon.volume[c] = 100;
        g_mon.expression[c] = 127;
        g_mon.sustain[c] = 0;
        g_mon.bend[c] = 0;
    }
}

static void channel_msg(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint8_t ch = st & 0x0F;
    switch (st & 0xF0) {
    case 0xC0:
        g_mon.program[ch] = d1 & 0x7F;
        break;
    case 0xE0:
        g_mon.bend[ch] = (int16_t)(((int)(d1 | (d2 << 7)) - 8192) * 200 / 8192);
        break;
    case 0xB0:
        switch (d1) {
        case 7: g_mon.volume[ch] = d2; break;
        case 11: g_mon.expression[ch] = d2; break;
        case 64: g_mon.sustain[ch] = d2 >= 64; break;
        case 121:   /* reset all controllers */
            g_mon.expression[ch] = 127;
            g_mon.sustain[ch] = 0;
            g_mon.bend[ch] = 0;
            break;
        default: break;
        }
        break;
    default:
        break;
    }
}

void monitor_channel_msgs(const uint8_t *m, int len)
{
    int i = 0;
    while (i < len) {
        uint8_t st = m[i];
        if (st == 0xF0) {
            /* GM / GS reset: every channel back to its defaults */
            if (len - i >= 6 && m[i + 1] == 0x7E && m[i + 3] == 0x09) {
                monitor_init();
            } else if (len - i >= 9 && m[i + 1] == 0x41 && m[i + 5] == 0x40 && m[i + 6] == 0x00 && m[i + 7] == 0x7F) {
                monitor_init();
            }
            while (i < len && m[i] != 0xF7) {
                i++;
            }
            i++;
            continue;
        }
        if (st < 0x80 || st >= 0xF0) {
            i++;
            continue;
        }
        int n = ((st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0) ? 1 : 2;
        if (i + n >= len) {
            break;   /* cut short */
        }
        channel_msg(st, m[i + 1], n == 2 ? m[i + 2] : 0);
        i += 1 + n;
    }
}

void monitor_clear(void)
{
    /* notes only: the synth keeps its instruments and controllers, so do we */
    memset((void *)g_mon.vel, 0, sizeof(g_mon.vel));
    memset((void *)g_mon.level, 0, sizeof(g_mon.level));
    memset((void *)g_mon.energy, 0, sizeof(g_mon.energy));
    memset((void *)g_mon.last_ms, 0, sizeof(g_mon.last_ms));
    memset((void *)g_mon.ons, 0, sizeof(g_mon.ons));
    memset((void *)g_mon.on_vel, 0, sizeof(g_mon.on_vel));
    v1_state_clear();
}

void monitor_decay(void)
{
    for (int c = 0; c < 16; c++) {
        uint8_t l = g_mon.level[c];
        g_mon.level[c] = l > 4 ? l - 4 : 0;
    }
    for (int n = 0; n < 128; n++) {
        uint8_t e = g_mon.energy[n];
        g_mon.energy[n] = e > 3 ? e - 3 : 0;
    }
}

bool monitor_any_note(uint8_t note)
{
    for (int c = 0; c < 16; c++) {
        if (g_mon.vel[c][note]) {
            return true;
        }
    }
    return false;
}
