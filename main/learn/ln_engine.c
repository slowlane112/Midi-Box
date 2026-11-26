/*
 * ln_engine.c - Learn playback and scoring. See ln_engine.h.
 *
 * Runs in its own task (1 ms steps, core 1). MIDI input arrives from the DIN / Bluetooth
 * tasks through midi_in's listener. A mutex guards the state; MIDI is sent while holding
 * it, which is fine because the synth UART only blocks when its 2 KB buffer is full.
 */
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "midi_out.h"
#include "midi_in.h"
#include "settings.h"
#include "ln_song.h"
#include "ln_engine.h"

#define WIN_PERFECT_MS 60
#define WIN_GOOD_MS    150
#define WIN_MISS_MS    200    /* after this a note is a Miss */
#define WAIT_EARLY_MS  400    /* wait mode: how early (song time) a key may come */
#define CHORD_MS       40     /* notes this close together are one chord in wait mode */
#define MAX_ACTIVE     96
#define CLICK_CH       9

uint8_t *g_ln_judged;
uint8_t *g_ln_user;
uint8_t g_ln_pressed[16];

static SemaphoreHandle_t s_mx;
static ln_cfg_t s_cfg;
static ln_status_t s_st;
static int64_t s_last_us;
static int s_box;            /* next note the box may play */
static int s_judge;          /* first of your notes not yet judged (play mode misses) */
static bool s_wait_wrong;    /* wait mode: a wrong key since stopping */
static uint32_t s_wait_t;    /* wait mode: time of the chord we stopped at (ms) */
static int64_t s_countin_from;
static int s_last_beat;
static uint8_t s_user_ch;
static bool s_listening;
static struct {
    int64_t end_us;
    uint8_t ch, note;
} s_act[MAX_ACTIVE];
static int s_nact;

static void lock(void) { xSemaphoreTake(s_mx, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mx); }
static uint32_t now_ms(int64_t us) { return (uint32_t)(us / 1000); }

static int64_t note_us(int i) { return (int64_t)g_ln.notes[i].t * 1000; }
static int key_of(int i) { return g_ln.notes[i].note + s_cfg.shift; }

static void all_off(void)
{
    for (int i = 0; i < s_nact; i++) {
        midi_out_note(s_act[i].ch, s_act[i].note, 0, false);
    }
    s_nact = 0;
}

/* ------------------------------------------------------------------ input */

static void listener(const uint8_t *m, uint16_t len, uint8_t source)
{
    (void)source;
    if (len < 3) {
        return;
    }
    uint8_t type = m[0] & 0xF0;
    if (type == 0x90 || type == 0x80) {
        ln_input(m[1] & 0x7F, m[2] & 0x7F, type == 0x90 && m[2], esp_timer_get_time());
    }
}

static void grade(ln_grade_t g, int note_index, uint8_t key, int64_t now_us, int dt_ms)
{
    s_st.last = g;
    s_st.last_ms = now_ms(now_us);
    s_st.last_key = key;
    if (g == LG_PERFECT || g == LG_GOOD) {
        int mult = 10 + (s_st.combo < 30 ? s_st.combo : 30);
        s_st.score += (uint32_t)((g == LG_PERFECT ? 100 : 50) * mult / 10);
        s_st.combo++;
        if (s_st.combo > s_st.best_combo) {
            s_st.best_combo = s_st.combo;
        }
        if (g == LG_PERFECT) {
            s_st.perfect++;
        } else {
            s_st.good++;
        }
        g_ln_judged[note_index] = g == LG_PERFECT ? LJ_PERFECT : LJ_GOOD;
        if (s_cfg.mode == LN_MODE_PLAY) {
            int b = (dt_ms + 210) / 30;
            b = b < 0 ? 0 : (b >= LN_HIST_BINS ? LN_HIST_BINS - 1 : b);
            s_st.hist[b]++;
            s_st.dt_sum += dt_ms;
            s_st.dt_n++;
            s_st.dt_sq += (uint32_t)(dt_ms * dt_ms);
            s_st.last_dt = (int16_t)dt_ms;
        }
    } else {
        s_st.combo = 0;
        if (g == LG_MISS) {
            s_st.miss++;
            g_ln_judged[note_index] = LJ_MISS;
        } else {
            s_st.wrong++;
        }
    }
}

