/*
 * player.c - plays an smf_t with microsecond timing.
 *
 * Timing: song time (us) is derived from ticks through the tempo map.
 * wall0 is the esp_timer time at which song time 0 would have played at the
 * current speed, so: due = wall0 + song_us * 100 / speed.
 * Pause, seek and speed changes just move wall0.
 *
 * Seeking "chases" the song from the start without playing notes, collecting
 * the last program / controllers / pitch bend per channel, then sends that
 * state so instruments and volumes are right at the new position.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "midi_out.h"
#include "monitor.h"
#include "settings.h"
#include "player.h"

static const char *TAG = "player";

typedef enum {
    CMD_LOAD, CMD_PLAY, CMD_PAUSE, CMD_TOGGLE, CMD_STOP, CMD_UNLOAD,
    CMD_SEEK_REL, CMD_SEEK_ABS, CMD_SPEED, CMD_BANK, CMD_WAKE,
} cmd_type_t;

typedef struct {
    cmd_type_t type;
    int32_t a;
    bool flag;
    char path[FS_PATH_MAX];
} cmd_t;

typedef struct {
    const uint8_t *p, *end;
    uint32_t tick;
    uint8_t rs;
    bool done;
} cursor_t;

/* ---- shared with the UI ---- */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static player_state_t s_state = PLAYER_EMPTY;
static int64_t s_wall0;              /* see header comment */
static uint64_t s_pos_us;            /* position when not playing */
static volatile uint16_t s_speed = 100;
static volatile int8_t s_transpose;
static volatile uint8_t s_bank;
static volatile bool s_repeat;
#define SONG_GAP_US 1000000   /* silence between the end of a song and the next start (Repeat too) */
static uint32_t s_load_id;
static smf_err_t s_error;
static uint8_t s_flags[SMF_MAX_PARTS];
static volatile uint32_t s_flags_gen;
static volatile uint8_t s_part_level[SMF_MAX_PARTS];

/* ---- player task only ---- */
static QueueHandle_t s_q;
static SemaphoreHandle_t s_lock;
static smf_t s_smf;
static smf_t s_tmp;
static cursor_t *s_cur;
static uint32_t s_last_tick;
static uint64_t s_last_us;
static uint32_t s_tempo = 500000;
static uint8_t s_act[16][128];       /* sent note + 1, 0 = not sounding */
static uint8_t s_act_part[16][128];  /* part + 1 that started it */
static uint32_t s_seen_gen;
static bool s_any_solo;

/* chase state */
static uint8_t s_ch_prog[16];
static uint8_t s_ch_cc[16][128];
static uint8_t s_ch_cc_set[16][16];
static uint16_t s_ch_pb[16];
static uint16_t s_ch_used;           /* channels with any event before the seek point, or with notes */

/* ------------------------------------------------------------------ helpers */

static int64_t now_us(void) { return esp_timer_get_time(); }

static void set_state(player_state_t st)
{
    portENTER_CRITICAL(&s_mux);
    s_state = st;
    portEXIT_CRITICAL(&s_mux);
}

static uint64_t song_now_us(void)
{
    if (s_state != PLAYER_PLAYING) {
        return s_pos_us;
    }
    int64_t d = now_us() - s_wall0;
    if (d < 0) {
        d = 0;
    }
    uint64_t us = (uint64_t)d * s_speed / 100;
    return us > s_smf.total_us ? s_smf.total_us : us;
}

static void rebase_wall(uint64_t song_us)
{
    portENTER_CRITICAL(&s_mux);
    s_wall0 = now_us() - (int64_t)(song_us * 100 / s_speed);
    s_pos_us = song_us;
    portEXIT_CRITICAL(&s_mux);
}

static bool part_sounds(int part)
{
    if (part < 0) {
        return true;
    }
    uint8_t f = s_flags[part];
    return s_any_solo ? (f & PART_SOLO) != 0 : (f & PART_MUTE) == 0;
}

static bool part_visible(int part)
{
    return part < 0 || !(s_flags[part] & PART_HIDE);
}

