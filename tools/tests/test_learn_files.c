/* (lengths may differ by 3 ms: the .mid ends each note one 480-PPQ tick early) */
/* Every built-in song written out as a .mid (tools/learn_songgen.py --midi) and read back through the
 * firmware MIDI loader must give the same notes, hands and bars as the built-in version. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "smf.h"
#include "ln_builtin.h"
#include "ln_song.h"
int main(int argc, char **argv) {
    int fails = 0;
    for (int i = 0; i < ln_builtin_count; i++) {
        ln_song_builtin(i);
        static ln_note_t a[4000]; int na = g_ln.n; memcpy(a, g_ln.notes, sizeof(ln_note_t) * na);
        int nb_a = g_ln.nbars; uint32_t len_a = g_ln.len_ms; uint32_t bar1 = g_ln.bars[1];
        char p[256]; snprintf(p, sizeof p, "%s/%02d_%s.mid", argv[1], i, ln_builtin[i].id);
        int err = ln_song_file(p);
        int same = 0, rh = 0, lh = 0;
        for (int k = 0; k < g_ln.n && k < na; k++) {
            const ln_note_t *b = &g_ln.notes[k];
            if (b->note == a[k].note && abs((int)b->t - (int)a[k].t) <= 1 && abs((int)b->len - (int)a[k].len) <= 3 && b->hand == a[k].hand) same++;
            rh += b->hand == LN_RH; lh += b->hand == LN_LH;
        }
        int ok = !err && g_ln.n == na && same == na && abs((int)g_ln.bars[1] - (int)bar1) <= 1;
        printf("  %-13s err %d notes %d/%d match %d  hands R%d L%d  bar1 %u/%u  len %u/%u %s\n", ln_builtin[i].id, err, g_ln.n, na, same, rh, lh,
               g_ln.bars[1], bar1, g_ln.len_ms, len_a, ok ? "ok" : "FAIL");
        fails += !ok; (void)nb_a;
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