void ln_input(uint8_t key, uint8_t vel, bool on, int64_t now_us)
{
    if (!s_mx) {
        return;
    }
    lock();
    bool live = s_st.state == LE_RUN || s_st.state == LE_COUNTIN || s_st.state == LE_PAUSED;
    int sound = key - s_cfg.shift;   /* you hear the note as written in the song */
    if (sound >= 0 && sound < 128) {
        midi_out_note(s_user_ch, (uint8_t)sound, vel, on);
    }
    if (on) {
        g_ln_pressed[key >> 3] |= (uint8_t)(1u << (key & 7));
    } else {
        g_ln_pressed[key >> 3] &= (uint8_t)~(1u << (key & 7));
    }
    if (!on || !live || s_st.state == LE_PAUSED) {
        unlock();
        return;
    }
    /* advance the clock to this moment so the timing is exact */
    int64_t song = s_st.song_us;
    if (!s_st.waiting && s_st.state == LE_RUN) {
        song += (now_us - s_last_us) * s_cfg.speed / 100;
    }
    /* the nearest unjudged note of yours on this key */
    int best = -1;
    int64_t best_d = 0;
    int64_t early = (int64_t)(s_cfg.mode == LN_MODE_WAIT ? WAIT_EARLY_MS : WIN_MISS_MS) * 1000 * s_cfg.speed / 100;
    int64_t late = (int64_t)WIN_MISS_MS * 1000 * s_cfg.speed / 100;
    for (int i = s_judge; i < g_ln.n; i++) {
        int64_t t = note_us(i);
        if (t > song + early) {
            break;
        }
        if (!g_ln_user[i] || g_ln_judged[i] || key_of(i) != key || t < song - late) {
            continue;
        }
        int64_t d = song - t;
        if (best < 0 || llabs(d) < llabs(best_d)) {
            best = i;
            best_d = d;
        }
    }
    if (best < 0) {
        if (s_st.state == LE_RUN) {
            grade(LG_WRONG, -1, key, now_us, 0);
            s_wait_wrong = true;
        }
        unlock();
        return;
    }
    int dt_ms = (int)(best_d * 100 / s_cfg.speed / 1000);   /* real ms, + = late */
    ln_grade_t g;
    if (s_cfg.mode == LN_MODE_WAIT) {
        g = s_wait_wrong ? LG_GOOD : LG_PERFECT;
    } else {
        int a = abs(dt_ms);
        g = a <= WIN_PERFECT_MS ? LG_PERFECT : (a <= WIN_GOOD_MS ? LG_GOOD : LG_GOOD);
        if (a > WIN_GOOD_MS) {
            /* within the miss window but outside Good: too far off to count */
            grade(LG_WRONG, -1, key, now_us, 0);
            unlock();
            return;
        }
    }
    grade(g, best, key, now_us, dt_ms);
    unlock();
}

/* ------------------------------------------------------------------ playback */

static void find_wait(void)
{
    /* the notes of the chord we stopped at that you haven't played yet */
    s_st.nwait = 0;
    for (int i = s_judge; i < g_ln.n && g_ln.notes[i].t <= s_wait_t + CHORD_MS; i++) {
        if (g_ln.notes[i].t >= s_wait_t && g_ln_user[i] && !g_ln_judged[i] && s_st.nwait < 8) {
            s_st.wait_keys[s_st.nwait++] = (uint8_t)key_of(i);
        }
    }
}

