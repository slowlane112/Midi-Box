#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sd.h"
#include "gm.h"
#include "smf.h"

static const char *TAG = "smf";

#define MAX_TEMPOS 2048

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

bool smf_read_vlq(const uint8_t **p, const uint8_t *end, uint32_t *out)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        if (*p >= end) {
            return false;
        }
        uint8_t b = *(*p)++;
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) {
            *out = v;
            return true;
        }
    }
    return false;
}

uint8_t smf_data_len(uint8_t status)
{
    switch (status & 0xF0) {
    case 0xC0: case 0xD0: return 1;
    default: return 2;
    }
}

static int tempo_cmp(const void *a, const void *b)
{
    const smf_tempo_t *x = a, *y = b;
    return (x->tick > y->tick) - (x->tick < y->tick);
}

static void copy_name(char *dst, size_t n, const uint8_t *src, uint32_t len)
{
    size_t k = 0;
    for (uint32_t i = 0; i < len && k + 1 < n; i++) {
        uint8_t c = src[i];
        if (c >= 32 && c < 127) {
            dst[k++] = (char)c;
        }
    }
    while (k > 0 && dst[k - 1] == ' ') {
        k--;
    }
    dst[k] = '\0';
}

/* Scan one track: tempo events, name, notes per channel, first program per channel. */
typedef struct {
    uint32_t notes[16];
    uint8_t program[16];
    char name[SMF_NAME_LEN];
} track_info_t;

static void scan_track(smf_t *s, uint16_t t, track_info_t *ti, smf_tempo_t *tempos, int *ntempos)
{
    const uint8_t *p = s->tracks[t].data;
    const uint8_t *end = p + s->tracks[t].len;
    uint32_t tick = 0;
    uint8_t rs = 0;
    memset(ti, 0, sizeof(*ti));
    memset(ti->program, 0xFF, sizeof(ti->program));

    while (p < end) {
        uint32_t delta;
        if (!smf_read_vlq(&p, end, &delta)) {
            break;
        }
        tick += delta;
        if (p >= end) {
            break;
        }
        uint8_t st = *p;
        if (st == 0xFF) {                       /* meta */
            if (p + 2 > end) {
                break;
            }
            uint8_t type = p[1];
            p += 2;
            uint32_t len;
            if (!smf_read_vlq(&p, end, &len) || p + len > end) {
                break;
            }
            if (type == 0x51 && len == 3 && *ntempos < MAX_TEMPOS) {
                uint32_t us = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
                if (us > 0) {
                    tempos[*ntempos].tick = tick;
                    tempos[*ntempos].us_per_qn = us;
                    (*ntempos)++;
                }
            } else if (type == 0x03 && ti->name[0] == '\0') {
                copy_name(ti->name, sizeof(ti->name), p, len);
            } else if (type == 0x2F) {
                p += len;
                break;
            }
            p += len;
            continue;
        }
        if (st == 0xF0 || st == 0xF7) {         /* sysex */
            p++;
            uint32_t len;
            if (!smf_read_vlq(&p, end, &len) || p + len > end) {
                break;
            }
            p += len;
            continue;
        }
        if (st & 0x80) {
            rs = st;
            p++;
        } else if (!rs) {
            ESP_LOGW(TAG, "track %u: data byte without status", t);
            break;
        }
        uint8_t n = smf_data_len(rs);
        if (p + n > end) {
            break;
        }
        uint8_t ch = rs & 0x0F;
        uint8_t type = rs & 0xF0;
        if (type == 0x90 && p[1] > 0) {
            ti->notes[ch]++;
        } else if (type == 0xC0 && ti->program[ch] == 0xFF) {
            ti->program[ch] = p[0] & 0x7F;
        }
        p += n;
    }
    if (tick > s->total_ticks) {
        s->total_ticks = tick;
    }
}

static void build_tempo_map(smf_t *s, smf_tempo_t *raw, int n)
{
    qsort(raw, (size_t)n, sizeof(smf_tempo_t), tempo_cmp);
    int count = 0;
    s->tempos = heap_caps_malloc(sizeof(smf_tempo_t) * (size_t)(n + 1), MALLOC_CAP_SPIRAM);
    if (!s->tempos) {
        return;
    }
    if (n == 0 || raw[0].tick != 0) {
        s->tempos[count++] = (smf_tempo_t){ .tick = 0, .us_per_qn = 500000, .us = 0 };
    }
    for (int i = 0; i < n; i++) {
        if (count > 0 && s->tempos[count - 1].tick == raw[i].tick) {
            s->tempos[count - 1].us_per_qn = raw[i].us_per_qn;   /* later one wins */
            continue;
        }
        s->tempos[count++] = raw[i];
    }
    s->tempos[0].us = 0;
    for (int i = 1; i < count; i++) {
        const smf_tempo_t *a = &s->tempos[i - 1];
        s->tempos[i].us = a->us + (uint64_t)(s->tempos[i].tick - a->tick) * a->us_per_qn / s->ppq;
    }
    s->ntempos = count;
}

uint64_t smf_tick_to_us(const smf_t *s, uint32_t tick)
{
    if (s->ppq == 0) {
        return (uint64_t)tick * s->smpte_us_per_tick;
    }
    if (s->ntempos == 0) {
        return (uint64_t)tick * 500000 / s->ppq;
    }
    int lo = 0, hi = s->ntempos - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (s->tempos[mid].tick <= tick) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    const smf_tempo_t *t = &s->tempos[lo];
    return t->us + (uint64_t)(tick - t->tick) * t->us_per_qn / s->ppq;
}

