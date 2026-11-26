/*
 * Spectrum: a 16-band real-time analyser of what the synth is playing.
 *
 * Based on the original MIDI Box spectrum (display_frequency.c): 16 bars on a
 * logarithmic frequency axis, each note adding its harmonics, 24 LED segments and a
 * red peak line. MIDI carries no audio, so the sound is modelled from the messages
 * sent to the synth, as closely as is cheap to do:
 *
 *  - Bands: 16 bars, centres 31.5 Hz .. 16 kHz, ~0.6 octave apart (25 Hz .. 20 kHz).
 *  - Instruments: the General MIDI program of each channel picks a timbre: how strong
 *    each of the first 16 harmonics is (flutes almost pure, strings / brass / saw
 *    leads bright, clarinet and square waves odd harmonics only), and how the sound
 *    moves in time (piano, guitar, bells, plucks die away, higher notes faster, top
 *    harmonics first; organ, strings, wind, pads hold while the key is down; slow
 *    attack for pads and ensembles; release tails after the key is let go).
 *  - Loudness: velocity, channel volume (CC7) and expression (CC11) as the GM
 *    specification says (40 log10 each), harder notes are brighter.
 *  - Sustain pedal (CC64) keeps released notes sounding; pitch bend moves them.
 *  - Drums: each GM drum is a mix of tone and noise where it really sits: kick thump
 *    at 55 Hz plus a click, snare body plus a 2-8 kHz rattle, hi-hats and cymbals up
 *    at 6-16 kHz with their own decay, toms by size, and so on.
 *  - Sound adds up as power, not amplitude; bars show decibels (2 dB per segment, 48 dB
 *    range); a harmonic between two bands is shared between them.
 *  - Meter: bars rise at once and fall at 30 dB/s; the peak line holds for 1 s then
 *    falls at 15 dB/s.
 *
 * Button 10: peak line on/off. Button 11: colour.
 */
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "monitor.h"
#include "viz_common.h"
#include "theme.h"
#include "gfx.h"
#include "font.h"
#include "vz.h"

#define BANDS     16
#define SEGS      24
#define SEG_H     4            /* lit rows of a segment; 1 px gap below each */
#define BAR_W     15
#define PITCH     25
#define TOP_Y     3
#define HARM      16
#define VOICES    96

#define DB_TOP    3.0f         /* level at the top segment */
#define DB_RANGE  48.0f        /* 2 dB per segment */
#define FALL_DBS  30.0f
#define PEAK_HOLD 1.0f
#define PEAK_DBS  15.0f

/* band centres 31.5 Hz * 1.5146^i; log2 of the first and the step */
#define LOG2_C0   4.977280f    /* log2(31.5) */
#define STEP_OCT  0.599196f    /* log2(16000 / 31.5) / 15 */
static const char *const s_label[BANDS] = {
    "31", "48", "72", "110", "170", "250", "380", "580",
    "870", "1.3k", "2k", "3k", "4.6k", "7k", "10k", "16k",
};

/* ---- timbres ------------------------------------------------------------------------ */

typedef struct {
    float tilt;      /* harmonic h has amplitude h^-tilt */
    float even;      /* extra factor for even harmonics (1 = all, ~0.1 = odd only) */
    uint8_t nh;      /* harmonics used */
    float attack;    /* s, linear rise */
    float tau;       /* s, decay time constant (0 = no decay) */
    float sus;       /* level the decay settles at (0 = dies away) */
    float release;   /* s, time constant after key (and pedal) up */
    float bright;    /* how much faster the top harmonics decay (plucked / struck) */
    bool pitch_tau;  /* decay shorter for higher notes (strings of a piano, guitar) */
    bool noise;      /* sound effect: noise around the note, not harmonics */
} timbre_t;