static void seek_to(int64_t song_us, bool reset_judged)
{
    all_off();
    s_st.song_us = song_us;
    s_box = 0;
    while (s_box < g_ln.n && note_us(s_box) < song_us) {
        s_box++;
    }
    if (reset_judged) {
        for (int i = s_box; i < g_ln.n; i++) {
            g_ln_judged[i] = LJ_NONE;
        }
    }
    s_judge = s_box;
    s_st.waiting = false;
    s_wait_wrong = false;
}

void ln_step(int64_t now_us)
{
    if (!s_mx) {
        return;
    }
    lock();
    int64_t dt = now_us - s_last_us;
    s_last_us = now_us;
    ln_state_t st = s_st.state;
    if (st != LE_RUN && st != LE_COUNTIN) {
        unlock();
        return;
    }
    if (!s_st.waiting) {
        s_st.song_us += dt * s_cfg.speed / 100;
    }
    int64_t song = s_st.song_us;

    if (st == LE_COUNTIN) {
        int64_t beat_us = (int64_t)g_ln.beat_ms * 1000;
        int beat = beat_us ? (int)((song - s_countin_from) / beat_us) : 0;
        int total = s_cfg.countin_bars * g_ln.beats_per_bar;
        if (beat != s_last_beat && beat < total) {
            s_last_beat = beat;
            midi_out_note(CLICK_CH, beat % g_ln.beats_per_bar == 0 ? 76 : 77, 100, true);
            midi_out_note(CLICK_CH, 76, 0, false);
            midi_out_note(CLICK_CH, 77, 0, false);
        }
        s_st.countin_left = total - beat;
        if (song >= (int64_t)s_cfg.start_ms * 1000) {
            s_st.state = LE_RUN;
            s_st.countin_left = 0;
            s_last_beat = -1;
        }
        unlock();
        return;
    }

    /* metronome: every beat from the song start, not while waiting for you */
    if (s_cfg.metronome && g_ln.beat_ms && !s_st.waiting && song >= 0) {
        int beat = (int)(song / ((int64_t)g_ln.beat_ms * 1000));
        if (beat != s_last_beat && song < (int64_t)g_ln.len_ms * 1000) {
            s_last_beat = beat;
            int bpb = g_ln.beats_per_bar ? g_ln.beats_per_bar : 4;
            midi_out_note(CLICK_CH, beat % bpb == 0 ? 76 : 77, 90, true);
            midi_out_note(CLICK_CH, 76, 0, false);
            midi_out_note(CLICK_CH, 77, 0, false);
        }
    }

    /* loop */
    if (s_st.loop && song >= (int64_t)s_st.loop_b * 1000) {
        seek_to((int64_t)s_st.loop_a * 1000, true);
        song = s_st.song_us;
    }

    /* wait mode: stop at your next note */
    if (s_cfg.mode == LN_MODE_WAIT) {
        if (s_st.waiting) {
            find_wait();
            if (s_st.nwait == 0) {
                s_st.waiting = false;     /* all played: carry on */
                s_wait_wrong = false;
            }
        } else {
            for (int i = s_judge; i < g_ln.n && note_us(i) <= song; i++) {
                if (g_ln_user[i] && !g_ln_judged[i]) {
                    s_st.song_us = song = note_us(i);   /* hold here */
                    s_wait_t = g_ln.notes[i].t;
                    s_st.waiting = true;
                    s_wait_wrong = false;
                    find_wait();
                    if (s_st.nwait == 0) {
                        s_st.waiting = false;   /* already played early */
                    }
                    break;
                }
            }
        }
    }

    /* notes the box plays */
    while (s_box < g_ln.n && note_us(s_box) <= song) {
        const ln_note_t *n = &g_ln.notes[s_box];
        if (!g_ln_user[s_box] && s_nact < MAX_ACTIVE) {
            midi_out_note(n->ch, n->note, n->vel, true);
            s_act[s_nact].end_us = note_us(s_box) + (int64_t)n->len * 1000;
            s_act[s_nact].ch = n->ch;
            s_act[s_nact].note = n->note;
            s_nact++;
        }
        s_box++;
    }
    for (int i = 0; i < s_nact;) {
        if (s_act[i].end_us <= song) {
            midi_out_note(s_act[i].ch, s_act[i].note, 0, false);
            s_act[i] = s_act[--s_nact];
        } else {
            i++;
        }
    }

    /* play mode: notes you didn't play in time */
    int64_t late = (int64_t)WIN_MISS_MS * 1000 * s_cfg.speed / 100;
    while (s_judge < g_ln.n && note_us(s_judge) < song - late) {
        if (g_ln_user[s_judge] && !g_ln_judged[s_judge] && s_cfg.mode == LN_MODE_PLAY) {
            grade(LG_MISS, s_judge, (uint8_t)key_of(s_judge), now_us, 0);
        }
        if (s_cfg.mode == LN_MODE_WAIT && g_ln_user[s_judge] && !g_ln_judged[s_judge]) {
            break;   /* wait mode never passes an unplayed note */
        }
        s_judge++;
    }

    /* the end */
    if (!s_st.loop && song >= (int64_t)g_ln.len_ms * 1000 + 300000 * s_cfg.speed / 100 && s_nact == 0) {
        s_st.state = LE_DONE;
    }
    unlock();
}

