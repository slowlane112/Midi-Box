#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "midi_out.h"
#include "midi_in.h"
#include "monitor.h"
#include "settings.h"
#include "song.h"
#include "seq.h"

#define MAX_ACTIVE   128
#define CLICK_CH     9
#define CLICK_HI     76    /* hi wood block on beat 1 */
#define CLICK_LO     77    /* low wood block */
#define CLICK_US     40000

typedef struct {
    int64_t us;
    uint8_t m[3];
    uint8_t len;
} in_msg_t;

typedef struct {
    uint8_t ch, note;
    int64_t off;            /* tick */
} active_t;

static QueueHandle_t s_inq;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* settings from the UI */
static volatile bool s_activated;
static volatile int s_armed;
static volatile seq_key_hook_t s_key_hook;   /* Notes screen: which key you pressed */
static volatile bool s_click = true;
static volatile bool s_overdub = true;

/* commands (set by the UI, consumed by the task) */
typedef enum { C_NONE, C_PLAY, C_RECORD, C_STOP } cmd_t;
static volatile cmd_t s_cmd;
static uint32_t s_cmd_from;
static seq_rec_t s_cmd_rec;

/* engine state (task only, published through s_status) */
static seq_state_t s_state = SEQ_STOPPED;
static int64_t s_t0;              /* esp_timer us where the clock started */
static int64_t s_start_tick;      /* tick at s_t0 (negative during count-in) */
static uint16_t s_bpm;
static int s_cur[SONG_TRACKS];
static active_t s_act[MAX_ACTIVE];
static int s_nact;
static int64_t s_last_beat;
static int64_t s_click_off_us;
static int8_t s_click_note = -1;

static seq_rec_t s_rec;
static bool s_recording_take;     /* a take exists (recording or pending) */
static uint8_t s_round;
static int s_take_notes;
static int64_t s_rec_from, s_rec_to;   /* recorded range, for replace mode */
static struct {
    bool on;
    int64_t tick;
    uint8_t vel;
} s_open[128];

static seq_status_t s_status;

/* ------------------------------------------------------------------ helpers */

static int64_t tick_at(int64_t us)
{
    return s_start_tick + (us - s_t0) * (int64_t)s_bpm * SONG_PPQ / 60000000LL;
}

