#include <stdio.h>
#include <string.h>
#include "theme.h"
#include "font.h"
#include "icons.h"
#include "ln_song.h"
#include "ln_store.h"
#include "ln_ui.h"

ln_setup_t g_lset = { .hand = LH_RIGHT, .mode = LN_MODE_WAIT, .speed = 100, .countin = 1, .builtin = -1 };
uint8_t g_kb_lo, g_kb_hi;
bool g_kb_known;
const ln_res_ops_t *g_lres_ops;

void ln_ui_load_keyboard(void)
{
    g_kb_known = ln_kb_load(&g_kb_lo, &g_kb_hi);
    if (!g_kb_known) {
        g_kb_lo = g_kb_hi = 0;
    }
}

unsigned ln_ui_hands(void)
{
    return g_lset.hand == LH_RIGHT ? 1u : (g_lset.hand == LH_LEFT ? 2u : 3u);
}

const char *ln_ui_hand_name(int hand)
{
    static const char *const n[3] = { "Right hand", "Left hand", "Both hands" };
    return n[hand < 0 || hand > 2 ? 0 : hand];
}

void ln_ui_note_name(char *out, int n, int note)
{
    static const char *const pc[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    snprintf(out, (size_t)n, "%s%d", pc[note % 12], note / 12 - 1);
}

int ln_ui_fit(int *out_of_reach, uint8_t *lo, uint8_t *hi)
{
    *lo = *hi = 0;
    if (!ln_song_range(ln_ui_hands(), lo, hi)) {
        *out_of_reach = 0;
        return 0;
    }
    if (!g_kb_known) {
        *out_of_reach = 0;
        return 0;
    }
    static const int8_t tries[5] = { 0, 12, -12, 24, -24 };
    int best = 0, best_out = -1;
    unsigned hands = ln_ui_hands();
    for (int t = 0; t < 5; t++) {
        int out = 0;
        for (int i = 0; i < g_ln.n; i++) {
            const ln_note_t *n = &g_ln.notes[i];
            if (n->hand < 2 && (hands & (1u << n->hand))) {
                int k = n->note + tries[t];
                out += k < g_kb_lo || k > g_kb_hi;
            }
        }
        if (best_out < 0 || out < best_out) {
            best_out = out;
            best = tries[t];
        }
    }
    *out_of_reach = best_out;
    return best;
}

void ln_ui_score_key(char *out, int n)
{
    static const char hc[3] = { 'R', 'L', 'B' };
    snprintf(out, (size_t)n, "%s|%c|%s", g_ln.key, hc[g_lset.hand], g_lset.mode == LN_MODE_PLAY ? "play" : "wait");
}

/* ------------------------------------------------------------------ keyboard */

static bool is_white(int n)
{
    int k = n % 12;
    return k == 0 || k == 2 || k == 4 || k == 5 || k == 7 || k == 9 || k == 11;
}

static int whites_between(int lo, int hi)
{
    int c = 0;
    for (int n = lo; n <= hi; n++) {
        c += is_white(n);
    }
    return c;
}

void ln_ui_kb_layout(ln_kb_t *k, int lo, int hi, int x, int w, int y, int h)
{
    lo = lo - lo % 12;              /* from a C */
    if (hi % 12) {
        hi += 12 - hi % 12;         /* up to the next C (unless it already is one) */
    }
    if (hi > 127) {
        hi = 127;
    }
    if (hi - lo < 24) {             /* at least two octaves */
        hi = lo + 24;
    }
    int nw = whites_between(lo, hi);
    k->lo = lo;
    k->hi = hi;
    k->kw = nw ? w / nw : 8;
    if (k->kw < 4) {
        k->kw = 4;
    }
    k->x0 = x + (w - k->kw * nw) / 2;
    k->y = y;
    k->h = h;
}

bool ln_ui_key_x(const ln_kb_t *k, int note, int *x, int *w)
{
    if (note < k->lo || note > k->hi) {
        return false;
    }
    int wi = whites_between(k->lo, note) - 1;
    if (is_white(note)) {
        *x = k->x0 + wi * k->kw;
        *w = k->kw - 1;
    } else {
        int bw = k->kw * 6 / 10;
        if (bw < 3) {
            bw = 3;
        }
        *x = k->x0 + (wi + 1) * k->kw - bw / 2 - 1;
        *w = bw;
    }
    return true;
}

void ln_ui_kb_draw(gfx_t *g, const ln_kb_t *k, const uint16_t *colour, int dim_lo, int dim_hi)
{
    uint16_t dim = RGB(0x3A, 0x41, 0x4C);
    for (int pass = 0; pass < 2; pass++) {
        for (int n = k->lo; n <= k->hi; n++) {
            if (is_white(n) != (pass == 0)) {
                continue;
            }
            int x, w;
            ln_ui_key_x(k, n, &x, &w);
            uint16_t c = colour && colour[n] ? colour[n] : (pass == 0 ? C_KEY_W : C_KEY_B);
            if (dim_hi && (n < dim_lo || n > dim_hi) && !(colour && colour[n])) {
                c = pass == 0 ? dim : C_BG;
            }
            gfx_fill(g, x, k->y, w, pass == 0 ? k->h : k->h * 6 / 10, c);
        }
    }
}

/* ------------------------------------------------------------------ grand staff */

int ln_ui_staff_y(int pos) { return LN_STAFF_C4_Y - (pos - 28) * LN_STAFF_STEP; }

int ln_ui_staff_pos(int note, int acc)
{
    static const int8_t letter[12] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
    int nat = note - acc;
    if (acc == 0 && letter[((nat % 12) + 12) % 12] < 0) {
        nat--;   /* a black key with no spelling given: as a sharp */
    }
    int pc = ((nat % 12) + 12) % 12;
    int l = letter[pc] < 0 ? letter[pc - 1] : letter[pc];
    return (nat / 12 - 1) * 7 + l;
}

void ln_ui_staff(gfx_t *g, int x0, int x1, uint16_t treble, uint16_t bass)
{
    for (int i = 0; i < 5; i++) {
        gfx_hline(g, x0, ln_ui_staff_y(30 + i * 2), x1 - x0, treble);
        gfx_hline(g, x0, ln_ui_staff_y(18 + i * 2), x1 - x0, bass);
    }
    int top = ln_ui_staff_y(38), bot = ln_ui_staff_y(18);
    gfx_vline(g, x0, top, bot - top + 1, LN_STAFF_LINE);
    gfx_icon(g, &icon_clef_treble, x0 + 5, top - 9, treble == LN_STAFF_LINE ? C_TEXT : treble);
    gfx_icon(g, &icon_clef_bass, x0 + 3, ln_ui_staff_y(26) - 1, bass == LN_STAFF_LINE ? C_TEXT : bass);
}

static void head(gfx_t *g, int x, int y, uint16_t c, int sc)
{
    static const uint16_t rows[7] = { 0x03C, 0x0FE, 0x1FF, 0x1FF, 0x1FF, 0x0FE, 0x078 };
    for (int r = 0; r < 7; r++) {
        for (int b = 0; b < 9; b++) {
            if (rows[r] & (0x100 >> b)) {
                gfx_fill(g, x - 4 * sc + b * sc, y - 3 * sc + r * sc, sc, sc, c);
            }
        }
    }
}

void ln_ui_staff_note(gfx_t *g, int x, int note, int acc, uint16_t c, bool big)
{
    int nat = note;
    int d = ln_ui_staff_pos(note, acc);
    int y = ln_ui_staff_y(d);
    int w = big ? 22 : 15;
    if (d == 28) {
        gfx_hline(g, x - w / 2, y, w, LN_STAFF_LEDGER);
    }
    for (int l = 40; l <= d; l += 2) {
        gfx_hline(g, x - w / 2, ln_ui_staff_y(l), w, LN_STAFF_LEDGER);
    }
    for (int l = 16; l >= d; l -= 2) {
        gfx_hline(g, x - w / 2, ln_ui_staff_y(l), w, LN_STAFF_LEDGER);
    }
    head(g, x, y, c, big ? 2 : 1);
    /* the accidental the spelling needs */
    static const int8_t black[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
    int a = acc;
    if (a == 0 && black[((nat % 12) + 12) % 12]) {
        a = 1;
    }
    if (a) {
        const char *t = a == 1 ? "#" : (a == -1 ? "b" : (a == 2 ? "x" : "bb"));
        const font_t *f = big ? &font_large : &font_main;
        int tw = gfx_text_width(f, t);
        gfx_text(g, f, x - (big ? 12 : 7) - tw, y - (big ? 10 : 7), t, c);
    }
}

void ln_ui_note_name_acc(char *out, int n, int note, int acc)
{
    static const char letters[8] = "CDEFGAB";
    int pos = ln_ui_staff_pos(note, acc);
    static const int8_t black[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
    if (acc == 0 && black[((note % 12) + 12) % 12]) {
        acc = 1;
    }
    const char *a = acc == 1 ? "#" : (acc == -1 ? "b" : (acc == 2 ? "x" : (acc == -2 ? "bb" : "")));
    snprintf(out, (size_t)n, "%c%s%d", letters[((pos % 7) + 7) % 7], a, pos / 7);
}