static void release_all(void)
{
    for (int ch = 0; ch < 16; ch++) {
        for (int n = 0; n < 128; n++) {
            if (s_act[ch][n]) {
                midi_out_note((uint8_t)ch, (uint8_t)(s_act[ch][n] - 1), 0, false);
            }
        }
        midi_out_cc((uint8_t)ch, 64, 0);
    }
    memset(s_act, 0, sizeof(s_act));
    memset(s_act_part, 0, sizeof(s_act_part));
    monitor_clear();
}

/* After mute / solo / hide changes: silence notes of parts that no longer sound. */
static void apply_flags(void)
{
    s_seen_gen = s_flags_gen;
    s_any_solo = false;
    for (int i = 0; i < s_smf.nparts; i++) {
        if (s_flags[i] & PART_SOLO) {
            s_any_solo = true;
        }
    }
    for (int ch = 0; ch < 16; ch++) {
        for (int n = 0; n < 128; n++) {
            if (!s_act[ch][n]) {
                continue;
            }
            int part = (int)s_act_part[ch][n] - 1;
            uint8_t sent = s_act[ch][n] - 1;
            if (!part_sounds(part)) {
                midi_out_note((uint8_t)ch, sent, 0, false);
                monitor_note((uint8_t)ch, sent, 0, false);
                s_act[ch][n] = 0;
            } else if (!part_visible(part)) {
                monitor_note((uint8_t)ch, sent, 0, false);
            }
        }
    }
}

/* ------------------------------------------------------------------ cursors */

static void cursor_next_delta(cursor_t *c)
{
    uint32_t d;
    if (c->p >= c->end || !smf_read_vlq(&c->p, c->end, &d)) {
        c->done = true;
        return;
    }
    c->tick += d;
}

static void reset_cursors(void)
{
    for (int t = 0; t < s_smf.ntracks; t++) {
        cursor_t *c = &s_cur[t];
        c->p = s_smf.tracks[t].data;
        c->end = c->p + s_smf.tracks[t].len;
        c->tick = 0;
        c->rs = 0;
        c->done = false;
        cursor_next_delta(c);
    }
    s_last_tick = 0;
    s_last_us = 0;
    s_tempo = 500000;
}

static int next_track(void)
{
    int best = -1;
    uint32_t bt = UINT32_MAX;
    for (int t = 0; t < s_smf.ntracks; t++) {
        if (!s_cur[t].done && s_cur[t].tick < bt) {
            bt = s_cur[t].tick;
            best = t;
        }
    }
    return best;
}

static uint64_t us_at_tick(uint32_t tick)
{
    uint32_t d = tick - s_last_tick;
    if (s_smf.ppq == 0) {
        return s_last_us + (uint64_t)d * s_smf.smpte_us_per_tick;
    }
    return s_last_us + (uint64_t)d * s_tempo / s_smf.ppq;
}

static void advance_to(uint32_t tick)
{
    s_last_us = us_at_tick(tick);
    s_last_tick = tick;
}

/* ------------------------------------------------------------------ events */

