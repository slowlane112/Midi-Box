/* Learn stage 2 on the PC: exercises (notes, spelling, fingering, played through the engine with
 * the metronome) and Note Reading (levels, keyboard range, scoring, lives, time). */
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

#include "ln_ex.h"
#include "ln_nr.h"
#include "ln_ear.h"
#include "ln_ui.h"

static const int8_t BLACK[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };

/* the notes of one hand, in time order (chords bottom to top) */
static int hand_notes(int hand, int *idx)
{
    int n = 0;
    for (int i = 0; i < g_ln.n; i++) if (g_ln.notes[i].hand == hand) idx[n++] = i;
    for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) {
        const ln_note_t *x = &g_ln.notes[idx[a]], *y = &g_ln.notes[idx[b]];
        if (y->t < x->t || (y->t == x->t && y->note < x->note)) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
    }
    return n;
}

int main(void)
{
    char nm[16];
    printf("Exercises\n");
    check("72 exercises in 4 groups", ln_ex_count(LX_MAJOR) + ln_ex_count(LX_MINOR) + ln_ex_count(LX_CHORDS) + ln_ex_count(LX_ARPS) == 72);
    ln_ex_name(LX_MAJOR, 6, nm, sizeof nm);
    check("names: F# major, Eb minor, G# minor chord", ({ char a[32], b[32]; ln_ex_name(LX_MINOR, 3, a, 32); ln_ex_name(LX_CHORDS, 20, b, 32);
        !strcmp(nm, "F# major") && !strcmp(a, "Eb minor") && !strcmp(b, "G# minor chord"); }));

    /* every scale, both hands, 2 octaves */
    bool shape = true, spell = true, fing = true, thumb = true, sym = true, both = true;
    for (int m = 0; m < 2; m++) for (int k = 0; k < 12; k++) {
        ln_ex_t x = { .cat = m ? LX_MINOR : LX_MAJOR, .idx = k, .hand = LH_BOTH, .bpm = 80, .octaves = 2 };
        if (!ln_ex_build(&x)) { shape = false; continue; }
        static const int maj[7] = { 2, 2, 1, 2, 2, 2, 1 }, hmin[7] = { 2, 1, 2, 2, 1, 3, 1 };
        int idx[2][64], n[2];
        for (int h = 0; h < 2; h++) {
            n[h] = hand_notes(h, idx[h]);
            shape &= n[h] == 29;
            for (int i = 0; i < 28 && n[h] == 29; i++) {
                const ln_note_t *a = &g_ln.notes[idx[h][i]], *b = &g_ln.notes[idx[h][i + 1]];
                int d = i < 14 ? i : 27 - i;             /* degree step going up */
                int want = (m ? hmin : maj)[d % 7];
                int got = i < 14 ? b->note - a->note : a->note - b->note;
                shape &= got == want;
                int pa = ln_ui_staff_pos(a->note, a->acc), pb = ln_ui_staff_pos(b->note, b->acc);
                spell &= (i < 14 ? pb - pa : pa - pb) == 1;      /* one letter a step */
                spell &= a->acc >= -2 && a->acc <= 2;
            }
            for (int i = 0; i < n[h]; i++) {
                const ln_note_t *a = &g_ln.notes[idx[h][i]];
                int f = ln_ex_finger(&x, idx[h][i]);
                fing &= f >= 1 && f <= 5;
                if (f == 1 && BLACK[a->note % 12]) { thumb = false; ln_ex_name(x.cat, k, nm, 16); printf("     thumb on a black key: %s hand %d note %d\n", nm, h, i); }
                if (i + 1 < n[h] && f == ln_ex_finger(&x, idx[h][i + 1])) fing = false;
                /* the same finger for a note going up and coming down */
                if (i > 0 && i < 14) sym &= f == ln_ex_finger(&x, idx[h][28 - i]);
            }
            if (h == 1 && n[0] == 29 && n[1] == 29)
                { int gap = g_ln.notes[idx[0][0]].note - g_ln.notes[idx[1][0]].note; both &= gap == 12 || gap == 24;
                for (int i = 0; i < 29; i++) both &= g_ln.notes[idx[0][i]].note - g_ln.notes[idx[1][i]].note == gap && g_ln.notes[idx[0][i]].t == g_ln.notes[idx[1][i]].t; }
        }
    }
    check("24 scales x 2 hands: 29 notes, right intervals (harmonic minor)", shape);
    check("spelled one letter per step (E# in F#, F double-sharp in G# minor)", spell);
    check("fingers 1-5, never the same finger twice in a row", fing);
    check("thumb never on a black key", thumb);
    check("same finger for a note going up and coming down", sym);
    check("both hands: same notes one or two octaves apart, together", both);

    ln_ex_t g = { .cat = LX_MINOR, .idx = 8, .hand = LH_RIGHT, .bpm = 80, .octaves = 1 };
    ln_ex_build(&g);
    check("G# minor, 1 octave: F double-sharp written as Fx5", ({ bool f = false; for (int i = 0; i < g_ln.n; i++) {
        ln_ui_note_name_acc(nm, sizeof nm, g_ln.notes[i].note, g_ln.notes[i].acc); f |= !strcmp(nm, "Fx5"); } f && g_ln.n == 15; }));
    ln_ex_t fs = { .cat = LX_MAJOR, .idx = 6, .hand = LH_RIGHT, .bpm = 80, .octaves = 1 };
    ln_ex_build(&fs);
    check("F# major: E#5 (not F5), fingers 2341231 2", ({ char f[16] = ""; bool e = false;
        for (int i = 0; i < 8; i++) { f[i] = (char)('0' + ln_ex_finger(&fs, i)); ln_ui_note_name_acc(nm, sizeof nm, g_ln.notes[i].note, g_ln.notes[i].acc); e |= !strcmp(nm, "E#5"); }
        e && !strcmp(f, "23412312"); }));
    ln_ex_t bb = { .cat = LX_MAJOR, .idx = 10, .hand = LH_RIGHT, .bpm = 80, .octaves = 1 };
    ln_ex_build(&bb);
    check("Bb major spelled with flats: Bb4 C5 D5 Eb5", ({ char s[64] = ""; for (int i = 0; i < 4; i++) { ln_ui_note_name_acc(nm, sizeof nm, g_ln.notes[i].note, g_ln.notes[i].acc); strcat(s, nm); strcat(s, " "); } !strcmp(s, "Bb4 C5 D5 Eb5 "); }));

    /* chords: root, 1st, 2nd inversion, root up, and back */
    bool ch = true;
    for (int k = 0; k < 24; k++) {
        ln_ex_t c = { .cat = LX_CHORDS, .idx = k, .hand = LH_RIGHT, .bpm = 80, .octaves = 2 };
        ln_ex_build(&c);
        ch &= g_ln.n == 21;
        int third = k < 12 ? 4 : 3;
        for (int j = 0; j < 7 && g_ln.n == 21; j++) {
            int lo = 999, hi = 0, mid = 0;
            int ns[3], q = 0;
            for (int i = 0; i < g_ln.n; i++) if (g_ln.notes[i].t == (uint32_t)(j * 2 * 750)) ns[q++ < 3 ? q - 1 : 0] = g_ln.notes[i].note;
            ch &= q == 3;
            for (int i = 0; i < 3; i++) { lo = ns[i] < lo ? ns[i] : lo; hi = ns[i] > hi ? ns[i] : hi; }
            for (int i = 0; i < 3; i++) if (ns[i] != lo && ns[i] != hi) mid = ns[i];
            static const int inv[7] = { 0, 1, 2, 0, 2, 1, 0 };
            int a = mid - lo, b = hi - mid;
            int wa = inv[j] == 0 ? third : (inv[j] == 1 ? 7 - third : 5), wb = inv[j] == 0 ? 7 - third : (inv[j] == 1 ? 5 : third);
            ch &= a == wa && b == wb;
            ch &= ((lo - (60 + k % 12)) % 12 + 12) % 12 == (inv[j] == 0 ? 0 : (inv[j] == 1 ? third : 7));
        }
    }
    check("24 chords: root position, inversions, up and back", ch);
    ln_ex_t ar = { .cat = LX_ARPS, .idx = 0, .hand = LH_RIGHT, .bpm = 80, .octaves = 2 };
    ln_ex_build(&ar);
    check("C major arpeggio: C E G C E G C, fingers 1231235 then back", ({ char f[16] = ""; for (int i = 0; i < 7; i++) f[i] = (char)('0' + ln_ex_finger(&ar, i));
        g_ln.n == 13 && g_ln.notes[3].note == 72 && g_ln.notes[6].note == 84 && !strcmp(f, "1231235"); }));

    /* through the engine, with the metronome */
    ln_engine_init();
    g_now = 1000000;
    ln_ex_t d = { .cat = LX_MAJOR, .idx = 2, .hand = LH_BOTH, .bpm = 120, .octaves = 2 };
    ln_ex_build(&d);
    g_clicks = 0;
    ln_cfg_t cfg = { .mode = LN_MODE_PLAY, .speed = 100, .hands = 3, .metronome = true };
    ln_start(&cfg);
    play_all(g_now, 100, 10, 0, 0);
    run(60000000);
    ln_status_t st;
    ln_get(&st);
    int beats = (int)((g_ln.len_ms + 499) / 500);
    check("D major both hands at 120 bpm, 10 ms late: all Perfect", st.state == LE_DONE && st.perfect == st.user_total && st.user_total == 58);
    check("metronome: one click a beat while it plays", g_clicks >= beats - 1 && g_clicks <= beats + 1);
    check("evenness 100 % when every note is equally late", ln_evenness(&st) == 100);
    ln_stop();
    ln_ex_build(&d);
    ln_start(&cfg);
    for (int i = 0, k = 0; i < g_ln.n; i++) if (g_ln_user[i]) { int off = (k++ % 2) ? 45 : -45; int64_t at = g_now + (int64_t)g_ln.notes[i].t * 1000 + off * 1000; add_ev(at, g_ln.notes[i].note, true); add_ev(at + 40000, g_ln.notes[i].note, false); }
    run(60000000);
    ln_get(&st);
    check("uneven player (+-45 ms): still Perfect, evenness about 55 %", st.perfect == st.user_total && ln_evenness(&st) >= 50 && ln_evenness(&st) <= 60);
    ln_stop();
    cfg.metronome = false;
    g_clicks = 0;
    ln_ex_build(&d);
    ln_start(&cfg);
    run(30000000);
    check("metronome off: no clicks", g_clicks == 0);
    ln_stop();

    printf("Note Reading\n");
    uint32_t t = 1000;
    ln_nr_start(NR_TREBLE, 0, 0, 42, t);
    bool in = true;
    for (int i = 0; i < 300; i++) { in &= g_nr.cur.note >= 64 && g_nr.cur.note <= 77 && !BLACK[g_nr.cur.note % 12] && !g_nr.cur.bass; t += 100; ln_nr_answer(g_nr.cur.note, t); if (g_nr.right >= 9) break; }
    check("level 1 treble: notes on the staff, E4..F5, no sharps", in && g_nr.level == 1);
    ln_nr_start(NR_BASS, 0, 0, 7, t);
    in = true;
    for (int i = 0; i < 9; i++) { in &= g_nr.cur.note >= 43 && g_nr.cur.note <= 57 && g_nr.cur.bass; t += 100; ln_nr_answer(g_nr.cur.note, t); }
    check("level 1 bass: G2..A3", in);
    ln_nr_start(NR_BOTH, 0, 0, 9, t);
    int seen_acc = 0, seen_bass = 0, seen_tr = 0, ledger = 0, bad = 0, repeat = 0, prev = -1;
    for (int i = 0; i < 400 && g_nr.state == NR_RUN; i++) {
        const nr_item_t *c = &g_nr.cur;
        seen_bass += c->bass; seen_tr += !c->bass;
        if (g_nr.level == 3 && c->acc) seen_acc++;
        if (g_nr.level < 3 && c->acc) bad++;
        int pos = ln_ui_staff_pos(c->note, c->acc);
        if (g_nr.level >= 2 && (pos > 39 || pos < 17 || pos == 28)) ledger++;
        ln_ui_note_name_acc(nm, sizeof nm, c->note, c->acc);
        if (nm[1] == '#' && (nm[0] == 'E' || nm[0] == 'B')) bad++;
        if (nm[1] == 'b' && (nm[0] == 'C' || nm[0] == 'F')) bad++;
        repeat += c->note == prev; prev = c->note;
        t += 50; ln_nr_answer(c->note, t);
        if (g_nr.left_ms < 5000) { g_nr.left_ms = 60000; }   /* keep it going */
    }
    check("both clefs; levels 2 and 3 add ledger lines, then sharps and flats", seen_bass > 50 && seen_tr > 50 && ledger > 10 && seen_acc > 20 && !bad);
    check("never the same note twice in a row", repeat == 0);
    ln_nr_start(NR_TREBLE, 60, 72, 3, t);
    in = true;
    for (int i = 0; i < 200; i++) { in &= g_nr.cur.note >= 60 && g_nr.cur.note <= 72; t += 50; ln_nr_answer(g_nr.cur.note, t); g_nr.left_ms = 60000; }
    check("13-key keyboard (C4..C5): only notes it has", in);

    ln_nr_start(NR_TREBLE, 0, 0, 5, 0);
    ln_nr_answer(g_nr.cur.note, 400);
    uint32_t fast = g_nr.score;
    ln_nr_answer(g_nr.cur.note, 400 + 3500);
    uint32_t slow = g_nr.score - fast;
    check("fast answer 200 points, slow one 110 (100 x combo 1.1)", fast == 200 && slow == 110);
    uint8_t wrongk = g_nr.cur.note + 1;
    ln_nr_answer(wrongk, 5000);
    check("wrong key: a life gone, streak reset, next note", g_nr.lives == 2 && g_nr.combo == 0 && g_nr.wrong == 1 && !g_nr.hist[0].ok && g_nr.wrong_key == wrongk);
    ln_nr_answer(0, 5100); ln_nr_answer(0, 5200);
    check("three wrong: out of lives, round over", g_nr.state == NR_OVER && g_nr.out_of_lives && ln_nr_answer(60, 5300) == -1);
    ln_nr_start(NR_TREBLE, 0, 0, 5, 0);
    ln_nr_tick(30000);
    ln_nr_pause(30000);
    ln_nr_tick(50000);
    uint32_t left = g_nr.left_ms;
    ln_nr_pause(50000);
    ln_nr_tick(80000);
    check("one minute; paused time doesn't count", left == 30000 && g_nr.state == NR_OVER);
    ln_nr_t s = { 0 };
    s.right = 30; s.wrong = 2;
    int s3 = ln_nr_stars(&s);
    s.right = 20; s.wrong = 12;
    int s1 = ln_nr_stars(&s);
    s.right = 9; s.wrong = 0;
    check("stars: 30 right = 3; under 70 % right = 1 at most; under 10 = 0", s3 == 3 && s1 == 1 && ln_nr_stars(&s) == 0);


    printf("Ear Training\n");
    static int on_n, off_n; static uint8_t last_on[8]; 
    void snd(uint8_t n, bool on) { if (on) { last_on[on_n % 8] = n; on_n++; } else off_n++; }
    uint32_t now = 1000;
    bool ok = true;
    for (int lv = 0; lv < EAR_NLEVELS; lv++) {
        ln_ear_start(lv, lv, 36, 96, 11 + lv, snd, now);
        for (int q = 0; q < 200; q++) {
            int d = g_ear.b - g_ear.a;
            switch (lv) {
            case EAR_SAME: ok &= (g_ear.answer == 0) == (d == 0) && d >= -4 && d <= 4; break;
            case EAR_UPDOWN: ok &= d != 0 && (g_ear.answer == 0) == (d > 0) && d >= -12 && d <= 12; break;
            case EAR_SMALL: ok &= d >= 1 && d <= 4; break;
            case EAR_FIFTHS: ok &= d >= 1 && d <= 7; break;
            case EAR_ALL: ok &= d != 0 && d >= -12 && d <= 12; break;
            default: ok &= g_ear.chord[1] - g_ear.chord[0] == (g_ear.answer ? 3 : 4) && g_ear.chord[2] - g_ear.chord[0] == 7;
            }
            ok &= lv == EAR_CHORD || (g_ear.a >= 36 && g_ear.b <= 96 && g_ear.b >= 36);
            g_ear.q = 0; ln_ear_skip(now); now += 2000; ln_ear_tick(now);
        }
    }
    check("questions: right intervals for each level, chords major / minor", ok);
    on_n = off_n = 0;
    now = 50000;
    ln_ear_start(EAR_SMALL, EAR_SMALL, 0, 0, 5, snd, now);
    ln_ear_tick(now); int on1 = on_n; uint8_t first = last_on[0];
    for (int t = 0; t < 2000 && g_ear.state == EQ_LISTEN; t += 10) { now += 10; ln_ear_tick(now); }
    check("listening: the box plays both notes, then it's your turn", on1 == 1 && on_n == 2 && off_n == 2 && first == g_ear.a && last_on[1] == g_ear.b && g_ear.state == EQ_ASK);
    check("playing the given note again doesn't count", ln_ear_key(g_ear.a, now) == -1 && g_ear.tries == 3);
    uint8_t ans = g_ear.b;
    ln_ear_key((uint8_t)(ans + 1), now);
    ln_ear_key(ans, now);
    check("right on the second try: 50 points, no streak", g_ear.score == 50 && g_ear.later == 1 && g_ear.combo == 0 && g_ear.state == EQ_SHOW);
    now += 1300; ln_ear_tick(now);
    for (int t = 0; t < 2000 && g_ear.state == EQ_LISTEN; t += 10) { now += 10; ln_ear_tick(now); }
    on_n = 0;
    ans = g_ear.b;
    ln_ear_key((uint8_t)(ans + 1), now); ln_ear_key((uint8_t)(ans + 2), now); ln_ear_key((uint8_t)(ans + 3), now);
    check("three wrong: missed, and the box plays the answer", g_ear.missed == 1 && g_ear.state == EQ_SHOW && on_n == 1 && last_on[0] == ans);
    /* a perfect round, with a hint on one */
    now += 5000;
    ln_ear_start(EAR_SAME, EAR_SAME, 0, 0, 77, snd, now);
    bool up_seen = false;
    for (int q = 0; q < EAR_QUESTIONS; q++) {
        for (int t = 0; t < 3000 && g_ear.state != EQ_ASK; t += 10) { now += 10; ln_ear_tick(now); }
        ln_ear_choose(g_ear.answer, now);
        up_seen |= g_ear.levelled_up;
        now += 1300; ln_ear_tick(now);
    }
    check("20 right first time: 3 stars, streak 20, next level unlocked after 8", g_ear.state == EQ_DONE && ln_ear_stars(&g_ear) == 3 && g_ear.best_combo == 20 && g_ear.max_level == 1 && up_seen);
    ln_ear_start(EAR_FIFTHS, EAR_FIFTHS, 0, 0, 3, snd, now);
    for (int t = 0; t < 3000 && g_ear.state != EQ_ASK; t += 10) { now += 10; ln_ear_tick(now); }
    ln_ear_hint();
    ln_ear_key(g_ear.b, now);
    check("with a hint: half points, no streak", g_ear.score == 50 && g_ear.combo == 0);
    check("choices: Same / Different, Up / Down, Major / Minor; interval names",
          !strcmp(ln_ear_choice(EAR_SAME, 1), "Different") && !strcmp(ln_ear_choice(EAR_CHORD, 0), "Major") && !strcmp(ln_ear_interval_name(-7), "perfect 5th"));

    printf("Practice time\n");
    uint32_t pt = 100000;
    for (int i = 0; i < 200; i++) { pt += 100; ln_prac_active(pt); }     /* 20 s */
    pt += 60000; ln_prac_active(pt);                                     /* a long gap: not counted */
    ln_prac_flush();
    ln_prac_t pr;
    ln_prac_get(&pr);
    check("20 s playing (a minute's gap not counted): time kept, not a session yet", pr.total_s == 19 && pr.sessions == 0 && pr.nrecent == 0);
    for (int i = 0; i < 300; i++) { pt += 100; ln_prac_active(pt); }     /* 30 s more */
    ln_prac_combo(42);
    ln_prac_flush();
    for (int i = 0; i < 600; i++) { pt += 100; ln_prac_active(pt); }     /* 60 s more */
    ln_prac_flush();
    ln_prac_get(&pr);
    check("one session of 109 s, written twice, counted once; best combo 42", pr.sessions == 1 && pr.nrecent == 1 && pr.recent[0] == 109 && pr.total_s == 109 && pr.best_combo == 42);
    ln_prac_end();
    for (int s = 0; s < 8; s++) {
        for (int i = 0; i < 400 + s * 10; i++) { pt += 100; ln_prac_active(pt); }
        pt += 5000;
        ln_prac_end();
    }
    ln_prac_get(&pr);
    check("9 sessions: the last 7 kept, oldest first", pr.sessions == 9 && pr.nrecent == 7 && pr.recent[6] == 46 && pr.recent[0] == 40);
    printf("Scores\n");
    ln_best_put("x:maj:2|B|play", 5000, 2);
    ln_best_put("x:maj:2|R|play", 7000, 3);
    ln_best_put("x:min:11|R|play", 100, 1);
    ln_best_put("nr:0|play", 3000, 2);
    ln_best_put("b:elise|R|play", 4000, 1);
    int8_t list[24];
    ln_best_stars_list("x:maj:", list, 24);
    int8_t lmin[24];
    ln_best_stars_list("x:min:", lmin, 24);
    check("per-exercise stars (best over hands), from one read of the file", list[2] == 3 && list[1] == 0 && lmin[11] == 1 && lmin[1] == 0);
    check("stars per section: songs 1, exercises 4, note reading 2, all 7",
          ln_total_stars_prefix("b:") == 1 && ln_total_stars_prefix("x:") == 4 && ln_total_stars_prefix("nr:") == 2 && ln_total_stars() == 7);

    check("songs learnt: songs with a star", ln_count_learnt("b:") == 1);
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