uint32_t smf_us_to_tick(const smf_t *s, uint64_t us)
{
    if (s->ppq == 0) {
        return s->smpte_us_per_tick ? (uint32_t)(us / s->smpte_us_per_tick) : 0;
    }
    if (s->ntempos == 0) {
        return (uint32_t)(us * s->ppq / 500000);
    }
    int i = s->ntempos - 1;
    while (i > 0 && s->tempos[i].us > us) {
        i--;
    }
    const smf_tempo_t *t = &s->tempos[i];
    return t->tick + (uint32_t)((us - t->us) * s->ppq / t->us_per_qn);
}

int smf_part_index(const smf_t *s, uint16_t track, uint8_t ch)
{
    if (!s->part_of || track >= s->ntracks) {
        return -1;
    }
    return s->part_of[track * 16 + (ch & 0x0F)];
}

void smf_free(smf_t *s)
{
    free(s->buf);
    free(s->tracks);
    free(s->tempos);
    free(s->part_of);
    memset(s, 0, sizeof(*s));
}

smf_err_t smf_load(smf_t *s, const char *path)
{
    memset(s, 0, sizeof(*s));
    s->buf = fs_read_all(path, &s->size);
    if (!s->buf) {
        return SMF_ERR_READ;
    }
    const uint8_t *p = s->buf, *end = s->buf + s->size;

    /* RIFF-wrapped MIDI (.rmi): skip to the MThd chunk */
    if (s->size > 20 && memcmp(p, "RIFF", 4) == 0) {
        for (const uint8_t *q = p; q + 14 < end; q++) {
            if (memcmp(q, "MThd", 4) == 0) {
                p = q;
                break;
            }
        }
    }
    if (end - p < 14 || memcmp(p, "MThd", 4) != 0 || be32(p + 4) < 6) {
        smf_free(s);
        return SMF_ERR_FORMAT;
    }
    s->format = be16(p + 8);
    uint16_t declared = be16(p + 10);
    uint16_t div = be16(p + 12);
    if (div & 0x8000) {
        int fps = -(int8_t)(div >> 8);
        int tpf = div & 0xFF;
        if (fps <= 0 || tpf <= 0) {
            smf_free(s);
            return SMF_ERR_FORMAT;
        }
        s->ppq = 0;
        s->smpte_us_per_tick = 1000000u / (uint32_t)(fps * tpf);
    } else {
        s->ppq = div ? div : 96;
    }
    if (s->format > 1) {
        ESP_LOGW(TAG, "format %u: playing tracks together", s->format);
    }
    p += 8 + be32(p + 4);

    s->tracks = calloc(declared ? declared : 1, sizeof(smf_track_t));
    if (!s->tracks) {
        smf_free(s);
        return SMF_ERR_MEMORY;
    }
    /* walk chunks; ignore unknown chunk types. A file cut short (a chunk longer than
     * what is left, or fewer tracks than the header says) is refused as incomplete */
    while (p + 8 <= end && s->ntracks < declared) {
        uint32_t len = be32(p + 4);
        bool is_trk = memcmp(p, "MTrk", 4) == 0;
        p += 8;
        if (len > (uint32_t)(end - p)) {
            smf_free(s);
            return SMF_ERR_INCOMPLETE;
        }
        if (is_trk) {
            s->tracks[s->ntracks].data = p;
            s->tracks[s->ntracks].len = len;
            s->ntracks++;
        }
        p += len;
    }
    if (s->ntracks == 0 && p >= end) {
        smf_free(s);
        return declared ? SMF_ERR_INCOMPLETE : SMF_ERR_FORMAT;
    }
    if (s->ntracks == 0) {
        smf_free(s);
        return SMF_ERR_FORMAT;
    }
    if (s->ntracks < declared) {
        smf_free(s);
        return SMF_ERR_INCOMPLETE;
    }

    smf_tempo_t *raw = heap_caps_malloc(sizeof(smf_tempo_t) * MAX_TEMPOS, MALLOC_CAP_SPIRAM);
    s->part_of = malloc(sizeof(int16_t) * s->ntracks * 16);
    if (!raw || !s->part_of) {
        free(raw);
        smf_free(s);
        return SMF_ERR_MEMORY;
    }
    for (int i = 0; i < s->ntracks * 16; i++) {
        s->part_of[i] = -1;
    }

    int nraw = 0;
    track_info_t ti;
    for (uint16_t t = 0; t < s->ntracks; t++) {
        scan_track(s, t, &ti, raw, &nraw);
        if (t == 0 && ti.name[0]) {
            snprintf(s->title, sizeof(s->title), "%s", ti.name);
        }
        for (uint8_t ch = 0; ch < 16; ch++) {
            if (ti.notes[ch] == 0 || s->nparts >= SMF_MAX_PARTS) {
                continue;
            }
            smf_part_t *pt = &s->parts[s->nparts];
            pt->track = t;
            pt->channel = ch;
            pt->program = ti.program[ch];
            pt->notes = ti.notes[ch];
            if (ti.name[0] && !(s->format == 0)) {
                snprintf(pt->name, sizeof(pt->name), "%s", ti.name);
            } else if (ch == 9) {
                snprintf(pt->name, sizeof(pt->name), "Drums");
            } else {
                snprintf(pt->name, sizeof(pt->name), "%s", gm_program_name(pt->program == 0xFF ? 0 : pt->program));
            }
            s->part_of[t * 16 + ch] = (int16_t)s->nparts;
            s->nparts++;
        }
    }
    if (s->ppq) {
        build_tempo_map(s, raw, nraw);
    }
    free(raw);
    s->total_us = smf_tick_to_us(s, s->total_ticks);
    ESP_LOGI(TAG, "%s: format %u, %u tracks, %d parts, ppq %u, %d tempos, %llu ms",
             path_name(path), s->format, s->ntracks, s->nparts, s->ppq, s->ntempos,
             (unsigned long long)(s->total_us / 1000));
    return SMF_OK;
}
