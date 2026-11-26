/*
 * Chords: names the chord being played (C, Am7, G7/B, Fmaj7 ...), shows its notes
 * and the last few chords as a progression.
 *
 * Button 10: which channel to listen to (All channels except drums, then 1-16).
 * Button 11: colour.
 */
#include <stdio.h>
#include <string.h>
#include "monitor.h"
#include "theme.h"
#include "viz_common.h"
#include "vz.h"

#define HISTORY 6

static const char *const s_pc[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

typedef struct {
    uint16_t mask;       /* intervals from the root, bit 0 = root */
    const char *suffix;
} tmpl_t;

static const tmpl_t s_tmpl[] = {
    { 0x091, "" },        /* 0 4 7 */
    { 0x089, "m" },       /* 0 3 7 */
    { 0x049, "dim" },     /* 0 3 6 */
    { 0x111, "aug" },     /* 0 4 8 */
    { 0x085, "sus2" },    /* 0 2 7 */
    { 0x0A1, "sus4" },    /* 0 5 7 */
    { 0x491, "7" },       /* 0 4 7 10 */
    { 0x891, "maj7" },    /* 0 4 7 11 */
    { 0x489, "m7" },      /* 0 3 7 10 */
    { 0x449, "m7b5" },    /* 0 3 6 10 */
    { 0x249, "dim7" },    /* 0 3 6 9 */
    { 0x889, "m(maj7)" }, /* 0 3 7 11 */
    { 0x291, "6" },       /* 0 4 7 9 */
    { 0x289, "m6" },      /* 0 3 7 9 */
    { 0x095, "add9" },    /* 0 2 4 7 */
    { 0x495, "9" },       /* 0 2 4 7 10 */
    { 0x895, "maj9" },    /* 0 2 4 7 11 */
    { 0x48D, "m9" },      /* 0 2 3 7 10 */
    { 0x4A1, "7sus4" },   /* 0 5 7 10 */
    { 0x411, "7" },       /* 0 4 10, no fifth */
    { 0x811, "maj7" },    /* 0 4 11, no fifth */
    { 0x409, "m7" },      /* 0 3 10, no fifth */
    { 0x081, "5" },       /* 0 7 power chord */
};
#define NTMPL ((int)(sizeof(s_tmpl) / sizeof(s_tmpl[0])))

static const char *const s_interval[12] = {
    "Octave", "Minor 2nd", "Major 2nd", "Minor 3rd", "Major 3rd", "Perfect 4th",
    "Tritone", "Perfect 5th", "Minor 6th", "Major 6th", "Minor 7th", "Major 7th",
};

static char s_name[24], s_last[24];
static char s_hist[HISTORY][24];
static int s_nhist;
static char s_pending[24];
static int s_stable;
static int s_since_notes = 1000;

static int popcount12(uint16_t m)
{
    int n = 0;
    for (int i = 0; i < 12; i++) {
        n += (m >> i) & 1;
    }
    return n;
}

static uint16_t rotate(uint16_t set, int r)
{
    return (uint16_t)(((set >> r) | (set << (12 - r))) & 0xFFF);
}

/* best chord for the pitch-class set; returns false if nothing fits */
static bool name_chord(uint16_t set, int bass_pc, char *out, size_t n)
{
    int best = -1000, best_root = 0, best_t = -1;
    for (int r = 0; r < 12; r++) {
        if (!(set & (1 << r))) {
            continue;   /* the root must be played */
        }
        uint16_t m = rotate(set, r);
        for (int t = 0; t < NTMPL; t++) {
            uint16_t tm = s_tmpl[t].mask;
            if ((tm & ~m) != 0) {
                continue;   /* chord notes missing */
            }
            int extra = popcount12((uint16_t)(m & ~tm));
            int score = (extra == 0 ? 100 : 60 - extra * 15) + popcount12(tm) * 3 + (r == bass_pc ? 6 : 0);
            if (score > best) {
                best = score;
                best_root = r;
                best_t = t;
            }
        }
    }
    if (best_t < 0 || best < 40) {
        return false;
    }
    if (best_root == bass_pc) {
        snprintf(out, n, "%s%s", s_pc[best_root], s_tmpl[best_t].suffix);
    } else {
        snprintf(out, n, "%s%s/%s", s_pc[best_root], s_tmpl[best_t].suffix, s_pc[bass_pc]);
    }
    return true;
}

static void push_history(const char *name)
{
    if (s_nhist > 0 && !strcmp(s_hist[s_nhist - 1], name)) {
        return;
    }
    if (s_nhist == HISTORY) {
        memmove(s_hist[0], s_hist[1], sizeof(s_hist[0]) * (HISTORY - 1));
        s_nhist--;
    }
    snprintf(s_hist[s_nhist++], sizeof(s_hist[0]), "%s", name);
}

void vz_chords_draw(int colour, int opt, bool active, bool fresh)
{
    (void)active;
    static int s_filter_seen = -2;
    int filter = opt == 0 ? -1 : opt - 1;
    if (fresh || filter != s_filter_seen) {
        s_nhist = 0;
        s_name[0] = s_last[0] = s_pending[0] = 0;
        s_filter_seen = filter;
    }

    /* held notes */
    uint8_t held[128];
    int count = 0, low = -1;
    uint16_t set = 0;
    for (int note = 0; note < 128; note++) {
        uint8_t v = 0;
        for (int c = 0; c < 16; c++) {
            if ((filter < 0 && c == 9) || (filter >= 0 && c != filter)) {
                continue;
            }
            if (g_mon.vel[c][note] > v) {
                v = g_mon.vel[c][note];
            }
        }
        held[note] = v;
        if (v) {
            count++;
            set |= (uint16_t)(1 << (note % 12));
            if (low < 0) {
                low = note;
            }
        }
    }

    char sub[48] = "";
    if (count == 0) {
        s_since_notes++;
        s_name[0] = 0;
    } else {
        s_since_notes = 0;
        int pcs = popcount12(set);
        if (pcs == 1) {
            snprintf(s_name, sizeof(s_name), "%s", s_pc[low % 12]);
            snprintf(sub, sizeof(sub), count > 1 ? "Octaves" : "Single note");
        } else if (name_chord(set, low % 12, s_name, sizeof(s_name))) {
            if (pcs == 2 && !strchr(s_name, '5')) {
                /* two notes that aren't a power chord: name the interval instead */
                int hi = -1;
                for (int n = 127; n >= 0; n--) {
                    if (held[n] && n % 12 != low % 12) {
                        hi = n;
                        break;
                    }
                }
                snprintf(sub, sizeof(sub), "%s", s_interval[((hi - low) % 12 + 12) % 12]);
            }
        } else if (pcs == 2) {
            int hi = -1;
            for (int n = 127; n >= 0; n--) {
                if (held[n] && n % 12 != low % 12) {
                    hi = n;
                    break;
                }
            }
            snprintf(s_name, sizeof(s_name), "%s + %s", s_pc[low % 12], s_pc[hi % 12]);
            snprintf(sub, sizeof(sub), "%s", s_interval[((hi - low) % 12 + 12) % 12]);
        } else {
            snprintf(s_name, sizeof(s_name), "%d notes", pcs);
            snprintf(sub, sizeof(sub), "No common chord name");
        }
        /* a chord joins the progression once it has been held a moment (ignores passing notes) */
        if (strcmp(s_name, s_pending)) {
            snprintf(s_pending, sizeof(s_pending), "%s", s_name);
            s_stable = 0;
        } else if (++s_stable == 10 && pcs >= 2 && strchr(s_name, ' ') == NULL) {
            push_history(s_name);
        }
        snprintf(s_last, sizeof(s_last), "%s", s_name);
    }

    uint16_t bg = viz_bg(0), acc = viz_col(colour);
    viz_clear(bg);
    gfx_t g = { framebuffer_2, VW, VH };

    const char *big = s_name[0] ? s_name : s_last;
    bool fading = !s_name[0];
    if (big[0]) {
        gfx_text_box(&g, &font_huge, 0, 20, VW, ALIGN_CENTER, big, fading ? gfx_blend(acc, bg, 90) : acc);
    } else {
        gfx_text_box(&g, &font_main, 0, 34, VW, ALIGN_CENTER, "Play some notes", C_DIM);
    }
    if (sub[0]) {
        gfx_text_box(&g, &font_small, 0, 55, VW, ALIGN_CENTER, sub, C_DIM);
    }

    /* the notes, low to high, brightness by velocity */
    char notes[16][6];
    int nn = 0, total = 0, w[16];
    for (int note = 0; note < 128 && nn < 16; note++) {
        if (held[note]) {
            snprintf(notes[nn], sizeof(notes[nn]), "%s%d", s_pc[note % 12], note / 12 - 1);
            w[nn] = gfx_text_width(&font_main, notes[nn]);
            total += w[nn] + (nn ? 10 : 0);
            nn++;
        }
    }
    int x = (VW - total) / 2, k = 0;
    for (int note = 0; note < 128 && k < nn; note++) {
        if (held[note]) {
            gfx_text(&g, &font_main, x, 72, notes[k], gfx_blend(C_TEXT, bg, (uint8_t)(110 + held[note])));
            x += w[k] + 10;
            k++;
        }
    }

    /* progression */
    viz_fill(20, 100, VW - 40, 1, viz_bg2(0));
    if (s_nhist) {
        char line[160] = "";
        for (int i = 0; i < s_nhist; i++) {
            if (i) {
                strncat(line, "   " UI_ENDASH "   ", sizeof(line) - strlen(line) - 1);
            }
            strncat(line, s_hist[i], sizeof(line) - strlen(line) - 1);
        }
        gfx_text_box(&g, &font_main, 10, 112, VW - 20, ALIGN_CENTER, line, C_DIM);
    }

    const char *lbl = filter < 0 ? "All channels" : NULL;
    char chl[16];
    if (!lbl) {
        snprintf(chl, sizeof(chl), "Ch %d", (filter + 1) & 31);
        lbl = chl;
    }
    gfx_text_right(&g, &font_small, VW - 6, 3, lbl, C_DIM);
}
