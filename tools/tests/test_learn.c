/* Learn engine with a fake clock and a simulated player: timing grades, misses, wrong keys,
 * wait mode, count-in, loop, speed, keyboard range, the box's own parts, and best scores. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "smf.h"
#include "settings.h"
#include "ln_builtin.h"
#include "ln_song.h"
#include "ln_engine.h"
#include "ln_store.h"
#include "midi_in.h"

settings_t g_settings;
static int64_t g_now;
int64_t esp_timer_get_time(void) { return g_now; }
uint32_t xTaskGetTickCount(void) { return (uint32_t)(g_now / 1000); }
void vTaskDelay(uint32_t t) { (void)t; }
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t f, const char *n, uint32_t s, void *a, UBaseType_t p, TaskHandle_t *h, BaseType_t c) { (void)f; (void)n; (void)s; (void)a; (void)p; (void)h; (void)c; return 1; }
/* midi out: count what the box plays */
static int g_box_on[16], g_clicks, g_user_sound; static bool g_in_input;
void midi_out_note(uint8_t ch, uint8_t note, uint8_t vel, bool on) { (void)note; if (on && vel) { if (g_in_input) g_user_sound++; else if (ch == 9) g_clicks++; else g_box_on[ch]++; } }
void midi_out_program(uint8_t ch, uint8_t p) { (void)ch; (void)p; }
void midi_out_all_notes_off(void) {}
void midi_in_set_listener(midi_in_listener_t fn) { (void)fn; }
void midi_in_set_thru(bool on) { (void)on; }
/* smf / gm / sd stand-ins (MIDI files are tested on the device) */
smf_err_t smf_load(smf_t *s, const char *p) { (void)s; (void)p; return SMF_ERR_READ; }
void smf_free(smf_t *s) { (void)s; }
uint64_t smf_tick_to_us(const smf_t *s, uint32_t t) { (void)s; return t; }
int smf_part_index(const smf_t *s, uint16_t t, uint8_t c) { (void)s; (void)t; (void)c; return -1; }
bool smf_read_vlq(const uint8_t **p, const uint8_t *e, uint32_t *o) { (void)p; (void)e; (void)o; return false; }
uint8_t smf_data_len(uint8_t s) { (void)s; return 2; }
const char *gm_program_name(uint8_t p) { (void)p; return "Piano"; }
const char *path_name(const char *p) { return p; }
/* a tiny in-memory file system for the score file */
static char g_file[2][4096]; static size_t g_flen[2]; static bool g_fexists[2];
static int fidx(const char *p) { return strstr(p, "scores") ? 0 : 1; }
uint8_t *fs_read_all(const char *p, size_t *len) { int i = fidx(p); if (!g_fexists[i]) return NULL; uint8_t *b = malloc(g_flen[i] + 1); memcpy(b, g_file[i], g_flen[i]); *len = g_flen[i]; return b; }
size_t fs_read_head(const char *p, void *buf, size_t n) { int i = fidx(p); if (!g_fexists[i]) return 0; size_t l = g_flen[i] < n ? g_flen[i] : n; memcpy(buf, g_file[i], l); return l; }
bool fs_write_all(const char *p, const void *d, size_t l) { int i = fidx(p); memcpy(g_file[i], d, l); g_flen[i] = l; g_fexists[i] = true; return true; }
bool fs_mkdir(const char *p) { (void)p; return true; }