static int64_t floor_div(int64_t a, int64_t b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

static void publish(int64_t tick)
{
    portENTER_CRITICAL(&s_mux);
    s_status.state = s_state;
    s_status.tick = (int32_t)tick;
    s_status.round = s_round;
    s_status.take_notes = s_take_notes;
    s_status.take_pending = s_recording_take && s_state == SEQ_STOPPED;
    portEXIT_CRITICAL(&s_mux);
}

static void all_off(void)
{
    for (int i = 0; i < s_nact; i++) {
        midi_out_note(s_act[i].ch, s_act[i].note, 0, false);
        monitor_note(s_act[i].ch, s_act[i].note, 0, false);
    }
    s_nact = 0;
    if (s_click_note >= 0) {
        midi_out_note(CLICK_CH, (uint8_t)s_click_note, 0, false);
        s_click_note = -1;
    }
    for (uint8_t ch = 0; ch < 16; ch++) {
        midi_out_cc(ch, 64, 0);
    }
}

static void reset_cursors(uint32_t tick)
{
    for (int t = 0; t < g_song.ntracks; t++) {
        s_cur[t] = ev_first_at(&g_song.tr[t], tick);
    }
}

/* restore each track's instrument and volume */
static void send_setup(void)
{
    midi_out_set_bank(g_song.bank);
    for (int t = 0; t < g_song.ntracks; t++) {
        const strack_t *tr = &g_song.tr[t];
        if (tr->channel != 9) {
            midi_out_cc(tr->channel, 0, g_song.bank ? 127 : 0);
        }
        midi_out_program(tr->channel, tr->program);
        midi_out_cc(tr->channel, 7, tr->volume);
    }
}

static void start_clock(int64_t tick)
{
    s_t0 = esp_timer_get_time();
    s_start_tick = tick;
    s_bpm = g_song.bpm ? g_song.bpm : 120;
    s_last_beat = floor_div(tick, SONG_PPQ) - 1;
}

/* close notes still held at 'tick' into the armed track */
static void close_open_notes(int64_t tick)
{
    strack_t *tr = &g_song.tr[s_armed];
    for (int n = 0; n < 128; n++) {
        if (!s_open[n].on) {
            continue;
        }
        s_open[n].on = false;
        int64_t len = tick - s_open[n].tick;
        sev_t e = { (uint32_t)s_open[n].tick, (uint32_t)(len > 0 ? len : 1), EV_NOTE, (uint8_t)n, s_open[n].vel, s_round };
        int i = ev_insert(tr, &e);
        if (i >= 0 && i < s_cur[s_armed]) {
            s_cur[s_armed]++;
        }
        s_take_notes++;
    }
}

/* ------------------------------------------------------------------ input */

static void on_input(const uint8_t *msg, uint16_t len, uint8_t source)
{
    (void)source;
    if (!s_activated || len < 2 || msg[0] >= 0xF0) {
        return;
    }
    int armed = s_armed;
    uint8_t ch = (armed >= 0 && armed < g_song.ntracks) ? g_song.tr[armed].channel : 0;
    uint8_t m[3] = { (uint8_t)((msg[0] & 0xF0) | ch), msg[1], len > 2 ? msg[2] : 0 };
    midi_out_send(m, len > 3 ? 3 : len);      /* play live on the armed track's sound */
    seq_key_hook_t hook = s_key_hook;
    if (hook && len > 2 && (msg[0] & 0xF0) == 0x90 && msg[2]) {
        hook(msg[1]);
    }
    if (s_state == SEQ_RECORDING || s_state == SEQ_COUNTIN) {
        in_msg_t im = { .us = esp_timer_get_time(), .len = (uint8_t)(len > 3 ? 3 : len) };
        memcpy(im.m, msg, im.len);
        xQueueSend(s_inq, &im, 0);
    }
}

static void record_msg(const in_msg_t *im)
{
    int64_t t = tick_at(im->us);
    int64_t from = s_rec.loop ? s_rec.loop_start : s_rec.start;
    if (t < from) {
        if (t < from - SONG_PPQ / 8) {
            return;           /* clearly before the start */
        }
        t = from;             /* a little early: snap to the start */
    }
    uint8_t type = im->m[0] & 0xF0;
    uint8_t a = im->m[1] & 0x7F, b = im->len > 2 ? (im->m[2] & 0x7F) : 0;
    strack_t *tr = &g_song.tr[s_armed];
    if (type == 0x90 && b) {
        if (s_open[a].on) {
            s_open[a].on = false;   /* retrigger: end the previous one here */
            sev_t e = { (uint32_t)s_open[a].tick, (uint32_t)(t - s_open[a].tick ? t - s_open[a].tick : 1), EV_NOTE, a, s_open[a].vel, s_round };
            int i = ev_insert(tr, &e);
            if (i >= 0 && i < s_cur[s_armed]) {
                s_cur[s_armed]++;
            }
            s_take_notes++;
        }
        s_open[a].on = true;
        s_open[a].tick = t;
        s_open[a].vel = b;
    } else if (type == 0x80 || type == 0x90) {
        if (!s_open[a].on) {
            return;
        }
        s_open[a].on = false;
        int64_t l = t - s_open[a].tick;
        sev_t e = { (uint32_t)s_open[a].tick, (uint32_t)(l > 0 ? l : 1), EV_NOTE, a, s_open[a].vel, s_round };
        int i = ev_insert(tr, &e);
        if (i >= 0 && i < s_cur[s_armed]) {
            s_cur[s_armed]++;   /* already behind the playhead: don't replay it this pass */
        }
        s_take_notes++;
    } else if (type == 0xB0 && (a == 64 || a == 1 || a == 11)) {
        sev_t e = { (uint32_t)t, 0, EV_CC, a, b, s_round };
        int i = ev_insert(tr, &e);
        if (i >= 0 && i < s_cur[s_armed]) {
            s_cur[s_armed]++;
        }
    }
    if (t > s_rec_to) {
        s_rec_to = t;
    }
}

/* ------------------------------------------------------------------ engine */

static void begin(uint32_t from, bool record)
{
    all_off();
    send_setup();
    memset(s_open, 0, sizeof(s_open));
    if (record) {
        s_rec = s_cmd_rec;
        song_save_undo(s_armed);           /* Studio "Undo" takes the whole take back */
        s_recording_take = true;
        s_round = 1;
        s_take_notes = 0;
        uint32_t start = s_rec.loop ? s_rec.loop_start : s_rec.start;
        s_rec_from = start;
        s_rec_to = start;
        int64_t pre = (int64_t)s_rec.countin_bars * song_bar_ticks();
        reset_cursors(start);
        start_clock((int64_t)start - pre);
        s_state = pre ? SEQ_COUNTIN : SEQ_RECORDING;
        xQueueReset(s_inq);
    } else {
        reset_cursors(from);
        start_clock(from);
        s_state = SEQ_PLAYING;
    }
}

static void stop_engine(int64_t tick)
{
    if (s_state == SEQ_RECORDING || s_state == SEQ_COUNTIN) {
        in_msg_t im;
        while (xQueueReceive(s_inq, &im, 0) == pdTRUE) {
            record_msg(&im);
        }
        close_open_notes(tick);
        if (tick > s_rec_to) {
            s_rec_to = tick;
        }
    }
    all_off();
    s_state = SEQ_STOPPED;
}

static void step(void)
{
    int64_t now = esp_timer_get_time();
    if (g_song.bpm && g_song.bpm != s_bpm) {           /* tempo changed while playing */
        int64_t t = tick_at(now);
        start_clock(t);
        s_last_beat = floor_div(t, SONG_PPQ);
    }
    int64_t T = tick_at(now);

    /* metronome while counting in or recording */
    int64_t beat = floor_div(T, SONG_PPQ);
    if (beat != s_last_beat) {
        s_last_beat = beat;
        if (s_click && (s_state == SEQ_COUNTIN || s_state == SEQ_RECORDING)) {
            bool one = floor_div(beat, 1) % g_song.beats == 0;
            if (s_click_note >= 0) {
                midi_out_note(CLICK_CH, (uint8_t)s_click_note, 0, false);
            }
            s_click_note = one ? CLICK_HI : CLICK_LO;
            midi_out_note(CLICK_CH, (uint8_t)s_click_note, one ? 110 : 70, true);
            s_click_off_us = now + CLICK_US;
        }
    }
    if (s_click_note >= 0 && now >= s_click_off_us) {
        midi_out_note(CLICK_CH, (uint8_t)s_click_note, 0, false);
        s_click_note = -1;
    }

    if (s_state == SEQ_COUNTIN) {
        int64_t start = s_rec.loop ? s_rec.loop_start : s_rec.start;
        if (T >= start) {
            s_state = SEQ_RECORDING;
        }
    }

    /* recorded input */
    if (s_state == SEQ_RECORDING || s_state == SEQ_COUNTIN) {
        in_msg_t im;
        while (xQueueReceive(s_inq, &im, 0) == pdTRUE) {
            record_msg(&im);
        }
    }

    /* note offs */
    for (int i = 0; i < s_nact;) {
        if (s_act[i].off <= T) {
            midi_out_note(s_act[i].ch, s_act[i].note, 0, false);
            monitor_note(s_act[i].ch, s_act[i].note, 0, false);
            s_act[i] = s_act[--s_nact];
        } else {
            i++;
        }
    }

    /* events up to now */
    bool replace = s_state == SEQ_RECORDING && !s_overdub;
    for (int t = 0; t < g_song.ntracks && T >= 0; t++) {
        strack_t *tr = &g_song.tr[t];
        bool sounds = song_track_sounds(t);
        while (s_cur[t] < tr->n && (int64_t)tr->ev[s_cur[t]].tick < T) {
            const sev_t *e = &tr->ev[s_cur[t]++];
            if (!sounds) {
                continue;
            }
            if (e->round != 0 && e->round == s_round) {
                continue;   /* recorded in this pass: you just played it, it plays back next pass */
            }
            if (t == s_armed && e->round == 0 && replace && (int64_t)e->tick >= s_rec_from) {
                continue;   /* replace mode: the old part is not heard while you play the new one */
            }
            if (e->type == EV_CC) {
                midi_out_cc(tr->channel, e->a, e->b);
                continue;
            }
            if (s_nact >= MAX_ACTIVE) {
                continue;
            }
            midi_out_note(tr->channel, e->a, e->b, true);
            monitor_note(tr->channel, e->a, e->b, true);
            s_act[s_nact++] = (active_t){ tr->channel, e->a, (int64_t)e->tick + (e->len ? e->len : 1) };
        }
    }

    /* loop / end */
    if (s_state == SEQ_RECORDING && s_rec.loop && T >= (int64_t)s_rec.loop_end) {
        in_msg_t im;
        while (xQueueReceive(s_inq, &im, 0) == pdTRUE) {
            record_msg(&im);
        }
        close_open_notes(s_rec.loop_end);
        s_rec_to = s_rec.loop_end;
        all_off();
        if (s_round < 250) {
            s_round++;
        }
        int64_t over = T - (int64_t)s_rec.loop_end;
        reset_cursors(s_rec.loop_start);
        start_clock((int64_t)s_rec.loop_start + over);
        s_last_beat = floor_div((int64_t)s_rec.loop_start + over, SONG_PPQ);
    } else if (s_state == SEQ_PLAYING) {
        int64_t end = (int64_t)song_end_tick() + song_bar_ticks();
        int64_t len = (int64_t)g_song.bars * song_bar_ticks();
        if (T > (end > len ? end : len)) {
            stop_engine(T);
        }
    }
    publish(T);
}

static void seq_task(void *arg)
{
    (void)arg;
    for (;;) {
        cmd_t c = s_cmd;
        if (c != C_NONE) {
            s_cmd = C_NONE;
            song_lock();
            if (c == C_PLAY) {
                begin(s_cmd_from, false);
            } else if (c == C_RECORD) {
                begin(0, true);
            } else if (s_state != SEQ_STOPPED) {
                stop_engine(tick_at(esp_timer_get_time()));
            }
            song_unlock();
            publish(s_status.tick);
        }
        if (s_state == SEQ_STOPPED) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        song_lock();
        step();
        song_unlock();
        vTaskDelay(1);
    }
}

/* ------------------------------------------------------------------ API */

void seq_init(void)
{
    s_inq = xQueueCreate(128, sizeof(in_msg_t));
    xTaskCreatePinnedToCore(seq_task, "seq", 4096, NULL, 14, NULL, 1);
}

void seq_activate(bool on)
{
    s_activated = on;
    midi_in_set_listener(on ? on_input : NULL);
    midi_in_set_thru(on ? false : g_settings.thru);   /* Studio sends input itself, on the armed channel */
}

void seq_set_armed(int track) { s_armed = track; }
void seq_set_key_hook(seq_key_hook_t fn) { s_key_hook = fn; }

void seq_send_setup(void)
{
    song_lock();
    send_setup();
    song_unlock();
}
void seq_set_click(bool on) { s_click = on; }
void seq_set_overdub(bool on) { s_overdub = on; }

static void wait_cmd(void)
{
    for (int i = 0; i < 200 && s_cmd != C_NONE; i++) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

void seq_play(uint32_t from)
{
    s_cmd_from = from;
    s_cmd = C_PLAY;
    wait_cmd();
}

void seq_record(const seq_rec_t *r)
{
    s_cmd_rec = *r;
    s_overdub = r->overdub;
    s_cmd = C_RECORD;
    wait_cmd();
}

void seq_stop(void)
{
    s_cmd = C_STOP;
    wait_cmd();
}

void seq_get(seq_status_t *o)
{
    portENTER_CRITICAL(&s_mux);
    *o = s_status;
    portEXIT_CRITICAL(&s_mux);
}

/* the UI calls these while stopped; they hold the song lock */

static void remove_rounds(int min_round, int max_round)
{
    strack_t *tr = &g_song.tr[s_armed];
    int k = 0;
    for (int i = 0; i < tr->n; i++) {
        uint8_t r = tr->ev[i].round;
        if (r >= min_round && r <= max_round) {
            if (tr->ev[i].type == EV_NOTE) {
                s_take_notes--;
            }
            continue;
        }
        tr->ev[k++] = tr->ev[i];
    }
    tr->n = k;
    if (s_take_notes < 0) {
        s_take_notes = 0;
    }
}

void seq_keep(void)
{
    if (!s_recording_take || s_state != SEQ_STOPPED) {
        return;
    }
    song_lock();
    strack_t *tr = &g_song.tr[s_armed];
    if (!s_overdub) {
        int64_t from = s_rec.loop ? s_rec.loop_start : s_rec_from;
        int64_t to = s_rec.loop ? s_rec.loop_end : s_rec_to;
        int k = 0;
        for (int i = 0; i < tr->n; i++) {
            const sev_t *e = &tr->ev[i];
            if (e->round == 0 && (int64_t)e->tick >= from && (int64_t)e->tick < to) {
                continue;   /* replaced */
            }
            tr->ev[k++] = *e;
        }
        tr->n = k;
    }
    for (int i = 0; i < tr->n; i++) {
        tr->ev[i].round = 0;
    }
    song_fit_length();
    g_song.dirty = true;
    s_recording_take = false;
    song_unlock();
    publish(s_status.tick);
}

void seq_discard(void)
{
    if (!s_recording_take || s_state != SEQ_STOPPED) {
        return;
    }
    song_lock();
    remove_rounds(1, 255);
    s_recording_take = false;
    song_unlock();
    publish(s_status.tick);
}

void seq_undo_round(void)
{
    if (!s_recording_take) {
        return;
    }
    song_lock();
    /* while looping, take back the last finished pass; otherwise everything so far */
    int r = s_round > 1 ? s_round - 1 : s_round;
    remove_rounds(r, r);
    reset_cursors((uint32_t)(s_status.tick > 0 ? s_status.tick : 0));
    song_unlock();
}