static timbre_t timbre_of(uint8_t prog)
{
    /* family defaults (8 programs each) */
    static const timbre_t fam[16] = {
        /*  tilt even  nh  attack  tau   sus   rel   bright pitch noise */
        { 1.4f, 1.0f, 16, 0.004f, 1.8f, 0.0f, 0.12f, 0.8f, true,  false },  /* piano */
        { 2.2f, 0.5f,  6, 0.002f, 0.8f, 0.0f, 0.30f, 1.5f, true,  false },  /* chromatic percussion */
        { 0.7f, 1.0f, 16, 0.010f, 0.0f, 1.0f, 0.05f, 0.0f, false, false },  /* organ */
        { 1.2f, 1.0f, 16, 0.003f, 1.2f, 0.0f, 0.10f, 1.2f, true,  false },  /* guitar */
        { 1.6f, 1.0f, 12, 0.005f, 1.0f, 0.0f, 0.08f, 1.0f, true,  false },  /* bass */
        { 1.0f, 1.0f, 16, 0.060f, 0.0f, 1.0f, 0.20f, 0.0f, false, false },  /* strings */
        { 1.1f, 1.0f, 16, 0.150f, 0.0f, 1.0f, 0.35f, 0.0f, false, false },  /* ensemble */
        { 0.9f, 1.0f, 16, 0.040f, 0.3f, 0.9f, 0.10f, 0.0f, false, false },  /* brass */
        { 1.0f, 1.0f, 16, 0.030f, 0.0f, 1.0f, 0.08f, 0.0f, false, false },  /* reed */
        { 2.4f, 0.6f,  5, 0.050f, 0.0f, 1.0f, 0.10f, 0.0f, false, false },  /* pipe */
        { 1.0f, 1.0f, 16, 0.010f, 0.0f, 1.0f, 0.10f, 0.0f, false, false },  /* synth lead */
        { 1.4f, 1.0f, 12, 0.400f, 0.0f, 1.0f, 0.80f, 0.0f, false, false },  /* synth pad */
        { 1.3f, 1.0f, 12, 0.200f, 1.0f, 0.8f, 1.00f, 0.0f, false, false },  /* synth effects */
        { 1.2f, 1.0f, 16, 0.003f, 1.0f, 0.0f, 0.10f, 1.0f, true,  false },  /* ethnic (mostly plucked) */
        { 2.0f, 0.5f,  6, 0.002f, 0.4f, 0.0f, 0.10f, 1.5f, false, false },  /* percussive */
        { 1.0f, 1.0f,  1, 0.100f, 0.0f, 1.0f, 0.50f, 0.0f, false, true  },  /* sound effects */
    };
    timbre_t t = fam[(prog >> 3) & 15];
    switch (prog) {
    case 6: t.tilt = 0.9f; t.tau = 1.2f; break;                         /* harpsichord */
    case 7: t.tilt = 0.9f; t.tau = 0.8f; break;                         /* clavinet */
    case 10: t.tau = 0.8f; break;                                       /* music box */
    case 11: t.tau = 2.0f; break;                                       /* vibraphone */
    case 12: t.tau = 0.35f; break;                                      /* marimba */
    case 13: t.tau = 0.2f; break;                                       /* xylophone */
    case 14: t.tilt = 1.2f; t.tau = 3.0f; t.nh = 12; break;             /* tubular bells */
    case 19: t.tilt = 0.6f; t.release = 0.4f; break;                    /* church organ */
    case 21: case 22: case 23: t.tilt = 0.9f; t.attack = 0.03f; break;  /* accordion, harmonica */
    case 29: case 30: t.tilt = 0.5f; t.tau = 4.0f; t.sus = 0.5f; t.bright = 0.2f; break;  /* overdrive, distortion */
    case 31: t.tilt = 2.5f; break;                                      /* guitar harmonics */
    case 38: case 39: t.tilt = 1.0f; t.tau = 0.4f; t.sus = 0.7f; t.pitch_tau = false; break;  /* synth bass */
    case 45: t.attack = 0.002f; t.tau = 0.3f; t.sus = 0.0f; t.bright = 1.0f; break;  /* pizzicato */
    case 46: t.tilt = 1.8f; t.attack = 0.002f; t.tau = 1.5f; t.sus = 0.0f; t.bright = 1.0f; t.pitch_tau = true; break;  /* harp */
    case 47: t.tilt = 2.0f; t.attack = 0.002f; t.tau = 0.8f; t.sus = 0.0f; break;  /* timpani */
    case 50: case 51: t.tilt = 0.9f; t.attack = 0.2f; break;            /* synth strings */
    case 52: case 53: case 54: t.tilt = 1.6f; t.nh = 10; t.attack = 0.12f; break;  /* choir, voice */
    case 55: t.tilt = 0.8f; t.attack = 0.002f; t.tau = 0.3f; t.sus = 0.0f; break;  /* orchestra hit */
    case 59: t.tilt = 0.7f; break;                                      /* muted trumpet */
    case 68: case 69: t.tilt = 0.9f; break;                             /* oboe, english horn */
    case 70: t.tilt = 1.1f; break;                                      /* bassoon */
    case 71: t.tilt = 1.2f; t.even = 0.15f; break;                      /* clarinet: odd harmonics */
    case 80: t.even = 0.05f; break;                                     /* square lead */
    case 109: t.tilt = 0.8f; t.attack = 0.02f; t.tau = 0.0f; t.sus = 1.0f; t.pitch_tau = false; break;  /* bagpipe */
    case 110: case 111: t.tilt = 0.9f; t.attack = 0.05f; t.tau = 0.0f; t.sus = 1.0f; t.pitch_tau = false; break;  /* fiddle, shanai */
    case 112: t.tau = 1.2f; break;                                      /* tinkle bell */
    case 114: t.tau = 0.8f; break;                                      /* steel drums */
    case 115: t.tau = 0.08f; break;                                     /* woodblock */
    case 116: t.tilt = 2.5f; t.tau = 0.5f; break;                       /* taiko */
    case 118: t.tau = 0.3f; break;                                      /* synth drum */
    case 119: t.noise = true; t.attack = 1.0f; t.tau = 0.0f; t.sus = 1.0f; t.release = 0.1f; break;  /* reverse cymbal */
    default: break;
    }
    return t;
}