static int fails;
static void check(const char *what, bool ok) { printf("  %-66s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

/* simulated player: plays each of its notes at note time + offset (ms, real time) */
typedef struct { int64_t at; uint8_t key; bool on; } ev_t;
static ev_t g_ev[4096]; static int g_nev;
static void add_ev(int64_t at, uint8_t key, bool on) { g_ev[g_nev++] = (ev_t){ at, key, on }; }
static int cmp(const void *a, const void *b) { const ev_t *x = a, *y = b; return x->at < y->at ? -1 : x->at > y->at; }

/* run until done or limit; inputs are fired at their time */
static void run(int64_t limit_us)
{
    qsort(g_ev, (size_t)g_nev, sizeof(ev_t), cmp);
    int k = 0;
    ln_status_t st;
    for (int64_t t = 0; t < limit_us; t += 1000) {
        g_now += 1000;
        while (k < g_nev && g_ev[k].at <= g_now) {
            g_in_input = true; ln_input(g_ev[k].key, 90, g_ev[k].on, g_now); g_in_input = false;
            k++;
        }
        ln_step(g_now);
        ln_get(&st);
        if (st.state == LE_DONE) {
            break;
        }
    }
    g_nev = 0;
}

/* schedule a player hitting every note of yours, real time = start + note time * 100 / speed */
static void play_all(int64_t start_us, int speed, int offset_ms, int skip_every, int shift)
{
    for (int i = 0, k = 0; i < g_ln.n; i++) {
        if (!g_ln_user[i]) continue;
        k++;
        if (skip_every && k % skip_every == 0) continue;
        int64_t at = start_us + (int64_t)g_ln.notes[i].t * 1000 * 100 / speed + offset_ms * 1000;
        add_ev(at, (uint8_t)(g_ln.notes[i].note + shift), true);
        add_ev(at + 50000, (uint8_t)(g_ln.notes[i].note + shift), false);
    }
}

static int find(const char *id) { for (int i = 0; i < ln_builtin_count; i++) if (!strcmp(ln_builtin[i].id, id)) return i; return -1; }

int main(void)
{
    ln_engine_init();
    ln_status_t st;

    check("all 15 built-in songs load", ({ bool ok = true; for (int i = 0; i < ln_builtin_count; i++) ok &= ln_song_builtin(i) && g_ln.n > 0; ok && ln_builtin_count == 15; }));
    check("imported songs: full scores keep every note, credits present", ({
        int c = find("clair"), m = find("moon");
        c >= 0 && m >= 0 && ln_builtin[c].n == 1468 && ln_builtin[m].n == 1142 && ln_builtin[c].nbars == 72 &&
        ln_builtin[m].nbars == 69 && ln_builtin[c].credit && ln_builtin[m].credit && !ln_builtin[find("twinkle")].credit; }));
    check("easy versions: right hand is one note at a time", ({
        bool ok = true;
        for (int k = 0; k < 2; k++) {
            const ln_builtin_t *b = &ln_builtin[find(k ? "moon_easy" : "clair_easy")];
            uint32_t last = UINT32_MAX;
            for (int i = 0; i < b->n; i++)
                if (b->notes[i].hand == 0) { ok &= b->notes[i].t != last; last = b->notes[i].t; }
        }
        ok; }));
    ln_song_builtin(find("twinkle"));
    check("Twinkle: 12 bars, 1.8 s per bar at 100 bpm, ends at 28.8 s",
          g_ln.nbars == 13 && g_ln.bars[1] == 2400 && g_ln.len_ms == 28800);

    /* 1. play mode, right hand, perfect player */
    ln_cfg_t c = { .mode = LN_MODE_PLAY, .speed = 100, .hands = 1 };
    memset(g_box_on, 0, sizeof g_box_on);
    ln_start(&c);
    play_all(g_now, 100, 10, 0, 0);
    run(60000000);
    ln_get(&st);
    int rh = 0, lh = 0;
    for (int i = 0; i < g_ln.n; i++) { if (g_ln.notes[i].hand == LN_RH) rh++; else lh++; }
    check("Play mode, every note 10 ms late: all Perfect", st.perfect == rh && st.good == 0 && st.miss == 0);
    check("  accuracy 100 %, 3 stars, combo = every note", ln_accuracy(&st) == 100 && ln_stars(&st) == 3 && st.best_combo == rh);
    check("  the box played the whole left hand", g_box_on[1] == lh && g_box_on[0] == 0);
    check("  song finishes by itself", st.state == LE_DONE);
    printf("     score %lu for %d notes\n", (unsigned long)st.score, rh);

    /* 2. 100 ms late = Good, and every 4th note skipped = Miss */
    ln_start(&c);
    play_all(g_now, 100, 100, 4, 0);
    run(60000000);
    ln_get(&st);
    check("100 ms late: Good; every 4th note skipped: Miss", st.perfect == 0 && st.good == rh - rh / 4 && st.miss == rh / 4);
    check("  combo never passes 3 (reset by each miss)", st.best_combo == 3);
    check("  timing chart centred on +100 ms", st.dt_n == st.good && st.dt_sum / st.dt_n == 100);

    /* 3. wrong keys */
    ln_start(&c);
    play_all(g_now, 100, 0, 0, 0);
    add_ev(g_now + 300000, 30, true);   /* a key that's in no note */
    add_ev(g_now + 900000, 31, true);
    run(60000000);
    ln_get(&st);
    check("Two stray keys: 2 Wrong, everything else still Perfect", st.wrong == 2 && st.perfect == rh);

    /* 4. half speed */
    c.speed = 50;
    ln_start(&c);
    int64_t t0 = g_now;
    play_all(t0, 50, 20, 0, 0);
    run(120000000);
    ln_get(&st);
    check("50 % speed: all Perfect, song takes twice as long", st.perfect == rh && g_now - t0 > 57000000);
    c.speed = 100;

    /* 5. wait mode: nobody plays -> the song waits at the first note */
    c.mode = LN_MODE_WAIT;
    memset(g_box_on, 0, sizeof g_box_on);
    ln_start(&c);
    run(5000000);
    ln_get(&st);
    check("Wait mode, nothing played: stops at the first note (C4)", st.waiting && st.nwait == 1 && st.wait_keys[0] == 60 && st.song_us == 0);
    /* a player who waits for the song to stop, then plays the keys shown 0.7 s later;
       one wrong key before the very first note */
    {
        int64_t since = -1;
        bool first = true;
        for (int64_t t = 0; t < 200000000; t += 1000) {
            g_now += 1000;
            ln_step(g_now);
            ln_get(&st);
            if (st.state == LE_DONE) break;
            if (st.waiting && st.nwait) {
                if (since < 0) since = g_now;
                if (first && g_now - since == 500000) { g_in_input = true; ln_input(61, 90, true, g_now); ln_input(61, 0, false, g_now); g_in_input = false; first = false; }
                if (g_now - since >= 700000) {
                    uint8_t keys[8]; int n = st.nwait; memcpy(keys, st.wait_keys, (size_t)n);
                    g_in_input = true;
                    for (int k = 0; k < n; k++) ln_input(keys[k], 90, true, g_now);
                    for (int k = 0; k < n; k++) ln_input(keys[k], 0, false, g_now + 1);
                    g_in_input = false;
                    since = -1;
                }
            } else {
                since = -1;
            }
        }
    }
    printf("     wait: state %d perfect %u good %u miss %u wrong %u waiting %d nwait %d song %lld box_lh %d/%d\n", st.state, st.perfect, st.good, st.miss, st.wrong, st.waiting, st.nwait, (long long)st.song_us, g_box_on[1], lh);
    check("Wait mode: every note played late still counts; finishes", st.state == LE_DONE && st.perfect + st.good == rh && st.miss == 0);
    check("  the note after a wrong key is Good, the rest Perfect", st.good == 1 && st.wrong == 1);
    check("  the box still played the whole left hand", g_box_on[1] == lh);

    /* 6. both hands */
    c.mode = LN_MODE_PLAY;
    c.hands = 3;
    memset(g_box_on, 0, sizeof g_box_on);
    ln_start(&c);
    play_all(g_now, 100, -30, 0, 0);
    run(60000000);
    ln_get(&st);
    check("Both hands, 30 ms early: all Perfect, box plays nothing", st.perfect == rh + lh && g_box_on[0] + g_box_on[1] == 0);

    /* 7. count-in */
    c.hands = 1;
    c.countin_bars = 1;
    g_clicks = 0;
    ln_start(&c);
    ln_get(&st);
    check("Count-in of 1 bar starts 2.4 s before the song", st.state == LE_COUNTIN && st.song_us == -2400000);
    play_all(g_now + 2400000, 100, 0, 0, 0);
    run(60000000);
    ln_get(&st);
    check("  4 clicks, then the song is scored normally", g_clicks == 4 && st.perfect == rh);
    c.countin_bars = 0;

    /* 8. keyboard range: a 13-key keyboard C4..C5 (Twinkle's right hand is C4..A4) */
    c.kb_lo = 60; c.kb_hi = 72;
    ln_start(&c);
    check("Right hand fits C4..C5: all yours", st.user_total == rh || ({ ln_get(&st); st.user_total == rh; }));
    c.kb_lo = 64; c.kb_hi = 76;
    ln_start(&c);
    ln_get(&st);
    int below = 0;
    for (int i = 0; i < g_ln.n; i++) if (g_ln.notes[i].hand == LN_RH && g_ln.notes[i].note < 64) below++;
    check("E4..E5 keyboard: notes below E4 go to the box", st.user_total == rh - below && below > 0);
    c.shift = 12; c.kb_lo = 72; c.kb_hi = 96;
    ln_start(&c);
    ln_get(&st);
    play_all(g_now, 100, 0, 0, 12);
    run(60000000);
    ln_get(&st);
    check("Keyboard octave up (+12): you play C5 for C4, all Perfect", st.user_total == rh && st.perfect == rh);
    c.shift = 0; c.kb_lo = 0; c.kb_hi = 0;

    /* 9. loop the first 4 bars */
    ln_start(&c);
    ln_loop_toggle();
    run(20000000);
    ln_get(&st);
    check("Loop bars 1-4 (7.2 s): still looping after 20 s", st.loop && st.state == LE_RUN && st.song_us < 7200000);
    ln_stop();

    /* 10. every song with a perfect player, both hands */
    bool all = true;
    for (int s = 0; s < ln_builtin_count; s++) {
        ln_song_builtin(s);
        ln_cfg_t cc = { .mode = LN_MODE_PLAY, .speed = 100, .hands = 3 };
        ln_start(&cc);
        play_all(g_now, 100, 5, 0, 0);
        run(400000000);
        ln_get(&st);
        bool ok = st.state == LE_DONE && st.perfect == st.user_total && st.miss == 0 && st.wrong == 0;
        if (!ok) printf("     %s: %u/%u perfect, %u miss, %u wrong, state %d\n", ln_builtin[s].id, st.perfect, st.user_total, st.miss, st.wrong, st.state);
        all &= ok;
    }
    check("Every built-in song, both hands, perfect player: 100 %", all);

    /* 11. best scores */
    check("No best score yet", ({ uint32_t sc; int sstars; !ln_best_get("b:elise|R|play", &sc, &sstars); }));
    check("First score is a new best", ln_best_put("b:elise|R|play", 4000, 2));
    check("Lower score is not", !ln_best_put("b:elise|R|play", 3000, 3));
    check("Higher score is", ln_best_put("b:elise|R|play", 5200, 3));
    ln_best_put("b:elise|B|wait", 900, 1);
    ln_best_put("b:ode|R|play", 1500, 2);
    check("Stars: best per song (3 for Elise), total over songs 3 + 2", ln_best_stars("b:elise") == 3 && ln_total_stars() == 5);
    check("Score file keeps one line per key", ({ uint32_t sc; int sstars; ln_best_get("b:elise|R|play", &sc, &sstars) && sc == 5200 && sstars == 3; }));
    ln_kb_save(36, 96);
    uint8_t lo, hi;
    check("Keyboard range saved and read back (C2..C7)", ln_kb_load(&lo, &hi) && lo == 36 && hi == 96);

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
