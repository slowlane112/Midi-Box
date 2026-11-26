/*
 * ln_song.c - built-in songs and MIDI files turned into one sorted note list for Learn.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smf.h"
#include "gm.h"
#include "sd.h"
#include "ln_builtin.h"
#include "ln_song.h"

ln_song_t g_ln;
static int s_cap;

void ln_song_free(void)
{
    free(g_ln.notes);
    free(g_ln.bars);
    memset(&g_ln, 0, sizeof(g_ln));
    g_ln.rh_part = g_ln.lh_part = LN_NONE;
    s_cap = 0;
}

static bool push(const ln_note_t *n)
{
    if (g_ln.n == s_cap) {
        int cap = s_cap ? s_cap * 2 : 1024;
        ln_note_t *p = realloc(g_ln.notes, (size_t)cap * sizeof(ln_note_t));
        if (!p) {
            return false;
        }
        g_ln.notes = p;
        s_cap = cap;
    }
    g_ln.notes[g_ln.n++] = *n;
    return true;
}

static int cmp_note(const void *a, const void *b)
{
    const ln_note_t *x = a, *y = b;
    if (x->t != y->t) {
        return x->t < y->t ? -1 : 1;
    }
    return (int)y->note - (int)x->note;
}

static void finish(void)
{
    qsort(g_ln.notes, (size_t)g_ln.n, sizeof(ln_note_t), cmp_note);
    g_ln.len_ms = 0;
    for (int i = 0; i < g_ln.n; i++) {
        const ln_note_t *n = &g_ln.notes[i];
        ln_part_t *p = &g_ln.parts[n->part];
        if (p->count == 0 || n->note < p->lo) {
            p->lo = n->note;
        }
        if (p->count == 0 || n->note > p->hi) {
            p->hi = n->note;
        }
        p->count++;
        if (n->t + n->len > g_ln.len_ms) {
            g_ln.len_ms = n->t + n->len;
        }
    }
}

static bool make_bars(uint32_t bar_ms_first, const uint32_t *given, int ngiven)
{
    int nb = given ? ngiven : (int)(g_ln.len_ms / (bar_ms_first ? bar_ms_first : 2000)) + 2;
    g_ln.bars = malloc((size_t)nb * sizeof(uint32_t));
    if (!g_ln.bars) {
        return false;
    }
    for (int i = 0; i < nb; i++) {
        g_ln.bars[i] = given ? given[i] : (uint32_t)i * bar_ms_first;
    }
    g_ln.nbars = nb;
    return true;
}

bool ln_song_custom(const char *title, const char *key, const ln_note_t *notes, int n, uint32_t beat_ms,
                    int beats_per_bar)
{
    ln_song_free();
    snprintf(g_ln.title, sizeof(g_ln.title), "%s", title);
    snprintf(g_ln.key, sizeof(g_ln.key), "%s", key);
    g_ln.nparts = 2;
    snprintf(g_ln.parts[0].name, LN_NAME_LEN, "Right hand");
    snprintf(g_ln.parts[1].name, LN_NAME_LEN, "Left hand");
    g_ln.parts[0].ch = 0;
    g_ln.parts[1].ch = 1;
    for (int i = 0; i < n; i++) {
        ln_note_t x = notes[i];
        x.part = x.hand == LN_LH ? 1 : 0;
        x.ch = x.part;
        if (!push(&x)) {
            ln_song_free();
            return false;
        }
    }
    finish();
    g_ln.beat_ms = beat_ms;
    g_ln.beats_per_bar = (uint8_t)beats_per_bar;
    if (!make_bars(beat_ms * (uint32_t)beats_per_bar, NULL, 0)) {
        ln_song_free();
        return false;
    }
    ln_song_assign(0, 1);
    return true;
}

bool ln_song_builtin(int index)
{
    ln_song_free();
    if (index < 0 || index >= ln_builtin_count) {
        return false;
    }
    const ln_builtin_t *b = &ln_builtin[index];
    snprintf(g_ln.title, sizeof(g_ln.title), "%s", b->title);
    snprintf(g_ln.key, sizeof(g_ln.key), "b:%s", b->id);
    g_ln.builtin = true;
    g_ln.level = b->level;
    g_ln.nparts = 2;
    snprintf(g_ln.parts[0].name, LN_NAME_LEN, "Right hand");
    snprintf(g_ln.parts[1].name, LN_NAME_LEN, "Left hand");
    g_ln.parts[0].ch = 0;
    g_ln.parts[1].ch = 1;
    /* ms = ticks * 60000 / (bpm * PPQ) */
    uint32_t div = (uint32_t)b->bpm * LN_BUILTIN_PPQ;
    for (int i = 0; i < b->n; i++) {
        const ln_bnote_t *s = &b->notes[i];
        ln_note_t n = {
            .t = (uint32_t)((uint64_t)s->t * 60000 / div),
            .len = (uint16_t)((uint64_t)s->len * 60000 / div),
            .note = s->note, .vel = s->vel, .ch = s->hand, .part = s->hand,
        };
        if (!push(&n)) {
            ln_song_free();
            return false;
        }
    }
    finish();
    g_ln.beats_per_bar = b->num;
    g_ln.beat_ms = (uint32_t)(60000UL * 4 / b->den / b->bpm);
    /* bar starts, plus the end of the last bar */
    g_ln.bars = malloc((size_t)(b->nbars + 1) * sizeof(uint32_t));
    if (!g_ln.bars) {
        ln_song_free();
        return false;
    }
    for (int i = 0; i <= b->nbars; i++) {
        g_ln.bars[i] = (uint32_t)((uint64_t)i * b->bar_ticks * 60000 / div);
    }
    g_ln.nbars = b->nbars + 1;
    g_ln.rh_part = 0;
    g_ln.lh_part = 1;
    ln_song_assign(0, 1);
    return true;
}