/* ---- drums: up to two parts (tone or noise) each ------------------------------------- */

typedef struct {
    float freq;      /* Hz, centre */
    float width;     /* octaves (spread, 1 sigma); small = a tone */
    float gain;      /* amplitude */
    float tau;       /* s */
} part_t;

static int drum_parts(uint8_t n, part_t p[2])
{
#define P(i, f, w, g, t) (p[i] = (part_t){ f, w, g, t })
    switch (n) {
    case 35: case 36: P(0, 55, 0.35f, 1.6f, 0.18f); P(1, 3000, 0.8f, 0.10f, 0.015f); return 2;   /* kick: click 24 dB below the thump */
    case 37: P(0, 1200, 0.7f, 0.8f, 0.03f); return 1;                                             /* side stick */
    case 38: case 40: P(0, 200, 0.4f, 1.0f, 0.08f); P(1, 4500, 1.3f, 0.63f, 0.15f); return 2;      /* snare */
    case 39: P(0, 1200, 1.0f, 1.0f, 0.10f); return 1;                                             /* clap */
    case 41: P(0, 80, 0.3f, 1.2f, 0.30f); P(1, 1500, 0.8f, 0.06f, 0.03f); return 2;                /* toms, low to high */
    case 43: P(0, 100, 0.3f, 1.2f, 0.30f); P(1, 1600, 0.8f, 0.06f, 0.03f); return 2;
    case 45: P(0, 120, 0.3f, 1.2f, 0.28f); P(1, 1800, 0.8f, 0.06f, 0.03f); return 2;
    case 47: P(0, 150, 0.3f, 1.2f, 0.26f); P(1, 2000, 0.8f, 0.06f, 0.03f); return 2;
    case 48: P(0, 180, 0.3f, 1.2f, 0.24f); P(1, 2200, 0.8f, 0.06f, 0.03f); return 2;
    case 50: P(0, 220, 0.3f, 1.2f, 0.22f); P(1, 2500, 0.8f, 0.06f, 0.03f); return 2;
    case 42: P(0, 9000, 0.6f, 0.7f, 0.05f); return 1;                                             /* closed hi-hat */
    case 44: P(0, 8000, 0.6f, 0.5f, 0.08f); return 1;                                             /* pedal hi-hat */
    case 46: P(0, 8500, 0.7f, 0.8f, 0.50f); return 1;                                             /* open hi-hat */
    case 49: case 57: P(0, 7000, 1.1f, 1.0f, 1.50f); return 1;                                    /* crash */
    case 51: case 59: P(0, 7500, 0.8f, 0.6f, 1.00f); P(1, 3000, 0.3f, 0.3f, 0.8f); return 2;      /* ride */
    case 53: P(0, 3500, 0.3f, 0.8f, 0.80f); P(1, 8000, 0.8f, 0.3f, 0.6f); return 2;               /* ride bell */
    case 52: P(0, 5000, 1.2f, 1.0f, 1.20f); return 1;                                             /* china */
    case 55: P(0, 8000, 0.9f, 0.9f, 0.60f); return 1;                                             /* splash */
    case 54: P(0, 8000, 0.8f, 0.7f, 0.25f); return 1;                                             /* tambourine */
    case 56: P(0, 560, 0.1f, 0.8f, 0.25f); P(1, 840, 0.1f, 0.5f, 0.25f); return 2;                /* cowbell */
    case 58: P(0, 2500, 1.0f, 0.6f, 0.40f); return 1;                                             /* vibraslap */
    case 60: case 61: P(0, n == 60 ? 400 : 300, 0.3f, 0.9f, 0.12f); return 1;                     /* bongos */
    case 62: case 63: case 64: P(0, n == 64 ? 200 : 280, 0.3f, 1.0f, 0.15f); P(1, 2000, 0.8f, 0.063f, 0.02f); return 2;  /* congas */
    case 65: case 66: P(0, n == 65 ? 300 : 200, 0.3f, 0.9f, 0.20f); P(1, 3000, 0.8f, 0.057f, 0.04f); return 2;  /* timbales */
    case 67: case 68: P(0, n == 67 ? 900 : 700, 0.1f, 0.8f, 0.30f); return 1;                     /* agogo */
    case 69: case 70: case 82: P(0, 7000, 0.8f, 0.5f, 0.06f); return 1;                           /* cabasa, maracas, shaker */
    case 71: case 72: P(0, 2500, 0.1f, 0.7f, n == 71 ? 0.15f : 0.40f); return 1;                  /* whistle */
    case 73: case 74: P(0, 3000, 0.9f, 0.6f, n == 73 ? 0.08f : 0.25f); return 1;                  /* guiro */
    case 75: P(0, 2500, 0.2f, 0.8f, 0.05f); return 1;                                             /* claves */
    case 76: case 77: P(0, n == 76 ? 1200 : 900, 0.3f, 0.8f, 0.06f); return 1;                    /* wood blocks */
    case 78: case 79: P(0, n == 78 ? 350 : 250, 0.4f, 0.7f, 0.20f); return 1;                     /* cuica */
    case 80: case 81: P(0, 5000, 0.15f, 0.6f, n == 80 ? 0.15f : 1.20f); return 1;                 /* triangle */
    default: P(0, 1000, 1.5f, 0.6f, 0.10f); return 1;
    }
#undef P
}