static void channel_event(int track, uint8_t st, uint8_t d1, uint8_t d2, uint8_t len, bool chase)
{
    uint8_t ch = st & 0x0F, type = st & 0xF0;
    int part = smf_part_index(&s_smf, (uint16_t)track, ch);
    if (chase) {
        s_ch_used |= (uint16_t)(1u << ch);
    }

    if (type == 0x90 && d2 > 0) {
        if (chase) {
            return;
        }
        int n2 = d1;
        if (ch != 9) {
            n2 += s_transpose;
            if (n2 < 0 || n2 > 127) {
                return;
            }
        }
        if (part >= 0 && d2 > s_part_level[part]) {
            s_part_level[part] = d2;
        }
        if (!part_sounds(part)) {
            return;
        }
        if (s_act[ch][d1] && s_act[ch][d1] - 1 != n2) {
            midi_out_note(ch, (uint8_t)(s_act[ch][d1] - 1), 0, false);
        }
        midi_out_note(ch, (uint8_t)n2, d2, true);
        s_act[ch][d1] = (uint8_t)(n2 + 1);
        s_act_part[ch][d1] = (uint8_t)(part + 1);
        if (part_visible(part)) {
            monitor_note(ch, (uint8_t)n2, d2, true);
        }
        return;
    }
    if (type == 0x80 || type == 0x90) {
        if (chase || !s_act[ch][d1]) {
            return;
        }
        uint8_t sent = s_act[ch][d1] - 1;
        midi_out_note(ch, sent, 0, false);
        monitor_note(ch, sent, 0, false);
        s_act[ch][d1] = 0;
        return;
    }
    if (chase) {
        if (type == 0xC0) {
            s_ch_prog[ch] = d1;
        } else if (type == 0xB0) {
            s_ch_cc[ch][d1] = d2;
            s_ch_cc_set[ch][d1 >> 3] |= (uint8_t)(1 << (d1 & 7));
        } else if (type == 0xE0) {
            s_ch_pb[ch] = (uint16_t)(d1 | (d2 << 7));
        }
        return;
    }
    uint8_t m[3] = { st, d1, d2 };
    midi_out_send(m, len);
    if (type == 0xB0 && part_visible(part)) {
        monitor_cc(ch, d1, d2);
    }
}

/* Handle the event under cursor t (its tick is already the current time). */
static void do_event(int t, bool chase)
{
    cursor_t *c = &s_cur[t];
    const uint8_t *p = c->p, *end = c->end;
    if (p >= end) {
        c->done = true;
        return;
    }
    uint8_t st = *p;
    if (st == 0xFF) {
        if (p + 2 > end) {
            c->done = true;
            return;
        }
        uint8_t type = p[1];
        p += 2;
        uint32_t len;
        if (!smf_read_vlq(&p, end, &len) || p + len > end) {
            c->done = true;
            return;
        }
        if (type == 0x51 && len == 3) {
            uint32_t us = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
            if (us) {
                s_tempo = us;
            }
        } else if (type == 0x2F) {
            c->p = p + len;
            c->done = true;
            return;
        }
        c->p = p + len;
    } else if (st == 0xF0 || st == 0xF7) {
        p++;
        uint32_t len;
        if (!smf_read_vlq(&p, end, &len) || p + len > end) {
            c->done = true;
            return;
        }
        if (len > 0 && len < 512) {
            /* F0 events: the length counts the data after F0; F7 "escapes" are sent raw */
            static uint8_t buf[513];
            size_t n = 0;
            if (st == 0xF0) {
                buf[n++] = 0xF0;
            }
            memcpy(buf + n, p, len);
            midi_out_send(buf, n + len);
        }
        c->p = p + len;
    } else {
        if (st & 0x80) {
            c->rs = st;
            p++;
        } else if (!c->rs) {
            c->done = true;
            return;
        }
        uint8_t n = smf_data_len(c->rs);
        if (p + n > end) {
            c->done = true;
            return;
        }
        uint8_t d1 = p[0] & 0x7F;
        uint8_t d2 = n > 1 ? (p[1] & 0x7F) : 0;
        c->p = p + n;
        channel_event(t, c->rs, d1, d2, (uint8_t)(n + 1), chase);
    }
    cursor_next_delta(c);
}

static bool skip_cc_on_chase(uint8_t cc)
{
    return cc == 0 || cc == 32 || cc == 6 || cc == 38 || (cc >= 96 && cc <= 101) || cc >= 120;
}

