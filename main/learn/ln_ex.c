/*
 * ln_ex.c - the exercises: note lists, spelling and fingering. See ln_ex.h.
 */
#include <stdio.h>
#include <string.h>
#include "ln_song.h"
#include "ln_ui.h"
#include "ln_ex.h"

static const int s_count[LX_NCAT] = { 12, 12, 24, 24 };
static const char *const s_cat[LX_NCAT] = { "Major scales", "Minor scales", "Chords", "Arpeggios" };
static const char *const s_code[LX_NCAT] = { "maj", "min", "chd", "arp" };

/* key names by tonic pitch class; minor keys use C#, G# and Eb, Bb like most books */
static const char *const s_maj_name[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
static const char *const s_min_name[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B" };
static const uint8_t s_nat[7] = { 0, 2, 4, 5, 7, 9, 11 };
static const uint8_t s_major[7] = { 0, 2, 4, 5, 7, 9, 11 };
static const uint8_t s_hminor[7] = { 0, 2, 3, 5, 7, 8, 11 };

/* fingering, one octave going up: right hand from the tonic, left hand from the tonic */
static const char *const s_fmaj[12][2] = {
    { "12312345", "54321321" }, { "23123412", "32143213" }, { "12312345", "54321321" },
    { "31234123", "32143213" }, { "12312345", "54321321" }, { "12341234", "54321321" },
    { "23412312", "43213214" }, { "12312345", "54321321" }, { "34123123", "32143213" },
    { "12312345", "54321321" }, { "41231234", "32143213" }, { "12312345", "43214321" },
};
static const char *const s_fmin[12][2] = {
    { "12312345", "54321321" }, { "34123123", "32143213" }, { "12312345", "54321321" },
    { "31234123", "21432132" }, { "12312345", "54321321" }, { "12341234", "54321321" },
    { "34123123", "43213214" }, { "12312345", "54321321" }, { "34123123", "32143213" },
    { "12312345", "54321321" }, { "41231234", "21321432" }, { "12312345", "43214321" },
};

static uint8_t s_fing[LX_MAX_NOTES * 2];   /* finger per note of g_ln */

int ln_ex_count(int cat) { return cat >= 0 && cat < LX_NCAT ? s_count[cat] : 0; }
const char *ln_ex_cat_name(int cat) { return cat >= 0 && cat < LX_NCAT ? s_cat[cat] : ""; }

static bool is_minor(int cat, int idx) { return cat == LX_MINOR || (cat >= LX_CHORDS && idx >= 12); }
static int tonic(int idx) { return idx % 12; }

void ln_ex_name(int cat, int idx, char *out, int n)
{
    bool mi = is_minor(cat, idx);
    const char *k = mi ? s_min_name[tonic(idx)] : s_maj_name[tonic(idx)];
    const char *tail = cat == LX_CHORDS ? " chord" : (cat == LX_ARPS ? " arpeggio" : "");
    snprintf(out, (size_t)n, "%s %s%s", k, mi ? "minor" : "major", tail);
}

void ln_ex_desc(const ln_ex_t *x, char *out, int n)
{
    if (x->cat == LX_CHORDS) {
        snprintf(out, (size_t)n, "root position and inversions, up and back");
    } else {
        snprintf(out, (size_t)n, "%s%d octave%s up and down", x->cat == LX_MINOR ? "harmonic, " : "", x->octaves,
                 x->octaves > 1 ? "s" : "");
    }
}

void ln_ex_key(int cat, int idx, char *out, int n)
{
    snprintf(out, (size_t)n, "x:%s:%d", cat >= 0 && cat < LX_NCAT ? s_code[cat] : "?", idx);
}

/* letter of the tonic (0 = C .. 6 = B) from the key's name */
static int tonic_letter(bool minor, int pc)
{
    const char *nm = minor ? s_min_name[pc] : s_maj_name[pc];
    return (int)(strchr("CDEFGAB", nm[0]) - "CDEFGAB");
}

int ln_ex_scale(int minor, int pc, int degree, int8_t *acc)
{
    const uint8_t *iv = minor ? s_hminor : s_major;
    int off = 12 * (degree / 7) + iv[degree % 7];
    int letter = (tonic_letter(minor, pc) + degree) % 7;
    int p = (pc + off) % 12;
    int a = ((p - s_nat[letter]) % 12 + 12) % 12;   /* 0..11 */
    if (a > 6) {
        a -= 12;                                     /* -5..6, in practice -2..2 */
    }
    if (acc) {
        *acc = (int8_t)a;
    }
    return off;
}

static int digit(const char *s, int i) { return s[i] - '0'; }

/* finger for scale degree d (0 .. 7*oct) of a scale going up and down */
static int scale_finger(bool minor, int pc, int hand, int d, int top)
{
    const char *f = (minor ? s_fmin : s_fmaj)[pc][hand];
    if (hand == 0) {
        return d == top ? digit(f, 7) : digit(f, d % 7);
    }
    if (d == 0) {
        return digit(f, 0);
    }
    return d % 7 == 0 ? digit(f, 7) : digit(f, d % 7);
}

static int arp_finger(int hand, int k, int top)
{
    /* k counts chord tones going up: 0 root, 1 third, 2 fifth, 3 root, ... */
    static const uint8_t rh[3] = { 1, 2, 3 }, lh[3] = { 1, 4, 2 };
    if (hand == 0) {
        return k == top ? 5 : rh[k % 3];
    }
    return k == 0 ? 5 : lh[k % 3];
}

/* chord fingers bottom to top, packed as 3 digits: 135 */
static int chord_fingers(int hand, int inv)
{
    static const int rh[3] = { 135, 125, 135 }, lh[3] = { 531, 531, 521 };
    return hand == 0 ? rh[inv] : lh[inv];
}

typedef struct {
    ln_note_t n;
    uint8_t finger;
} pend_t;

static pend_t s_tmp[LX_MAX_NOTES * 2];
static int s_ntmp;

static void add(uint32_t t, uint32_t len, int pitch, int8_t acc, int hand, int finger)
{
    if (s_ntmp >= (int)(sizeof(s_tmp) / sizeof(s_tmp[0])) || pitch < 0 || pitch > 127) {
        return;
    }
    pend_t *p = &s_tmp[s_ntmp++];
    memset(p, 0, sizeof(*p));
    p->n.t = t;
    p->n.len = (uint16_t)len;
    p->n.note = (uint8_t)pitch;
    p->n.vel = hand == LN_RH ? 84 : 72;
    p->n.hand = (uint8_t)hand;
    p->n.acc = acc;
    p->finger = (uint8_t)finger;
}

bool ln_ex_build(const ln_ex_t *x)
{
    bool minor = is_minor(x->cat, x->idx);
    int pc = tonic(x->idx);
    int oct = x->octaves == 1 ? 1 : 2;
    int bpm = x->bpm < 30 ? 30 : (x->bpm > 200 ? 200 : x->bpm);
    uint32_t beat = 60000u / (uint32_t)bpm;
    s_ntmp = 0;
    for (int h = 0; h < 2; h++) {
        if (!((x->hand == LH_BOTH) || (x->hand == LH_RIGHT && h == 0) || (x->hand == LH_LEFT && h == 1))) {
            continue;
        }
        /* right hand from middle C upwards (two octaves from F up start an octave lower, so the
         * top stays near the staff); left hand from C3 or, for two octaves, C2 */
        int base = h ? (oct == 2 ? 36 : 48) + pc : (oct == 2 && pc >= 5 ? 48 : 60) + pc;
        int8_t acc;
        if (x->cat == LX_MAJOR || x->cat == LX_MINOR) {
            int top = 7 * oct, k = 0;
            for (int d = 0; d <= top; d++, k++) {
                int off = ln_ex_scale(minor, pc, d, &acc);
                add((uint32_t)k * beat, d == top ? beat : beat * 9 / 10, base + off, acc, h,
                    scale_finger(minor, pc, h, d, top));
            }
            for (int d = top - 1; d >= 0; d--, k++) {
                int off = ln_ex_scale(minor, pc, d, &acc);
                add((uint32_t)k * beat, d == 0 ? beat * 2 : beat * 9 / 10, base + off, acc, h,
                    scale_finger(minor, pc, h, d, top));
            }
        } else if (x->cat == LX_ARPS) {
            int top = 3 * oct, k = 0;
            for (int i = 0; i <= top; i++, k++) {
                int d = (i / 3) * 7 + (i % 3) * 2;
                int off = ln_ex_scale(minor, pc, d, &acc);
                add((uint32_t)k * beat, i == top ? beat : beat * 9 / 10, base + off, acc, h, arp_finger(h, i, top));
            }
            for (int i = top - 1; i >= 0; i--, k++) {
                int d = (i / 3) * 7 + (i % 3) * 2;
                int off = ln_ex_scale(minor, pc, d, &acc);
                add((uint32_t)k * beat, i == 0 ? beat * 2 : beat * 9 / 10, base + off, acc, h, arp_finger(h, i, top));
            }
        } else {
            /* root, 1st, 2nd inversion, root an octave up, then back down: a chord every 2 beats */
            static const int8_t inv_of[7] = { 0, 1, 2, 3, 2, 1, 0 };
            for (int c = 0; c < 7; c++) {
                int inv = inv_of[c];
                int fg = chord_fingers(h, inv == 3 ? 0 : inv);
                for (int j = 0; j < 3; j++) {
                    /* chord tones from the bottom: steps of a third, wrapping the root up an octave */
                    int tone = (inv == 3 ? 0 : inv) + j;       /* 0 root, 1 third, 2 fifth, 3 root up, 4 third up */
                    int d = (inv == 3 ? 7 : 0) + (tone / 3) * 7 + (tone % 3) * 2;
                    int off = ln_ex_scale(minor, pc, d, &acc);
                    int f = j == 0 ? fg / 100 : (j == 1 ? fg / 10 % 10 : fg % 10);
                    add((uint32_t)c * 2 * beat, c == 6 ? beat * 4 : beat * 2 - beat / 10, base + off, acc, h, f);
                }
            }
        }
    }
    char title[48], key[24];
    ln_ex_name(x->cat, x->idx, title, sizeof(title));
    ln_ex_key(x->cat, x->idx, key, sizeof(key));
    static ln_note_t notes[LX_MAX_NOTES * 2];
    for (int i = 0; i < s_ntmp; i++) {
        notes[i] = s_tmp[i].n;
    }
    if (!ln_song_custom(title, key, notes, s_ntmp, beat, 4)) {
        return false;
    }
    /* fingers in g_ln's (sorted) order */
    memset(s_fing, 0, sizeof(s_fing));
    bool used[LX_MAX_NOTES * 2] = { false };
    for (int i = 0; i < g_ln.n && i < LX_MAX_NOTES * 2; i++) {
        const ln_note_t *a = &g_ln.notes[i];
        for (int j = 0; j < s_ntmp; j++) {
            const ln_note_t *b = &s_tmp[j].n;
            if (!used[j] && a->t == b->t && a->note == b->note && a->hand == b->hand) {
                used[j] = true;
                s_fing[i] = s_tmp[j].finger;
                break;
            }
        }
    }
    return true;
}

int ln_ex_finger(const ln_ex_t *x, int i)
{
    (void)x;
    return i >= 0 && i < LX_MAX_NOTES * 2 && i < g_ln.n ? s_fing[i] : 0;
}
