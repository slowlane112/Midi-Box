#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "sd.h"
#include "midi_out.h"
#include "song.h"

song_t g_song;
clip_t g_clip;

static SemaphoreHandle_t s_lock;

/* single-level undo: a copy of one track's events */
static struct {
    bool valid;
    int track;
    sev_t *ev;
    int n;
} s_undo;

/* ------------------------------------------------------------------ basics */

void song_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(&g_song, 0, sizeof(g_song));
}

void song_lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
void song_unlock(void) { xSemaphoreGive(s_lock); }

static void undo_clear(void)
{
    free(s_undo.ev);
    memset(&s_undo, 0, sizeof(s_undo));
}

void song_free(void)
{
    for (int t = 0; t < SONG_TRACKS; t++) {
        free(g_song.tr[t].ev);
    }
    memset(&g_song, 0, sizeof(g_song));
    undo_clear();
}

void song_new(const char *name)
{
    song_free();
    snprintf(g_song.name, sizeof(g_song.name), "%s", name);
    g_song.bpm = 120;
    g_song.beats = 4;
    g_song.bars = 8;
    song_track_add("Piano", 0, 0);
    g_song.dirty = false;
}

uint32_t song_end_tick(void)
{
    uint32_t end = 0;
    for (int t = 0; t < g_song.ntracks; t++) {
        const strack_t *tr = &g_song.tr[t];
        for (int i = 0; i < tr->n; i++) {
            uint32_t e = tr->ev[i].tick + tr->ev[i].len;
            if (e > end) {
                end = e;
            }
        }
    }
    return end;
}

void song_fit_length(void)
{
    uint32_t bt = song_bar_ticks();
    uint32_t bars = (song_end_tick() + bt - 1) / bt;
    if (bars > g_song.bars) {
        g_song.bars = (uint16_t)(bars > 999 ? 999 : bars);
    }
}

uint8_t song_free_channel(void)
{
    for (uint8_t ch = 0; ch < 16; ch++) {
        if (ch == 9) {
            continue;
        }
        bool used = false;
        for (int t = 0; t < g_song.ntracks; t++) {
            if (g_song.tr[t].channel == ch) {
                used = true;
            }
        }
        if (!used) {
            return ch;
        }
    }
    return 0;
}

int song_track_add(const char *name, uint8_t channel, uint8_t program)
{
    if (g_song.ntracks >= SONG_TRACKS) {
        return -1;
    }
    strack_t *tr = &g_song.tr[g_song.ntracks];
    memset(tr, 0, sizeof(*tr));
    snprintf(tr->name, sizeof(tr->name), "%s", name);
    tr->channel = channel & 0x0F;
    tr->program = program & 0x7F;
    tr->volume = 100;
    g_song.dirty = true;
    return g_song.ntracks++;
}

void song_track_delete(int t)
{
    if (t < 0 || t >= g_song.ntracks) {
        return;
    }
    free(g_song.tr[t].ev);
    memmove(&g_song.tr[t], &g_song.tr[t + 1], sizeof(strack_t) * (size_t)(g_song.ntracks - t - 1));
    g_song.ntracks--;
    memset(&g_song.tr[g_song.ntracks], 0, sizeof(strack_t));
    undo_clear();
    g_song.dirty = true;
}

bool song_track_sounds(int t)
{
    bool any_solo = false;
    for (int i = 0; i < g_song.ntracks; i++) {
        if (g_song.tr[i].flags & TRK_SOLO) {
            any_solo = true;
        }
    }
    uint8_t f = g_song.tr[t].flags;
    return any_solo ? (f & TRK_SOLO) != 0 : (f & TRK_MUTE) == 0;
}

/* ------------------------------------------------------------------ events */

static bool grow(strack_t *tr, int need)
{
    if (need <= tr->cap) {
        return true;
    }
    int cap = tr->cap ? tr->cap : 256;
    while (cap < need) {
        cap *= 2;
    }
    sev_t *n = heap_caps_realloc(tr->ev, sizeof(sev_t) * (size_t)cap, MALLOC_CAP_SPIRAM);
    if (!n) {
        return false;
    }
    tr->ev = n;
    tr->cap = cap;
    return true;
}