/* Position the song at target_us and restore channel state. */
static void seek_to(uint64_t target_us)
{
    release_all();
    memset(s_ch_prog, 0xFF, sizeof(s_ch_prog));
    memset(s_ch_cc_set, 0, sizeof(s_ch_cc_set));
    s_ch_used = 0;
    for (int i = 0; i < s_smf.nparts; i++) {
        s_ch_used |= (uint16_t)(1u << s_smf.parts[i].channel);
    }
    for (int ch = 0; ch < 16; ch++) {
        s_ch_pb[ch] = 0xFFFF;
    }
    reset_cursors();
    for (;;) {
        int t = next_track();
        if (t < 0 || us_at_tick(s_cur[t].tick) >= target_us) {
            break;
        }
        advance_to(s_cur[t].tick);
        do_event(t, true);
    }
    for (uint8_t ch = 0; ch < 16; ch++) {
        if (!(s_ch_used & (1u << ch))) {
            continue;
        }
        midi_out_cc(ch, 121, 0);   /* reset all controllers, then restore */
        bool bank_set = false;
        for (uint8_t cc = 0; cc < 33; cc += 32) {
            if (s_ch_cc_set[ch][cc >> 3] & (1 << (cc & 7))) {
                midi_out_cc(ch, cc, s_ch_cc[ch][cc]);
                bank_set = true;
            }
        }
        if (!bank_set && ch != 9) {
            midi_out_cc(ch, 0, s_bank == BANK_MT32 ? 127 : 0);
        }
        midi_out_program(ch, s_ch_prog[ch] == 0xFF ? 0 : s_ch_prog[ch]);
        for (int cc = 1; cc < 120; cc++) {
            if (!skip_cc_on_chase((uint8_t)cc) && (s_ch_cc_set[ch][cc >> 3] & (1 << (cc & 7)))) {
                midi_out_cc(ch, (uint8_t)cc, s_ch_cc[ch][cc]);
            }
        }
        if (s_ch_pb[ch] != 0xFFFF) {
            midi_out_pitch(ch, s_ch_pb[ch]);
        }
    }
    rebase_wall(target_us);
}

/* ------------------------------------------------------------------ commands */

