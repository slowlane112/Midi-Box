#include <string.h>
#include "monitor.h"
#include "vz.h"

void vz_scan(vz_scan_t *s, vz_note_fn fn, void *ctx)
{
    for (int c = 0; c < 16; c++) {
        for (int n = 0; n < 128; n++) {
            uint8_t bit = (uint8_t)(1u << (n & 7));
            bool was = (s->held[c][n >> 3] & bit) != 0;
            uint8_t ons = g_mon.ons[c][n];
            bool now = g_mon.vel[c][n] != 0;
            if (ons != s->ons[c][n]) {
                /* new note-on(s): close the previous one first, then report the new one */
                s->ons[c][n] = ons;
                if (was) {
                    fn(c, n, 0, false, ctx);
                }
                fn(c, n, g_mon.on_vel[c][n], true, ctx);
                was = true;
            }
            if (was && !now) {
                fn(c, n, 0, false, ctx);
                was = false;
            }
            if (was) {
                s->held[c][n >> 3] |= bit;
            } else {
                s->held[c][n >> 3] &= (uint8_t)~bit;
            }
        }
    }
}

void vz_scan_reset(vz_scan_t *s)
{
    memset(s->held, 0, sizeof(s->held));
    for (int c = 0; c < 16; c++) {
        for (int n = 0; n < 128; n++) {
            s->ons[c][n] = g_mon.ons[c][n];
        }
    }
}