int ev_first_at(const strack_t *tr, uint32_t tick)
{
    int lo = 0, hi = tr->n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (tr->ev[mid].tick < tick) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

int ev_insert(strack_t *tr, const sev_t *e)
{
    if (!grow(tr, tr->n + 1)) {
        return -1;
    }
    int i = ev_first_at(tr, e->tick + 1);   /* after events at the same tick: keeps recording order */
    memmove(&tr->ev[i + 1], &tr->ev[i], sizeof(sev_t) * (size_t)(tr->n - i));
    tr->ev[i] = *e;
    tr->n++;
    return i;
}

void ev_remove(strack_t *tr, int i)
{
    if (i < 0 || i >= tr->n) {
        return;
    }
    memmove(&tr->ev[i], &tr->ev[i + 1], sizeof(sev_t) * (size_t)(tr->n - i - 1));
    tr->n--;
}

static int ev_cmp(const void *a, const void *b)
{
    const sev_t *x = a, *y = b;
    return (x->tick > y->tick) - (x->tick < y->tick);
}

static void resort(strack_t *tr)
{
    /* qsort is not stable; ties keep a sensible order because notes at one tick are unordered anyway */
    qsort(tr->ev, (size_t)tr->n, sizeof(sev_t), ev_cmp);
}

/* ------------------------------------------------------------------ undo */

void song_save_undo(int t)
{
    undo_clear();
    const strack_t *tr = &g_song.tr[t];
    s_undo.ev = tr->n ? heap_caps_malloc(sizeof(sev_t) * (size_t)tr->n, MALLOC_CAP_SPIRAM) : NULL;
    if (tr->n && !s_undo.ev) {
        return;
    }
    if (tr->n) {
        memcpy(s_undo.ev, tr->ev, sizeof(sev_t) * (size_t)tr->n);
    }
    s_undo.n = tr->n;
    s_undo.track = t;
    s_undo.valid = true;
}

bool song_can_undo(void) { return s_undo.valid && s_undo.track < g_song.ntracks; }

void song_undo(void)
{
    if (!song_can_undo()) {
        return;
    }
    strack_t *tr = &g_song.tr[s_undo.track];
    /* swap, so a second undo is a redo */
    sev_t *cur = tr->n ? heap_caps_malloc(sizeof(sev_t) * (size_t)tr->n, MALLOC_CAP_SPIRAM) : NULL;
    int cur_n = cur ? tr->n : 0;
    if (cur) {
        memcpy(cur, tr->ev, sizeof(sev_t) * (size_t)tr->n);
    }
    tr->n = 0;
    if (grow(tr, s_undo.n) && s_undo.n) {
        memcpy(tr->ev, s_undo.ev, sizeof(sev_t) * (size_t)s_undo.n);
        tr->n = s_undo.n;
    }
    free(s_undo.ev);
    s_undo.ev = cur;
    s_undo.n = cur_n;
    g_song.dirty = true;
}

/* ------------------------------------------------------------------ range edits */

static bool in_range(const sev_t *e, uint32_t t0, uint32_t t1) { return e->tick >= t0 && e->tick < t1; }

int edit_count(int t, uint32_t t0, uint32_t t1)
{
    const strack_t *tr = &g_song.tr[t];
    int n = 0;
    for (int i = ev_first_at(tr, t0); i < tr->n && tr->ev[i].tick < t1; i++) {
        n += tr->ev[i].type == EV_NOTE;
    }
    return n;
}

void edit_copy(int t, uint32_t t0, uint32_t t1)
{
    const strack_t *tr = &g_song.tr[t];
    free(g_clip.ev);
    memset(&g_clip, 0, sizeof(g_clip));
    int a = ev_first_at(tr, t0), b = ev_first_at(tr, t1);
    g_clip.len_ticks = t1 - t0;
    if (b <= a) {
        return;
    }
    g_clip.ev = heap_caps_malloc(sizeof(sev_t) * (size_t)(b - a), MALLOC_CAP_SPIRAM);
    if (!g_clip.ev) {
        return;
    }
    for (int i = a; i < b; i++) {
        sev_t e = tr->ev[i];
        e.tick -= t0;
        e.round = 0;
        g_clip.ev[g_clip.n++] = e;
    }
}

static void delete_range(strack_t *tr, uint32_t t0, uint32_t t1)
{
    int a = ev_first_at(tr, t0), b = ev_first_at(tr, t1);
    if (b > a) {
        memmove(&tr->ev[a], &tr->ev[b], sizeof(sev_t) * (size_t)(tr->n - b));
        tr->n -= b - a;
    }
}

void edit_delete(int t, uint32_t t0, uint32_t t1)
{
    song_save_undo(t);
    delete_range(&g_song.tr[t], t0, t1);
    g_song.dirty = true;
}

void edit_cut(int t, uint32_t t0, uint32_t t1)
{
    edit_copy(t, t0, t1);
    edit_delete(t, t0, t1);
}

static void paste_at(strack_t *tr, uint32_t at)
{
    if (!grow(tr, tr->n + g_clip.n)) {
        return;
    }
    for (int i = 0; i < g_clip.n; i++) {
        sev_t e = g_clip.ev[i];
        e.tick += at;
        tr->ev[tr->n++] = e;
    }
    resort(tr);
}

void edit_paste(int t, uint32_t at)
{
    if (g_clip.n == 0) {
        return;
    }
    song_save_undo(t);
    paste_at(&g_song.tr[t], at);
    song_fit_length();
    g_song.dirty = true;
}

void edit_duplicate(int t, uint32_t t0, uint32_t t1)
{
    /* keep the user's clipboard */
    clip_t saved = g_clip;
    memset(&g_clip, 0, sizeof(g_clip));
    edit_copy(t, t0, t1);
    song_save_undo(t);
    paste_at(&g_song.tr[t], t1);
    free(g_clip.ev);
    g_clip = saved;
    song_fit_length();
    g_song.dirty = true;
}

void edit_transpose(int t, uint32_t t0, uint32_t t1, int semis)
{
    song_save_undo(t);
    strack_t *tr = &g_song.tr[t];
    for (int i = 0; i < tr->n; i++) {
        sev_t *e = &tr->ev[i];
        if (e->type == EV_NOTE && in_range(e, t0, t1)) {
            int n = e->a + semis;
            e->a = (uint8_t)(n < 0 ? 0 : (n > 127 ? 127 : n));
        }
    }
    g_song.dirty = true;
}

void edit_velocity(int t, uint32_t t0, uint32_t t1, int percent)
{
    song_save_undo(t);
    strack_t *tr = &g_song.tr[t];
    for (int i = 0; i < tr->n; i++) {
        sev_t *e = &tr->ev[i];
        if (e->type == EV_NOTE && in_range(e, t0, t1)) {
            int v = e->b * percent / 100;
            e->b = (uint8_t)(v < 1 ? 1 : (v > 127 ? 127 : v));
        }
    }
    g_song.dirty = true;
}

/* where q would put a note at 'tick' */
static uint32_t quant_tick(uint32_t tick, const quant_t *q)
{
    uint32_t g = q->grid ? q->grid : 1;
    uint32_t step = (tick + g / 2) / g;
    uint32_t target = step * g;
    if (step & 1) {
        target += g * q->swing / 200;   /* swing pushes every second step later */
    }
    int32_t d = (int32_t)target - (int32_t)tick;
    return (uint32_t)((int32_t)tick + d * q->strength / 100);
}

static uint32_t quant_len(uint32_t len, const quant_t *q)
{
    uint32_t g = q->grid ? q->grid : 1;
    uint32_t l = (len + g / 2) / g * g;
    l = l ? l : g;
    int32_t d = (int32_t)l - (int32_t)len;
    return (uint32_t)((int32_t)len + d * q->strength / 100);
}

uint32_t edit_quantise_preview(int t, uint32_t t0, uint32_t t1, const quant_t *q, int *moved)
{
    const strack_t *tr = &g_song.tr[t];
    uint32_t worst = 0;
    int n = 0;
    for (int i = ev_first_at(tr, t0); i < tr->n && tr->ev[i].tick < t1; i++) {
        const sev_t *e = &tr->ev[i];
        if (e->type != EV_NOTE) {
            continue;
        }
        uint32_t nt = quant_tick(e->tick, q);
        uint32_t d = nt > e->tick ? nt - e->tick : e->tick - nt;
        if (d) {
            n++;
        }
        if (d > worst) {
            worst = d;
        }
    }
    if (moved) {
        *moved = n;
    }
    return worst;
}

uint32_t edit_quantise(int t, uint32_t t0, uint32_t t1, const quant_t *q)
{
    uint32_t worst = edit_quantise_preview(t, t0, t1, q, NULL);
    song_save_undo(t);
    strack_t *tr = &g_song.tr[t];
    for (int i = 0; i < tr->n; i++) {
        sev_t *e = &tr->ev[i];
        if (e->type != EV_NOTE || !in_range(e, t0, t1)) {
            continue;
        }
        e->tick = quant_tick(e->tick, q);
        if (q->ends) {
            e->len = quant_len(e->len, q);
        }
    }
    resort(tr);
    g_song.dirty = true;
    return worst;
}

/* ------------------------------------------------------------------ byte buffer */

typedef struct {
    uint8_t *p;
    size_t n, cap;
    bool ok;
} buf_t;

static void put(buf_t *b, const void *d, size_t n)
{
    if (!b->ok || n == 0) {
        return;
    }
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 4096;
        while (cap < b->n + n) {
            cap *= 2;
        }
        uint8_t *p = heap_caps_realloc(b->p, cap, MALLOC_CAP_SPIRAM);
        if (!p) {
            b->ok = false;
            return;
        }
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}

static void put8(buf_t *b, uint8_t v) { put(b, &v, 1); }
static void put16(buf_t *b, uint16_t v) { put8(b, (uint8_t)(v >> 8)); put8(b, (uint8_t)v); }
static void put32(buf_t *b, uint32_t v) { put16(b, (uint16_t)(v >> 16)); put16(b, (uint16_t)v); }

static void putvlq(buf_t *b, uint32_t v)
{
    uint8_t t[5];
    int n = 0;
    t[n++] = v & 0x7F;
    while (v >>= 7) {
        t[n++] = (uint8_t)(0x80 | (v & 0x7F));
    }
    while (n) {
        put8(b, t[--n]);
    }
}

/* ------------------------------------------------------------------ .mbp project */

#define MBP_MAGIC "MBP1"
#define MBP_VERSION 1

bool song_save(const char *path)
{
    buf_t b = { .ok = true };
    put(&b, MBP_MAGIC, 4);
    put16(&b, MBP_VERSION);
    put16(&b, g_song.bpm);
    put8(&b, g_song.beats);
    put8(&b, g_song.bank);
    put16(&b, g_song.bars);
    put8(&b, (uint8_t)g_song.ntracks);
    put(&b, g_song.name, SONG_NAME_LEN);
    for (int t = 0; t < g_song.ntracks; t++) {
        const strack_t *tr = &g_song.tr[t];
        put(&b, tr->name, TRACK_NAME_LEN);
        put8(&b, tr->channel);
        put8(&b, tr->program);
        put8(&b, tr->volume);
        put8(&b, tr->flags);
        put32(&b, (uint32_t)tr->n);
        for (int i = 0; i < tr->n; i++) {
            const sev_t *e = &tr->ev[i];
            put32(&b, e->tick);
            put32(&b, e->len);
            put8(&b, e->type);
            put8(&b, e->a);
            put8(&b, e->b);
        }
    }
    bool ok = b.ok && fs_write_all(path, b.p, b.n);
    free(b.p);
    if (ok) {
        g_song.dirty = false;
    }
    return ok;
}

typedef struct {
    const uint8_t *p, *end;
    bool ok;
} rd_t;

static uint8_t get8(rd_t *r) { if (r->p + 1 > r->end) { r->ok = false; return 0; } return *r->p++; }
static uint16_t get16(rd_t *r) { uint16_t a = get8(r); return (uint16_t)((a << 8) | get8(r)); }
static uint32_t get32(rd_t *r) { uint32_t a = get16(r); return (a << 16) | get16(r); }
static void getn(rd_t *r, void *d, size_t n)
{
    if (r->p + n > r->end) {
        r->ok = false;
        memset(d, 0, n);
        return;
    }
    memcpy(d, r->p, n);
    r->p += n;
}

bool song_load(const char *path)
{
    size_t len;
    uint8_t *data = fs_read_all(path, &len);
    if (!data) {
        return false;
    }
    rd_t r = { data, data + len, true };
    char magic[4];
    getn(&r, magic, 4);
    if (!r.ok || memcmp(magic, MBP_MAGIC, 4) != 0 || get16(&r) != MBP_VERSION) {
        free(data);
        return false;
    }
    song_free();
    g_song.bpm = get16(&r);
    g_song.beats = get8(&r);
    g_song.bank = get8(&r);
    g_song.bars = get16(&r);
    int nt = get8(&r);
    getn(&r, g_song.name, SONG_NAME_LEN);
    g_song.name[SONG_NAME_LEN - 1] = '\0';
    if (g_song.bpm < 20 || g_song.bpm > 300) {
        g_song.bpm = 120;
    }
    if (g_song.beats < 1 || g_song.beats > 12) {
        g_song.beats = 4;
    }
    for (int t = 0; t < nt && t < SONG_TRACKS && r.ok; t++) {
        strack_t *tr = &g_song.tr[t];
        getn(&r, tr->name, TRACK_NAME_LEN);
        tr->name[TRACK_NAME_LEN - 1] = '\0';
        tr->channel = get8(&r) & 0x0F;
        tr->program = get8(&r) & 0x7F;
        tr->volume = get8(&r) & 0x7F;
        tr->flags = get8(&r);
        uint32_t n = get32(&r);
        if (n > (uint32_t)((r.end - r.p) / 11)) {
            r.ok = false;
            break;
        }
        if (n && !grow(tr, (int)n)) {
            r.ok = false;
            break;
        }
        for (uint32_t i = 0; i < n; i++) {
            sev_t e = { 0 };
            e.tick = get32(&r);
            e.len = get32(&r);
            e.type = get8(&r);
            e.a = get8(&r) & 0x7F;
            e.b = get8(&r) & 0x7F;
            tr->ev[tr->n++] = e;
        }
        resort(tr);
        g_song.ntracks = t + 1;
    }
    free(data);
    if (!r.ok) {
        song_free();
        return false;
    }
    if (g_song.ntracks == 0) {
        song_track_add("Piano", 0, 0);
    }
    song_fit_length();
    g_song.dirty = false;
    return true;
}

/* ------------------------------------------------------------------ .mid export */

typedef struct {
    uint32_t tick;
    uint8_t pri;      /* note-offs before note-ons at the same tick */
    uint8_t m[3];
    uint8_t len;
} mev_t;

static int mev_cmp(const void *a, const void *b)
{
    const mev_t *x = a, *y = b;
    if (x->tick != y->tick) {
        return x->tick < y->tick ? -1 : 1;
    }
    return x->pri - y->pri;
}

static void meta(buf_t *b, uint32_t delta, uint8_t type, const void *d, uint8_t n)
{
    putvlq(b, delta);
    put8(b, 0xFF);
    put8(b, type);
    putvlq(b, n);
    put(b, d, n);
}

static void end_chunk(buf_t *b, size_t len_pos)
{
    uint32_t len = (uint32_t)(b->n - len_pos - 4);
    if (b->ok) {
        b->p[len_pos] = (uint8_t)(len >> 24);
        b->p[len_pos + 1] = (uint8_t)(len >> 16);
        b->p[len_pos + 2] = (uint8_t)(len >> 8);
        b->p[len_pos + 3] = (uint8_t)len;
    }
}

bool song_export_mid(const char *path)
{
    buf_t b = { .ok = true };
    uint32_t end = song_end_tick();
    uint32_t song_end = (uint32_t)g_song.bars * song_bar_ticks();
    if (end < song_end) {
        end = song_end;
    }

    put(&b, "MThd", 4);
    put32(&b, 6);
    put16(&b, 1);
    put16(&b, (uint16_t)(g_song.ntracks + 1));
    put16(&b, SONG_PPQ);

    /* conductor track: name, tempo, time signature */
    put(&b, "MTrk", 4);
    size_t lp = b.n;
    put32(&b, 0);
    meta(&b, 0, 0x03, g_song.name, (uint8_t)strlen(g_song.name));
    uint32_t us = 60000000u / (g_song.bpm ? g_song.bpm : 120);
    uint8_t tempo[3] = { (uint8_t)(us >> 16), (uint8_t)(us >> 8), (uint8_t)us };
    meta(&b, 0, 0x51, tempo, 3);
    uint8_t ts[4] = { g_song.beats, 2, 24, 8 };
    meta(&b, 0, 0x58, ts, 4);
    meta(&b, end, 0x2F, NULL, 0);
    end_chunk(&b, lp);

    for (int t = 0; t < g_song.ntracks && b.ok; t++) {
        const strack_t *tr = &g_song.tr[t];
        uint8_t ch = tr->channel;
        put(&b, "MTrk", 4);
        lp = b.n;
        put32(&b, 0);
        meta(&b, 0, 0x03, tr->name, (uint8_t)strlen(tr->name));
        uint8_t setup[] = {
            (uint8_t)(0xB0 | ch), 0, (uint8_t)((g_song.bank == BANK_MT32 && ch != 9) ? 127 : 0),
            (uint8_t)(0xC0 | ch), tr->program,
            (uint8_t)(0xB0 | ch), 7, tr->volume,
        };
        put8(&b, 0); put(&b, setup, 3);
        put8(&b, 0); put(&b, setup + 3, 2);
        put8(&b, 0); put(&b, setup + 5, 3);

        int cnt = 0;
        for (int i = 0; i < tr->n; i++) {
            cnt += tr->ev[i].type == EV_NOTE ? 2 : 1;
        }
        mev_t *m = cnt ? heap_caps_malloc(sizeof(mev_t) * (size_t)cnt, MALLOC_CAP_SPIRAM) : NULL;
        if (cnt && !m) {
            b.ok = false;
            break;
        }
        int k = 0;
        for (int i = 0; i < tr->n; i++) {
            const sev_t *e = &tr->ev[i];
            if (e->type == EV_NOTE) {
                m[k++] = (mev_t){ e->tick, 1, { (uint8_t)(0x90 | ch), e->a, e->b ? e->b : 1 }, 3 };
                m[k++] = (mev_t){ e->tick + (e->len ? e->len : 1), 0, { (uint8_t)(0x80 | ch), e->a, 0 }, 3 };
            } else {
                m[k++] = (mev_t){ e->tick, 1, { (uint8_t)(0xB0 | ch), e->a, e->b }, 3 };
            }
        }
        qsort(m, (size_t)k, sizeof(mev_t), mev_cmp);
        uint32_t last = 0;
        for (int i = 0; i < k; i++) {
            putvlq(&b, m[i].tick - last);
            put(&b, m[i].m, m[i].len);
            last = m[i].tick;
        }
        free(m);
        meta(&b, end > last ? end - last : 0, 0x2F, NULL, 0);
        end_chunk(&b, lp);
    }
    bool ok = b.ok && fs_write_all(path, b.p, b.n);
    free(b.p);
    return ok;
}