static void do_load(const cmd_t *cmd)
{
    release_all();
    set_state(PLAYER_LOADING);

    smf_err_t err = smf_load(&s_tmp, cmd->path);
    cursor_t *cur = NULL;
    if (err == SMF_OK) {
        cur = calloc(s_tmp.ntracks, sizeof(cursor_t));
        if (!cur) {
            smf_free(&s_tmp);
            err = SMF_ERR_MEMORY;
        }
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    smf_free(&s_smf);
    free(s_cur);
    s_cur = NULL;
    if (err == SMF_OK) {
        s_smf = s_tmp;
        memset(&s_tmp, 0, sizeof(s_tmp));
        s_cur = cur;
        memset(s_flags, 0, sizeof(s_flags));
        memset((void *)s_part_level, 0, sizeof(s_part_level));
    }
    xSemaphoreGive(s_lock);

    if (err != SMF_OK) {
        ESP_LOGW(TAG, "load %s failed: %d", cmd->path, err);
        portENTER_CRITICAL(&s_mux);
        s_load_id++;   /* counts attempts, so the UI sees each failure */
        s_error = err;
        s_state = PLAYER_ERROR;
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    apply_flags();
    midi_out_reset(s_bank, g_settings.volume);
    uint64_t start = (uint64_t)cmd->a * 1000;
    if (start >= s_smf.total_us) {
        start = 0;
    }
    seek_to(start);
    portENTER_CRITICAL(&s_mux);
    s_load_id++;
    s_error = SMF_OK;
    s_state = cmd->flag ? PLAYER_PLAYING : (start ? PLAYER_PAUSED : PLAYER_STOPPED);
    portEXIT_CRITICAL(&s_mux);
    if (cmd->flag) {
        rebase_wall(start);
    }
}

static void do_pause(void)
{
    if (s_state != PLAYER_PLAYING) {
        return;
    }
    uint64_t pos = song_now_us();
    portENTER_CRITICAL(&s_mux);
    s_pos_us = pos;
    s_state = PLAYER_PAUSED;
    portEXIT_CRITICAL(&s_mux);
    release_all();
}

static void do_play(void)
{
    if (s_state == PLAYER_ENDED) {
        seek_to(0);
    }
    if (s_state == PLAYER_STOPPED || s_state == PLAYER_PAUSED || s_state == PLAYER_ENDED) {
        rebase_wall(s_pos_us);
        set_state(PLAYER_PLAYING);
    }
}

static void handle(const cmd_t *c)
{
    bool loaded = s_state >= PLAYER_STOPPED && s_state != PLAYER_ERROR;
    switch (c->type) {
    case CMD_LOAD:
        do_load(c);
        break;
    case CMD_PLAY:
        if (loaded) {
            do_play();
        }
        break;
    case CMD_PAUSE:
        do_pause();
        break;
    case CMD_TOGGLE:
        if (s_state == PLAYER_PLAYING) {
            do_pause();
        } else if (loaded) {
            do_play();
        }
        break;
    case CMD_STOP:
        if (loaded) {
            set_state(PLAYER_STOPPED);
            seek_to(0);
        }
        break;
    case CMD_UNLOAD:
        release_all();
        xSemaphoreTake(s_lock, portMAX_DELAY);
        smf_free(&s_smf);
        free(s_cur);
        s_cur = NULL;
        xSemaphoreGive(s_lock);
        set_state(PLAYER_EMPTY);
        break;
    case CMD_SEEK_REL:
    case CMD_SEEK_ABS:
        if (loaded) {
            int64_t target = c->type == CMD_SEEK_ABS ? (int64_t)c->a * 1000 : (int64_t)song_now_us() + (int64_t)c->a * 1000;
            if (target < 0) {
                target = 0;
            }
            if ((uint64_t)target > s_smf.total_us) {
                target = (int64_t)s_smf.total_us;
            }
            player_state_t st = s_state;
            seek_to((uint64_t)target);
            if (st == PLAYER_ENDED) {
                set_state(PLAYER_PAUSED);
            }
        }
        break;
    case CMD_SPEED: {
        uint64_t pos = song_now_us();
        s_speed = (uint16_t)c->a;
        rebase_wall(pos);
        break;
    }
    case CMD_BANK:
        s_bank = (uint8_t)c->a;
        midi_out_set_bank(s_bank);
        break;
    case CMD_WAKE:
        break;
    }
}

/* ------------------------------------------------------------------ task */

static void player_task(void *arg)
{
    (void)arg;
    cmd_t cmd;
    for (;;) {
        if (s_seen_gen != s_flags_gen) {
            apply_flags();
        }
        if (s_state != PLAYER_PLAYING) {
            if (xQueueReceive(s_q, &cmd, portMAX_DELAY) == pdTRUE) {
                handle(&cmd);
            }
            continue;
        }

        int t = next_track();
        if (t < 0) {
            if (s_repeat) {
                /* 1 s of silence before playing it again; commands (stop, seek, load)
                 * still work during the gap */
                release_all();
                portENTER_CRITICAL(&s_mux);
                s_pos_us = s_smf.total_us;
                portEXIT_CRITICAL(&s_mux);
                int64_t until = now_us() + SONG_GAP_US;
                for (int64_t left; s_state == PLAYER_PLAYING && (left = until - now_us()) > 0;) {
                    TickType_t ticks = pdMS_TO_TICKS((uint32_t)(left / 1000) + 1);
                    if (xQueueReceive(s_q, &cmd, ticks) == pdTRUE) {
                        handle(&cmd);
                        if (next_track() >= 0) {
                            break;   /* seeked back into the song: carry on from there */
                        }
                    }
                }
                if (s_state == PLAYER_PLAYING && next_track() < 0) {
                    seek_to(0);
                }
                continue;
            }
            release_all();
            portENTER_CRITICAL(&s_mux);
            s_pos_us = s_smf.total_us;
            s_state = PLAYER_ENDED;
            portEXIT_CRITICAL(&s_mux);
            continue;
        }

        uint64_t ev_us = us_at_tick(s_cur[t].tick);
        int64_t due = s_wall0 + (int64_t)(ev_us * 100 / s_speed);
        int64_t now = now_us();
        int64_t wait = due - now;

        if (wait > 2000) {
            TickType_t ticks = pdMS_TO_TICKS((uint32_t)(wait / 1000) - 1);
            if (ticks == 0) {
                ticks = 1;
            }
            if (xQueueReceive(s_q, &cmd, ticks) == pdTRUE) {
                handle(&cmd);
            }
            continue;
        }
        if (wait > 0) {
            esp_rom_delay_us((uint32_t)wait);
        } else if (wait < -250000) {
            /* fell behind (e.g. long SD access): slip instead of bursting */
            portENTER_CRITICAL(&s_mux);
            s_wall0 += -wait;
            portEXIT_CRITICAL(&s_mux);
        }
        if (xQueueReceive(s_q, &cmd, 0) == pdTRUE) {
            handle(&cmd);
            continue;
        }
        advance_to(s_cur[t].tick);
        do_event(t, false);
    }
}

/* ------------------------------------------------------------------ API */

static void send(cmd_type_t type, int32_t a, bool flag, const char *path)
{
    cmd_t c = { .type = type, .a = a, .flag = flag };
    if (path) {
        snprintf(c.path, sizeof(c.path), "%s", path);
    }
    xQueueSend(s_q, &c, pdMS_TO_TICKS(100));
}

void player_init(void)
{
    s_q = xQueueCreate(8, sizeof(cmd_t));
    s_lock = xSemaphoreCreateMutex();
    s_bank = g_settings.bank;
    xTaskCreatePinnedToCore(player_task, "player", 6144, NULL, 15, NULL, 1);
}

void player_load(const char *path, uint32_t start_ms, bool autoplay)
{
    set_state(PLAYER_LOADING);   /* so the UI shows Loading at once */
    send(CMD_LOAD, (int32_t)start_ms, autoplay, path);
}
void player_play(void) { send(CMD_PLAY, 0, false, NULL); }
void player_pause(void) { send(CMD_PAUSE, 0, false, NULL); }
void player_toggle(void) { send(CMD_TOGGLE, 0, false, NULL); }
void player_stop(void) { send(CMD_STOP, 0, false, NULL); }
void player_unload(void) { send(CMD_UNLOAD, 0, false, NULL); }
void player_seek_rel(int32_t ms) { send(CMD_SEEK_REL, ms, false, NULL); }
void player_seek_abs(uint32_t ms) { send(CMD_SEEK_ABS, (int32_t)ms, false, NULL); }
void player_set_bank(uint8_t bank) { send(CMD_BANK, bank, false, NULL); }
void player_set_repeat(bool on) { s_repeat = on; }
void player_set_transpose(int8_t semi) { s_transpose = semi; }
void player_set_volume(uint8_t level) { midi_out_set_volume(level); }

void player_set_speed(uint16_t pct)
{
    if (pct < 25) {
        pct = 25;
    }
    if (pct > 200) {
        pct = 200;
    }
    send(CMD_SPEED, pct, false, NULL);
}

void player_get_status(player_status_t *st)
{
    portENTER_CRITICAL(&s_mux);
    st->state = s_state;
    st->load_id = s_load_id;
    st->error = s_error;
    int64_t wall0 = s_wall0;
    uint64_t pos = s_pos_us;
    portEXIT_CRITICAL(&s_mux);
    if (st->state == PLAYER_PLAYING) {
        int64_t d = now_us() - wall0;
        pos = d > 0 ? (uint64_t)d * s_speed / 100 : 0;
    }
    uint64_t total = s_smf.total_us;
    if (pos > total) {
        pos = total;
    }
    st->pos_ms = (uint32_t)(pos / 1000);
    st->len_ms = (uint32_t)(total / 1000);
    st->speed_pct = s_speed;
    st->transpose = s_transpose;
    st->bank = s_bank;
}

void player_lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
void player_unlock(void) { xSemaphoreGive(s_lock); }
const smf_t *player_song(void) { return &s_smf; }
uint8_t *player_part_flags(void) { return s_flags; }

void player_parts_changed(void)
{
    s_flags_gen++;
    send(CMD_WAKE, 0, false, NULL);
}

uint8_t player_part_level(int part)
{
    if (part < 0 || part >= SMF_MAX_PARTS) {
        return 0;
    }
    uint8_t v = s_part_level[part];
    s_part_level[part] = (uint8_t)(v * 7 / 8);
    return v;
}