/* ---- voices ------------------------------------------------------------------------- */

typedef struct {
    uint8_t used, ch, note, vel;
    bool key;        /* key still down */
    bool released;   /* key and pedal up: release running */
    float t;         /* s since note-on */
    float rel_t;     /* s since release */
    float rel_lvl;   /* envelope when the release started */
    timbre_t tb;
} voice_t;

/* the big state lives in PSRAM: internal RAM is needed for screen 2's 121 KB DMA framebuffer */
typedef struct {
    voice_t v[VOICES];
    uint8_t ons[16][128];
} spec_mem_t;
static spec_mem_t *s_mem;
#define s_v   (s_mem->v)
#define s_ons (s_mem->ons)
static float s_disp[BANDS], s_peak[BANDS], s_hold[BANDS];
static int64_t s_last_us;
static float s_log2h[HARM + 1];
static float s_noise_w[BANDS];   /* scratch */

static voice_t *voice_for(uint8_t ch, uint8_t note)
{
    voice_t *freev = NULL, *quiet = NULL;
    for (int i = 0; i < VOICES; i++) {
        voice_t *v = &s_v[i];
        if (v->used && v->ch == ch && v->note == note) {
            return v;   /* retrigger the same key */
        }
        if (!v->used && !freev) {
            freev = v;
        }
        if (v->used && (!quiet || v->t > quiet->t)) {
            quiet = v;   /* oldest */
        }
    }
    return freev ? freev : quiet;
}

static float envelope(const voice_t *v)
{
    const timbre_t *t = &v->tb;
    float e = t->attack > 0 && v->t < t->attack ? v->t / t->attack : 1.0f;
    if (t->tau > 0) {
        float tau = t->tau;
        if (t->pitch_tau) {
            tau *= exp2f(-((float)v->note - 60.0f) / 24.0f);   /* an octave up: 30% shorter */
            tau = tau < 0.08f ? 0.08f : tau;
        }
        e *= t->sus + (1.0f - t->sus) * expf(-v->t / tau);
    } else {
        e *= t->sus;
    }
    return e;
}