/* ------------------------------------------------------------------ MIDI files */

typedef struct {
    uint32_t tick;
    uint8_t vel;
} pend_t;

int ln_song_file(const char *path)
{
    ln_song_free();
    static smf_t s;
    smf_err_t err = smf_load(&s, path);
    if (err != SMF_OK) {
        return (int)err;
    }
    snprintf(g_ln.key, sizeof(g_ln.key), "%s", path);
    snprintf(g_ln.title, sizeof(g_ln.title), "%s", s.title[0] ? s.title : path_name(path));
    char *dot = strrchr(g_ln.title, '.');
    if (dot && !s.title[0]) {
        *dot = 0;
    }

    /* our parts: one per SMF part, up to LN_MAX_PARTS - 1, the rest together as "Other" */
    int8_t map[SMF_MAX_PARTS];
    for (int i = 0; i < s.nparts; i++) {
        int k = i < LN_MAX_PARTS - 1 ? i : LN_MAX_PARTS - 1;
        map[i] = (int8_t)k;
        ln_part_t *p = &g_ln.parts[k];
        if (k == i) {
            const smf_part_t *sp = &s.parts[i];
            p->ch = sp->channel;
            p->program = sp->program == 0xFF ? 0 : sp->program;
            p->drums = sp->channel == 9;
            if (sp->name[0]) {
                snprintf(p->name, LN_NAME_LEN, "%s", sp->name);
            } else {
                snprintf(p->name, LN_NAME_LEN, "%s", p->drums ? "Drums" : gm_program_name(p->program));
            }
        } else {
            snprintf(p->name, LN_NAME_LEN, "Other parts");
        }
    }
    g_ln.nparts = s.nparts < LN_MAX_PARTS ? s.nparts : LN_MAX_PARTS;

    pend_t *pend = malloc(16 * 128 * sizeof(pend_t));
    if (!pend) {
        smf_free(&s);
        return 3;
    }
    uint8_t ts_num = 4, ts_den = 4;
    bool ok = true;
    for (int tr = 0; tr < s.ntracks && ok; tr++) {
        for (int i = 0; i < 16 * 128; i++) {
            pend[i].tick = UINT32_MAX;
        }
        const uint8_t *p = s.tracks[tr].data, *end = p + s.tracks[tr].len;
        uint32_t tick = 0;
        uint8_t status = 0;
        while (p < end && ok) {
            uint32_t delta;
            if (!smf_read_vlq(&p, end, &delta)) {
                break;
            }
            tick += delta;
            if (p >= end) {
                break;
            }
            uint8_t b = *p;
            if (b == 0xFF) {
                if (end - p < 2) {
                    break;
                }
                uint8_t type = p[1];
                p += 2;
                uint32_t len;
                if (!smf_read_vlq(&p, end, &len) || len > (uint32_t)(end - p)) {
                    break;
                }
                if (type == 0x58 && len >= 2 && tick == 0) {
                    ts_num = p[0] ? p[0] : 4;
                    ts_den = (uint8_t)(1u << (p[1] < 6 ? p[1] : 2));
                }
                if (type == 0x2F) {
                    break;
                }
                p += len;
                continue;
            }
            if (b == 0xF0 || b == 0xF7) {
                p++;
                uint32_t len;
                if (!smf_read_vlq(&p, end, &len) || len > (uint32_t)(end - p)) {
                    break;
                }
                p += len;
                continue;
            }
            if (b & 0x80) {
                status = b;
                p++;
            }
            if (!status) {
                break;
            }
            uint8_t dl = smf_data_len(status);
            if (end - p < dl) {
                break;
            }
            uint8_t ch = status & 0x0F, type = status & 0xF0;
            if (type == 0x90 || type == 0x80) {
                uint8_t note = p[0] & 0x7F, vel = p[1] & 0x7F;
                pend_t *pe = &pend[ch * 128 + note];
                if (pe->tick != UINT32_MAX) {   /* end the sounding note (also on a repeated note-on) */
                    int sp = smf_part_index(&s, (uint16_t)tr, ch);
                    if (sp >= 0) {
                        uint64_t t0 = smf_tick_to_us(&s, pe->tick), t1 = smf_tick_to_us(&s, tick);
                        uint32_t len = (uint32_t)((t1 - t0) / 1000);
                        ln_note_t n = { .t = (uint32_t)(t0 / 1000), .len = (uint16_t)(len > 60000 ? 60000 : (len ? len : 1)),
                                        .note = note, .vel = pe->vel, .ch = ch, .part = (uint8_t)map[sp] };
                        ok = push(&n);
                    }
                    pe->tick = UINT32_MAX;
                }
                if (type == 0x90 && vel) {
                    pe->tick = tick;
                    pe->vel = vel;
                }
            }
            p += dl;
        }
    }
    free(pend);
    uint32_t bar_ticks = s.ppq ? (uint32_t)s.ppq * 4 * ts_num / ts_den : 0;
    uint32_t first_qn_us = s.ntempos ? s.tempos[0].us_per_qn : 500000;
    if (ok && g_ln.n == 0) {
        smf_free(&s);
        ln_song_free();
        return 4;
    }
    if (!ok) {
        smf_free(&s);
        ln_song_free();
        return 3;
    }
    finish();
    g_ln.beats_per_bar = ts_num;
    g_ln.beat_ms = (uint32_t)((uint64_t)first_qn_us * 4 / ts_den / 1000);
    /* bars follow the tempo map */
    int nb = 0;
    uint32_t *bars = NULL;
    if (bar_ticks) {
        nb = (int)(s.total_ticks / bar_ticks) + 2;
        bars = malloc((size_t)nb * sizeof(uint32_t));
        if (bars) {
            for (int i = 0; i < nb; i++) {
                bars[i] = (uint32_t)(smf_tick_to_us(&s, (uint32_t)i * bar_ticks) / 1000);
            }
        }
    }
    bool bars_ok = bars ? make_bars(0, bars, nb) : make_bars(2000, NULL, 0);
    free(bars);
    smf_free(&s);
    if (!bars_ok) {
        ln_song_free();
        return 3;
    }
    int rh, lh;
    ln_song_guess_hands(&rh, &lh);
    ln_song_assign(rh, lh);
    return 0;
}

