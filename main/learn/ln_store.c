/*
 * ln_store.c - keyboard range and best scores on the SD card. The score file is small
 * (one line per song you've played), so it is read whole and rewritten on a new best.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sd.h"
#include "ln_store.h"

#define KB_FILE     LN_DIR "/keyboard.txt"
#define SCORES_FILE LN_DIR "/scores.txt"
#define PRAC_FILE   LN_DIR "/practice.txt"
#define EAR_FILE    LN_DIR "/ear.txt"

bool ln_kb_load(uint8_t *lo, uint8_t *hi)
{
    char buf[32];
    size_t n = fs_read_head(KB_FILE, buf, sizeof(buf) - 1);
    if (n == 0) {
        return false;
    }
    buf[n] = 0;
    int a, b;
    if (sscanf(buf, "%d %d", &a, &b) != 2 || a < 0 || b > 127 || a + 12 > b) {
        return false;
    }
    *lo = (uint8_t)a;
    *hi = (uint8_t)b;
    return true;
}

bool ln_kb_save(uint8_t lo, uint8_t hi)
{
    fs_mkdir(LN_DIR);
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%u %u\n", lo, hi);
    return fs_write_all(KB_FILE, buf, (size_t)n);
}

/* whole file in memory; NULL if none */
static char *read_scores(size_t *len)
{
    char *buf = (char *)fs_read_all(SCORES_FILE, len);
    if (!buf) {
        return NULL;
    }
    char *z = realloc(buf, *len + 1);
    if (!z) {
        free(buf);
        return NULL;
    }
    z[*len] = 0;
    return z;
}

/* calls fn(score, stars, key) for each line; stops when fn returns true */
static void each(const char *buf, bool (*fn)(uint32_t, int, const char *, void *), void *ctx)
{
    const char *p = buf;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t l = nl ? (size_t)(nl - p) : strlen(p);
        char line[260];
        if (l < sizeof(line)) {
            memcpy(line, p, l);
            line[l] = 0;
            unsigned long sc;
            int st, off = 0;
            if (sscanf(line, "%lu\t%d\t%n", &sc, &st, &off) == 2 && off > 0) {
                if (fn((uint32_t)sc, st, line + off, ctx)) {
                    return;
                }
            }
        }
        p = nl ? nl + 1 : NULL;
    }
}

typedef struct {
    const char *key;
    uint32_t score;
    int stars;
    bool found;
} find_t;

static bool find_fn(uint32_t sc, int st, const char *key, void *ctx)
{
    find_t *f = ctx;
    if (!strcmp(key, f->key)) {
        f->score = sc;
        f->stars = st;
        f->found = true;
        return true;
    }
    return false;
}

bool ln_best_get(const char *key, uint32_t *score, int *stars)
{
    size_t len;
    char *buf = read_scores(&len);
    find_t f = { .key = key };
    if (buf) {
        each(buf, find_fn, &f);
        free(buf);
    }
    *score = f.score;
    *stars = f.stars;
    return f.found;
}

bool ln_best_put(const char *key, uint32_t score, int stars)
{
    size_t len = 0;
    char *buf = read_scores(&len);
    find_t f = { .key = key };
    if (buf) {
        each(buf, find_fn, &f);
    }
    if (f.found && score <= f.score && stars <= f.stars) {
        free(buf);
        return false;
    }
    uint32_t best = f.found && f.score > score ? f.score : score;
    int bst = f.found && f.stars > stars ? f.stars : stars;
    /* rewrite: every other line, then this one */
    size_t cap = len + strlen(key) + 32;
    char *out = malloc(cap);
    if (!out) {
        free(buf);
        return false;
    }
    size_t o = 0;
    const char *p = buf;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t l = nl ? (size_t)(nl - p) : strlen(p);
        const char *k = memchr(p, '\t', l);
        k = k ? memchr(k + 1, '\t', l - (size_t)(k + 1 - p)) : NULL;
        bool same = k && (size_t)(p + l - (k + 1)) == strlen(key) && !memcmp(k + 1, key, strlen(key));
        if (!same && l) {
            memcpy(out + o, p, l);
            o += l;
            out[o++] = '\n';
        }
        p = nl ? nl + 1 : NULL;
    }
    o += (size_t)snprintf(out + o, cap - o, "%lu\t%d\t%s\n", (unsigned long)best, bst, key);
    free(buf);
    fs_mkdir(LN_DIR);
    bool ok = fs_write_all(SCORES_FILE, out, o);
    free(out);
    return ok && (!f.found || score > f.score);
}