/* power p at log2 frequency lf: shared between the two nearest bands */
static void add_tone(float *pw, float lf, float p)
{
    float pos = (lf - LOG2_C0) / STEP_OCT;
    if (pos <= -1.0f || pos >= (float)BANDS) {
        return;
    }
    int i = (int)floorf(pos);
    float fr = pos - (float)i;
    if (i >= 0) {
        pw[i] += p * (1.0f - fr);
    }
    if (i + 1 < BANDS) {
        pw[i + 1] += p * fr;
    }
}

/* noise: power p spread over the bands around fc, width in octaves */
static void add_noise(float *pw, float fc, float width, float p)
{
    if (width < 0.2f) {
        add_tone(pw, log2f(fc), p);
        return;
    }
    float lc = log2f(fc), sum = 0;
    for (int b = 0; b < BANDS; b++) {
        float d = (LOG2_C0 + STEP_OCT * (float)b - lc) / width;
        s_noise_w[b] = expf(-0.5f * d * d);
        sum += s_noise_w[b];
    }
    /* noise that is mostly outside the analyser's range shows only what falls inside */
    float norm = width * 2.5066f / STEP_OCT;   /* sum of an unclipped gaussian */
    norm = sum > norm ? sum : norm;
    for (int b = 0; b < BANDS; b++) {
        pw[b] += p * s_noise_w[b] / norm;
    }
}

static float gain2(uint8_t x)   /* GM: 40 log10(x / 127) dB, as a power ratio */
{
    float a = (float)x / 127.0f;
    a *= a;
    return a * a;
}

void vz_spectrum_free(void)
{
    free(s_mem);
    s_mem = NULL;
}

/* ---- per frame ---------------------------------------------------------------------- */

static void update(float dt, float *pw)
{
    /* new notes (the note-on counter also catches notes shorter than a frame) */
    for (int c = 0; c < 16; c++) {
        for (int n = 0; n < 128; n++) {
            uint8_t ons = g_mon.ons[c][n];
            if (ons == s_ons[c][n]) {
                continue;
            }
            s_ons[c][n] = ons;
            voice_t *v = voice_for((uint8_t)c, (uint8_t)n);
            memset(v, 0, sizeof(*v));
            v->used = 1;
            v->ch = (uint8_t)c;
            v->note = (uint8_t)n;
            v->vel = g_mon.on_vel[c][n];
            v->key = true;
            if (c != 9) {
                v->tb = timbre_of(g_mon.program[c]);
            }
        }
    }

    for (int i = 0; i < VOICES; i++) {
        voice_t *v = &s_v[i];
        if (!v->used) {
            continue;
        }
        uint8_t c = v->ch;
        float g = gain2(v->vel) * gain2(g_mon.volume[c]) * gain2(g_mon.expression[c]) * 1.0f;

        if (c == 9) {   /* drum hit: decays by itself */
            part_t p[2];
            int np = drum_parts(v->note, p);
            float alive = 0;
            for (int k = 0; k < np; k++) {
                float a = p[k].gain * expf(-v->t / p[k].tau);
                float pp = g * a * a;
                alive += pp;
                if (pp > 1e-7f) {
                    add_noise(pw, p[k].freq, p[k].width, pp);
                }
            }
            v->t += dt;
            if (alive < 1e-7f) {
                v->used = 0;
            }
            continue;
        }

        /* key / pedal */
        if (v->key && !g_mon.vel[c][v->note]) {
            v->key = false;
        }
        if (!v->key && !v->released && !g_mon.sustain[c]) {
            v->released = true;
            v->rel_lvl = envelope(v);
        }
        float e;
        if (v->released) {
            e = v->rel_lvl * expf(-v->rel_t / v->tb.release);
            /* a dying piano string keeps dying even after the release */
            if (v->tb.tau > 0 && v->tb.sus == 0) {
                float e2 = envelope(v);
                e = e2 < e ? e2 : e;
            }
            v->rel_t += dt;
        } else {
            e = envelope(v);
        }
        if (e < 0.001f && (v->released || v->t > v->tb.attack + 0.05f)) {
            v->used = 0;   /* below -60 dB */
            continue;
        }
        float p = g * e * e;
        const timbre_t *t = &v->tb;
        float lf0 = 8.781360f + ((float)v->note - 69.0f + (float)g_mon.bend[c] / 100.0f) / 12.0f;   /* log2(440) */
        if (t->noise) {
            add_noise(pw, exp2f(lf0) * 4.0f, 1.5f, p);
        } else {
            /* harder notes are brighter; struck and plucked notes lose their top first */
            float tilt = t->tilt - 0.5f * ((float)v->vel / 127.0f - 0.6f);
            tilt = tilt < 0.3f ? 0.3f : tilt;
            float dull = t->bright > 0 && t->tau > 0 ? expf(-v->t * t->bright / t->tau * 0.6f) : 1.0f;
            float hd = 1.0f;
            float sum = 0, amp[HARM];
            for (int h = 1; h <= t->nh; h++) {
                float a = exp2f(-tilt * s_log2h[h]) * hd;
                if (!(h & 1)) {
                    a *= t->even;
                }
                amp[h - 1] = a;
                sum += a * a;
                hd *= dull;
            }
            for (int h = 1; h <= t->nh; h++) {
                float lf = lf0 + s_log2h[h];
                if (lf > 14.29f) {   /* above 20 kHz */
                    break;
                }
                add_tone(pw, lf, p * amp[h - 1] * amp[h - 1] / sum);
            }
        }
        v->t += dt;
    }
}

