#include <stdio.h>
#include <string.h>
#include "song.h"
#include "smf.h"
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } else passes++; } while (0)
static int fails, passes;
static void note(int t, uint32_t tick, uint32_t len, int n, int v) { sev_t e = { tick, len, EV_NOTE, (uint8_t)n, (uint8_t)v, 0 }; ev_insert(&g_song.tr[t], &e); }
int main(void) {
    song_init();
    song_new("Test Song");
    CHECK(g_song.ntracks == 1 && g_song.tr[0].channel == 0);
    int bass = song_track_add("Bass", song_free_channel(), 33);
    CHECK(bass == 1 && g_song.tr[1].channel == 1);
    int drums = song_track_add("Drums", 9, 0);
    CHECK(song_free_channel() == 2);
    /* slightly late notes on beats 1-4 of bar 1 */
    note(0, 10, 400, 60, 100); note(0, 490, 400, 62, 90); note(0, 975, 400, 64, 80); note(0, 1450, 440, 65, 70);
    note(bass, 0, 1900, 36, 100);
    note(drums, 0, 60, 36, 120); note(drums, 960, 60, 38, 110);
    CHECK(g_song.tr[0].n == 4 && g_song.tr[0].ev[0].tick == 10 && g_song.tr[0].ev[3].tick == 1450);
    /* quantise to quarter notes, 100% */
    quant_t q = { SONG_PPQ, 100, 0, false };
    int moved; uint32_t worst = edit_quantise_preview(0, 0, 1920, &q, &moved);
    CHECK(moved == 4 && worst == 15);
    edit_quantise(0, 0, 1920, &q);
    CHECK(g_song.tr[0].ev[0].tick == 0 && g_song.tr[0].ev[1].tick == 480 && g_song.tr[0].ev[2].tick == 960 && g_song.tr[0].ev[3].tick == 1440);
    /* undo then redo */
    song_undo(); CHECK(g_song.tr[0].ev[0].tick == 10);
    song_undo(); CHECK(g_song.tr[0].ev[0].tick == 0);
    /* 50% strength moves half way */
    note(0, 2000, 100, 70, 60);  /* 80 ticks late of 1920 */
    quant_t h = { SONG_PPQ, 50, 0, false }; edit_quantise(0, 1920, 3840, &h);
    CHECK(g_song.tr[0].ev[4].tick == 1960);
    /* swing on 1/8 grid: second 8th moves later */
    quant_t sw = { SONG_PPQ / 2, 100, 50, false };
    note(0, 4080, 100, 72, 60);   /* 1/8 after 3840 is 4080, odd step */
    edit_quantise(0, 3840, 5760, &sw);
    CHECK(g_song.tr[0].ev[5].tick == 4080 + 60);
    /* copy bar 1 and paste at bar 3 */
    edit_copy(0, 0, 1920);
    CHECK(g_clip.n == 4 && g_clip.len_ticks == 1920);
    edit_paste(0, 3 * 1920);
    CHECK(edit_count(0, 3 * 1920, 4 * 1920) == 4);
    /* duplicate the drum bar */
    edit_duplicate(drums, 0, 1920);
    CHECK(g_song.tr[drums].n == 4 && g_song.tr[drums].ev[2].tick == 1920);
    CHECK(g_clip.n == 4);   /* duplicate keeps the clipboard */
    /* transpose and velocity */
    edit_transpose(0, 0, 1920, 12); CHECK(g_song.tr[0].ev[0].a == 72);
    edit_velocity(0, 0, 1920, 50); CHECK(g_song.tr[0].ev[0].b == 50);
    /* cut */
    edit_cut(0, 3 * 1920, 4 * 1920); CHECK(edit_count(0, 3 * 1920, 4 * 1920) == 0 && g_clip.n == 4);
    /* save / load round trip */
    song_fit_length();
    int n0 = g_song.tr[0].n, n2 = g_song.tr[drums].n;
    CHECK(song_save("t_song.mbp"));
    song_new("other");
    CHECK(song_load("t_song.mbp"));
    CHECK(!strcmp(g_song.name, "Test Song") && g_song.ntracks == 3 && g_song.tr[0].n == n0 && g_song.tr[drums].n == n2);
    CHECK(g_song.tr[1].program == 33 && g_song.tr[2].channel == 9);
    /* corrupt file is refused */
    FILE *f = fopen("t_bad.mbp", "wb"); fwrite("MBP1\0\1garbage", 1, 13, f); fclose(f);
    CHECK(!song_load("t_bad.mbp"));
    CHECK(song_load("t_song.mbp"));
    /* export .mid and read it back with the player's loader */
    g_song.bpm = 90;
    CHECK(song_export_mid("t_song.mid"));
    smf_t s; CHECK(smf_load(&s, "t_song.mid") == SMF_OK);
    CHECK(s.format == 1 && s.ntracks == 4 && s.ppq == SONG_PPQ && s.nparts == 3);
    CHECK(!strcmp(s.title, "Test Song"));
    CHECK(s.ntempos == 1 && s.tempos[0].us_per_qn == 666666);
    printf("export: parts:"); for (int i = 0; i < s.nparts; i++) printf(" [%s ch%d prog%d notes%u]", s.parts[i].name, s.parts[i].channel + 1, s.parts[i].program, s.parts[i].notes); printf("\n");
    CHECK(s.parts[0].notes == (uint32_t)n0 && s.parts[1].program == 33 && s.parts[2].channel == 9);
    printf("export length %llu ms (bars=%u at 90 bpm)\n", (unsigned long long)(s.total_us / 1000), g_song.bars);
    smf_free(&s);
    printf("%d passed, %d failed\n", passes, fails);
    return fails != 0;
}