typedef struct {
    const char *prefix;
    size_t plen;
    int stars;
} stars_t;

static bool stars_fn(uint32_t sc, int st, const char *key, void *ctx)
{
    (void)sc;
    stars_t *s = ctx;
    if (!strncmp(key, s->prefix, s->plen) && key[s->plen] == '|' && st > s->stars) {
        s->stars = st;
    }
    return false;
}

int ln_best_stars(const char *song_key)
{
    size_t len;
    char *buf = read_scores(&len);
    stars_t s = { song_key, strlen(song_key), 0 };
    if (buf) {
        each(buf, stars_fn, &s);
        free(buf);
    }
    return s.stars;
}

/* best stars per song, summed: a song counts once however many hands / modes */
#define TOTAL_KEYS 192
typedef struct {
    char key[TOTAL_KEYS][64];
    int stars[TOTAL_KEYS];
    int n;
    const char *prefix;
} total_t;

static bool total_fn(uint32_t sc, int st, const char *key, void *ctx)
{
    (void)sc;
    total_t *t = ctx;
    if (t->prefix && strncmp(key, t->prefix, strlen(t->prefix))) {
        return false;
    }
    const char *bar = strchr(key, '|');
    size_t l = bar ? (size_t)(bar - key) : strlen(key);
    if (l >= sizeof(t->key[0])) {
        l = sizeof(t->key[0]) - 1;
    }
    for (int i = 0; i < t->n; i++) {
        if (strlen(t->key[i]) == l && !strncmp(t->key[i], key, l)) {
            if (st > t->stars[i]) {
                t->stars[i] = st;
            }
            return false;
        }
    }
    if (t->n < TOTAL_KEYS) {
        memcpy(t->key[t->n], key, l);
        t->key[t->n][l] = 0;
        t->stars[t->n++] = st;
    }
    return false;
}

int ln_total_stars(void) { return ln_total_stars_prefix(NULL); }

int ln_total_stars_prefix(const char *prefix)
{
    size_t len;
    char *buf = read_scores(&len);
    total_t *t = calloc(1, sizeof(total_t));
    int sum = 0;
    if (buf && t) {
        t->prefix = prefix;
        each(buf, total_fn, t);
        for (int i = 0; i < t->n; i++) {
            sum += t->stars[i];
        }
    }
    free(t);
    free(buf);
    return sum;
}

typedef struct {
    const char *prefix;
    size_t plen;
    int8_t *out;
    int n;
} list_t;

static bool list_fn(uint32_t sc, int st, const char *key, void *ctx)
{
    (void)sc;
    list_t *l = ctx;
    if (strncmp(key, l->prefix, l->plen)) {
        return false;
    }
    char *end;
    long i = strtol(key + l->plen, &end, 10);
    if (end != key + l->plen && *end == '|' && i >= 0 && i < l->n && st > l->out[i]) {
        l->out[i] = (int8_t)st;
    }
    return false;
}

void ln_best_stars_list(const char *prefix, int8_t *out, int n)
{
    memset(out, 0, (size_t)n);
    size_t len;
    char *buf = read_scores(&len);
    list_t l = { prefix, strlen(prefix), out, n };
    if (buf) {
        each(buf, list_fn, &l);
        free(buf);
    }
}

/* ------------------------------------------------------------------ keys with stars */

int ln_count_learnt(const char *prefix)
{
    size_t len;
    char *buf = read_scores(&len);
    total_t *t = calloc(1, sizeof(total_t));
    int n = 0;
    if (buf && t) {
        t->prefix = prefix;
        each(buf, total_fn, t);
        for (int i = 0; i < t->n; i++) {
            n += t->stars[i] > 0;
        }
    }
    free(t);
    free(buf);
    return n;
}