void vz_spectrum_draw(int colour, int opt, bool active, bool fresh)
{
    (void)active;
    int64_t now = esp_timer_get_time();
    if (!s_mem) {
        if (!(s_mem = viz_alloc(sizeof(spec_mem_t)))) {
            viz_clear(viz_bg(0));
            return;
        }
        fresh = true;
    }
    if (fresh) {
        memset(s_v, 0, sizeof(s_v));
        memcpy(s_ons, (const void *)g_mon.ons, sizeof(s_ons));   /* only notes from now on */
        for (int b = 0; b < BANDS; b++) {
            s_disp[b] = s_peak[b] = -200.0f;
            s_hold[b] = 0;
        }
        for (int h = 1; h <= HARM; h++) {
            s_log2h[h] = log2f((float)h);
        }
        s_last_us = now;
    }
    float dt = (float)(now - s_last_us) / 1e6f;
    dt = dt < 0 ? 0 : (dt > 0.1f ? 0.1f : dt);
    s_last_us = now;

    float pw[BANDS] = { 0 };
    update(dt, pw);

    /* meter: instant rise, steady fall; peak holds then falls */
    for (int b = 0; b < BANDS; b++) {
        float db = pw[b] > 1e-12f ? 10.0f * log10f(pw[b]) : -200.0f;
        s_disp[b] = db > s_disp[b] ? db : s_disp[b] - FALL_DBS * dt;
        if (s_disp[b] >= s_peak[b]) {
            s_peak[b] = s_disp[b];
            s_hold[b] = PEAK_HOLD;
        } else if (s_hold[b] > 0) {
            s_hold[b] -= dt;
        } else {
            s_peak[b] -= PEAK_DBS * dt;
        }
    }

    /* draw: the original's 16 bars of 24 segments, red peak line, grey base line */
    viz_clear(viz_bg(0));
    uint16_t col = viz_col(colour);
    uint16_t peak_c = colour == 6 ? 0xFFFF : viz_col(6);   /* red, white on red bars */
    int x0 = (VW - (BANDS * PITCH - (PITCH - BAR_W))) / 2;
    int base = TOP_Y + SEGS * (SEG_H + 1);
    gfx_t g = { framebuffer_2, VW, VH };
    for (int b = 0; b < BANDS; b++) {
        int x = x0 + b * PITCH;
        float lvl = (s_disp[b] - (DB_TOP - DB_RANGE)) / DB_RANGE * SEGS;
        int lit = lvl <= 0 ? 0 : (lvl >= SEGS ? SEGS : (int)(lvl + 0.5f));
        for (int s = 0; s < lit; s++) {
            viz_fill(x, base - (s + 1) * (SEG_H + 1), BAR_W, SEG_H, col);
        }
        float pl = (s_peak[b] - (DB_TOP - DB_RANGE)) / DB_RANGE * SEGS;
        if (opt && pl >= 1.0f) {
            int ps = pl >= SEGS ? SEGS : (int)(pl + 0.5f);
            viz_fill(x, base - ps * (SEG_H + 1), BAR_W, 2, peak_c);
        }
        int tw = gfx_text_width(&font_small, s_label[b]);
        gfx_text(&g, &font_small, x + (BAR_W - tw) / 2, base + 2, s_label[b], C_DIM);
    }
    gfx_hline(&g, x0, base, BANDS * PITCH - (PITCH - BAR_W), C_LINE);
}