static void task(void *arg)
{
    (void)arg;
    for (;;) {
        ln_status_t st;
        st.state = s_st.state;
        if (st.state == LE_RUN || st.state == LE_COUNTIN) {
            ln_step(esp_timer_get_time());
            vTaskDelay(1);
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

/* ------------------------------------------------------------------ control */

void ln_engine_init(void)
{
    s_mx = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(task, "learn", 4096, NULL, 14, NULL, 1);
}

bool ln_start(const ln_cfg_t *cfg)
{
    if (!s_mx || !g_ln.n) {
        return false;
    }
    lock();
    all_off();
    free(g_ln_judged);
    free(g_ln_user);
    g_ln_judged = calloc((size_t)g_ln.n, 1);
    g_ln_user = calloc((size_t)g_ln.n, 1);
    if (!g_ln_judged || !g_ln_user) {
        unlock();
        return false;
    }
    s_cfg = *cfg;
    if (s_cfg.speed < 25) {
        s_cfg.speed = 100;
    }
    memset(&s_st, 0, sizeof(s_st));
    memset(g_ln_pressed, 0, sizeof(g_ln_pressed));
    for (int i = 0; i < g_ln.n; i++) {
        const ln_note_t *n = &g_ln.notes[i];
        int k = n->note + s_cfg.shift;
        bool mine = n->hand < 2 && (s_cfg.hands & (1u << n->hand));
        bool reach = !s_cfg.kb_lo || (k >= s_cfg.kb_lo && k <= s_cfg.kb_hi);
        g_ln_user[i] = mine && reach;
        s_st.user_total += g_ln_user[i];
    }
    /* sounds: each part's program; you play on your hand's channel */
    for (int p = 0; p < g_ln.nparts; p++) {
        if (!g_ln.parts[p].drums) {
            midi_out_program(g_ln.parts[p].ch, g_ln.parts[p].program);
        }
    }
    int up = (s_cfg.hands & 1) ? g_ln.rh_part : g_ln.lh_part;
    if (up < 0) {
        up = g_ln.rh_part >= 0 ? g_ln.rh_part : 0;
    }
    s_user_ch = g_ln.parts[up].ch;
    int64_t start = (int64_t)s_cfg.start_ms * 1000;
    seek_to(start, true);
    s_judge = 0;
    while (s_judge < g_ln.n && note_us(s_judge) < start) {
        s_judge++;
    }
    s_last_us = esp_timer_get_time();
    if (s_cfg.countin_bars && g_ln.beat_ms) {
        s_countin_from = start - (int64_t)s_cfg.countin_bars * g_ln.beats_per_bar * g_ln.beat_ms * 1000;
        s_st.song_us = s_countin_from;
        s_last_beat = -1;
        s_st.state = LE_COUNTIN;
    } else {
        s_st.state = LE_RUN;
        s_last_beat = -1;
    }
    if (!s_listening) {
        midi_in_set_thru(false);   /* the engine sounds your keys itself */
        midi_in_set_listener(listener);
        s_listening = true;
    }
    unlock();
    return true;
}

void ln_stop(void)
{
    if (!s_mx) {
        return;
    }
    lock();
    all_off();
    s_st.state = LE_IDLE;
    if (s_listening) {
        midi_in_set_listener(NULL);
        midi_in_set_thru(g_settings.thru);
        s_listening = false;
    }
    unlock();
    midi_out_all_notes_off();
}

void ln_pause_toggle(void)
{
    lock();
    if (s_st.state == LE_RUN) {
        s_st.state = LE_PAUSED;
        all_off();
    } else if (s_st.state == LE_PAUSED) {
        s_st.state = LE_RUN;
        s_last_us = esp_timer_get_time();
    }
    unlock();
}

void ln_seek_bars(int delta)
{
    lock();
    int64_t pos = s_st.song_us < 0 ? 0 : s_st.song_us;
    int bar = ln_song_bar_at((uint32_t)(pos / 1000));
    /* "back" from just after a bar start goes to the bar before, like a CD player */
    if (delta < 0 && pos - (int64_t)g_ln.bars[bar] * 1000 < 1500000) {
        bar += delta;
    } else if (delta > 0) {
        bar += delta;
    }
    bar = bar < 0 ? 0 : (bar >= g_ln.nbars ? g_ln.nbars - 1 : bar);
    if (s_st.state == LE_DONE) {
        s_st.state = LE_RUN;
    }
    seek_to((int64_t)g_ln.bars[bar] * 1000, true);
    s_last_us = esp_timer_get_time();
    unlock();
}

void ln_loop_toggle(void)
{
    lock();
    if (s_st.loop) {
        s_st.loop = false;
    } else {
        int64_t pos = s_st.song_us < 0 ? 0 : s_st.song_us;
        int bar = ln_song_bar_at((uint32_t)(pos / 1000));
        int end = bar + 4 < g_ln.nbars ? bar + 4 : g_ln.nbars - 1;
        if (end > bar) {
            s_st.loop = true;
            s_st.loop_a = g_ln.bars[bar];
            s_st.loop_b = g_ln.bars[end];
        }
    }
    unlock();
}

void ln_get(ln_status_t *out)
{
    if (!s_mx) {
        memset(out, 0, sizeof(*out));
        return;
    }
    lock();
    *out = s_st;
    unlock();
}

int64_t ln_song_us(void)
{
    return s_st.song_us;
}

int ln_accuracy(const ln_status_t *s)
{
    int judged = s->perfect + s->good + s->miss;
    if (!judged) {
        return 0;
    }
    return (int)((s->perfect * 100 + s->good * 60) / judged);
}

int ln_stars(const ln_status_t *s)
{
    int a = ln_accuracy(s);
    return a >= 92 ? 3 : (a >= 80 ? 2 : (a >= 60 ? 1 : 0));
}

int ln_evenness(const ln_status_t *s)
{
    if (!s->dt_n) {
        return 0;
    }
    /* spread of your timing around your own average: 0 ms -> 100 %, 100 ms or more -> 0 % */
    int64_t n = s->dt_n, mean = s->dt_sum / s->dt_n;
    int64_t var = (int64_t)s->dt_sq / n - mean * mean;
    int sd = 0;
    while ((int64_t)(sd + 1) * (sd + 1) <= var) {
        sd++;
    }
    return sd >= 100 ? 0 : 100 - sd;
}

void ln_set_metronome(bool on)
{
    if (!s_mx) {
        return;
    }
    lock();
    s_cfg.metronome = on;
    unlock();
}