/* ------------------------------------------------------------------ ear training level */

int ln_ear_max_load(void)
{
    char buf[8];
    size_t n = fs_read_head(EAR_FILE, buf, sizeof(buf) - 1);
    buf[n] = 0;
    int v = n ? atoi(buf) : 0;
    return v < 0 ? 0 : (v > 5 ? 5 : v);
}

void ln_ear_max_save(int level)
{
    fs_mkdir(LN_DIR);
    char buf[8];
    int n = snprintf(buf, sizeof(buf), "%d\n", level);
    fs_write_all(EAR_FILE, buf, (size_t)n);
}

/* ------------------------------------------------------------------ practice time */

static uint32_t s_sess_ms, s_last_active, s_written_s;
static bool s_counted;
static uint16_t s_best_combo_ram;

void ln_prac_active(uint32_t now_ms)
{
    uint32_t d = now_ms - s_last_active;
    if (s_last_active && d < 1000) {
        s_sess_ms += d;   /* gaps over a second (menus, pauses) don't count */
    }
    s_last_active = now_ms;
}

void ln_prac_combo(uint16_t combo)
{
    if (combo > s_best_combo_ram) {
        s_best_combo_ram = combo;
    }
}

static void prac_read(ln_prac_t *p)
{
    memset(p, 0, sizeof(*p));
    char buf[160];
    size_t n = fs_read_head(PRAC_FILE, buf, sizeof(buf) - 1);
    buf[n] = 0;
    unsigned a = 0, b = 0, c = 0;
    if (n && sscanf(buf, "%u %u %u", &a, &b, &c) == 3) {
        p->total_s = a;
        p->sessions = b;
        p->best_combo = (uint16_t)c;
        const char *q = strchr(buf, '\n');
        while (q && p->nrecent < LN_RECENT) {
            char *e;
            long v = strtol(q + (*q == '\n'), &e, 10);
            if (e == q + (*q == '\n')) {
                break;
            }
            p->recent[p->nrecent++] = (uint32_t)v;
            q = e;
        }
    }
}

static void prac_write(const ln_prac_t *p)
{
    fs_mkdir(LN_DIR);
    char buf[160];
    int n = snprintf(buf, sizeof(buf), "%lu %lu %u\n", (unsigned long)p->total_s, (unsigned long)p->sessions, p->best_combo);
    for (int i = 0; i < p->nrecent; i++) {
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "%lu%s", (unsigned long)p->recent[i], i + 1 < p->nrecent ? " " : "\n");
    }
    fs_write_all(PRAC_FILE, buf, (size_t)n);
}

/* this session, as it would be after a flush */
static void merge(ln_prac_t *p)
{
    uint32_t sess = s_sess_ms / 1000;
    p->total_s += sess - s_written_s;
    if (s_best_combo_ram > p->best_combo) {
        p->best_combo = s_best_combo_ram;
    }
    if (sess < LN_MIN_SESSION_S && !s_counted) {
        return;
    }
    if (!s_counted) {
        p->sessions++;
        if (p->nrecent == LN_RECENT) {
            memmove(&p->recent[0], &p->recent[1], sizeof(p->recent[0]) * (LN_RECENT - 1));
            p->nrecent--;
        }
        p->recent[p->nrecent++] = sess;
    } else if (p->nrecent) {
        p->recent[p->nrecent - 1] = sess;
    }
}

void ln_prac_flush(void)
{
    uint32_t sess = s_sess_ms / 1000;
    if (sess == s_written_s && s_best_combo_ram == 0) {
        return;
    }
    ln_prac_t p;
    prac_read(&p);
    merge(&p);
    prac_write(&p);
    if (sess >= LN_MIN_SESSION_S) {
        s_counted = true;
    }
    s_written_s = sess;
    s_best_combo_ram = 0;
}

void ln_prac_end(void)
{
    ln_prac_flush();
    s_sess_ms = s_written_s = s_last_active = 0;
    s_counted = false;
}

void ln_prac_get(ln_prac_t *out)
{
    prac_read(out);
    merge(out);
}