/* ------------------------------------------------------------------ hands */

static bool pianoish(const ln_part_t *p)
{
    return !p->drums && p->count && p->program < 8;   /* GM 1-8: pianos */
}

void ln_song_guess_hands(int *rh, int *lh)
{
    if (g_ln.builtin) {
        *rh = 0;
        *lh = 1;
        return;
    }
    /* prefer piano parts; among them the highest average is the right hand */
    int best[2] = { LN_NONE, LN_NONE };
    for (int pass = 0; pass < 2 && best[0] == LN_NONE; pass++) {
        for (int i = 0; i < g_ln.nparts; i++) {
            const ln_part_t *p = &g_ln.parts[i];
            if (p->drums || !p->count || (pass == 0 && !pianoish(p))) {
                continue;
            }
            if (best[0] == LN_NONE || (p->lo + p->hi) > (g_ln.parts[best[0]].lo + g_ln.parts[best[0]].hi)) {
                best[1] = best[0];
                best[0] = i;
            } else if (best[1] == LN_NONE || (p->lo + p->hi) > (g_ln.parts[best[1]].lo + g_ln.parts[best[1]].hi)) {
                best[1] = i;
            }
        }
    }
    *rh = best[0];
    if (best[1] != LN_NONE && g_ln.parts[best[1]].hi <= g_ln.parts[best[0]].hi &&
        (pianoish(&g_ln.parts[best[1]]) || !pianoish(&g_ln.parts[best[0]]))) {
        *lh = best[1];
    } else {
        /* one part with a wide range: both hands are in it */
        const ln_part_t *p = best[0] >= 0 ? &g_ln.parts[best[0]] : NULL;
        *lh = (p && p->lo < 60 && p->hi >= 60) ? LN_SPLIT : LN_NONE;
    }
}

void ln_song_assign(int rh, int lh)
{
    g_ln.rh_part = rh;
    g_ln.lh_part = lh;
    for (int i = 0; i < g_ln.n; i++) {
        ln_note_t *n = &g_ln.notes[i];
        if (g_ln.parts[n->part].drums) {
            n->hand = LN_BACKING;
        } else if (n->part == rh) {
            n->hand = (lh == LN_SPLIT && n->note < 60) ? LN_LH : LN_RH;
        } else if (n->part == lh) {
            n->hand = LN_LH;
        } else {
            n->hand = LN_BACKING;
        }
    }
}

bool ln_song_range(unsigned hands, uint8_t *lo, uint8_t *hi)
{
    bool any = false;
    for (int i = 0; i < g_ln.n; i++) {
        const ln_note_t *n = &g_ln.notes[i];
        if (n->hand < 2 && (hands & (1u << n->hand))) {
            if (!any || n->note < *lo) {
                *lo = n->note;
            }
            if (!any || n->note > *hi) {
                *hi = n->note;
            }
            any = true;
        }
    }
    return any;
}

int ln_song_bar_at(uint32_t t)
{
    int lo = 0, hi = g_ln.nbars - 1;
    if (hi < 0) {
        return 0;
    }
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (g_ln.bars[mid] <= t) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return lo;
}
