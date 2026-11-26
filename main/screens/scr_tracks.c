/*
 * Player > Tracks: mute, solo or hide parts while the song plays.
 * A part is one (track, channel) pair, so type-0 files show one row per channel.
 */
#include <stdio.h>
#include <string.h>
#include "player.h"
#include "gm.h"
#include "theme.h"
#include "widgets.h"
#include "viz.h"
#include "app.h"

#define ROWS 8

static int s_sel;
static uint32_t s_last;

static void caption(void)
{
    player_lock();
    const smf_t *s = player_song();
    int shown = 0;
    uint8_t *f = player_part_flags();
    for (int i = 0; i < s->nparts; i++) {
        if (!(f[i] & PART_HIDE)) {
            shown++;
        }
    }
    char c[40];
    if (shown == s->nparts) {
        c[0] = '\0';
    } else {
        snprintf(c, sizeof(c), "%d of %d parts shown", shown, s->nparts);
    }
    player_unlock();
    viz_set_caption(c);
}

static void enter(void *arg)
{
    (void)arg;
    s_sel = 0;
    caption();
}

static void leave(void) { }

static int part_count(void)
{
    player_lock();
    int n = player_song()->nparts;
    player_unlock();
    return n;
}

static void toggle(uint8_t bit)
{
    uint8_t *f = player_part_flags();
    f[s_sel] ^= bit;
    player_parts_changed();
    caption();
}

static void set_all(uint8_t bit, bool on)
{
    uint8_t *f = player_part_flags();
    int n = part_count();
    for (int i = 0; i < n; i++) {
        f[i] = on ? (uint8_t)(f[i] | bit) : (uint8_t)(f[i] & ~bit);
    }
    player_parts_changed();
    caption();
}

static bool key(const btn_event_t *e)
{
    int n = part_count();
    if (n == 0) {
        if (e->key == K_S1) {
            ui_pop();
        }
        return e->key == K_S1;
    }
    if (e->repeat && e->key != K_UP && e->key != K_DOWN) {
        return true;
    }
    switch (e->key) {
    case K_UP: s_sel = (s_sel + n - 1) % n; return true;
    case K_DOWN: s_sel = (s_sel + 1) % n; return true;
    case K_S1:
        if (e->shift) {
            uint8_t *f = player_part_flags();
            memset(f, 0, SMF_MAX_PARTS);
            player_parts_changed();
            caption();
            ui_toast("All parts on");
        } else {
            ui_pop();
        }
        return true;
    case K_S2:
        if (e->shift) {
            set_all(PART_MUTE, true);
            set_all(PART_SOLO, false);
        } else {
            toggle(PART_MUTE);
        }
        return true;
    case K_S3:
        if (e->shift) {
            player_toggle();
        } else {
            /* one solo at a time: soloing a part un-solos the others; again turns it off */
            uint8_t *f = player_part_flags();
            bool was = f[s_sel] & PART_SOLO;
            for (int i = 0; i < n; i++) {
                f[i] &= (uint8_t)~PART_SOLO;
            }
            if (!was) {
                f[s_sel] |= PART_SOLO;
            }
            player_parts_changed();
            caption();
        }
        return true;
    case K_S4:
        if (e->shift) {
            set_all(PART_HIDE, false);
        } else {
            toggle(PART_HIDE);
        }
        return true;
    case K_S5:
        s_sel = e->shift ? (s_sel - ROWS < 0 ? 0 : s_sel - ROWS) : (s_sel + ROWS >= n ? n - 1 : s_sel + ROWS);
        return true;
    default:
        return false;
    }
}

static void tick(uint32_t now)
{
    if (now - s_last > 80) {
        s_last = now;
        ui_invalidate();   /* activity bars */
    }
}

static void draw(gfx_t *g)
{
    player_lock();
    const smf_t *s = player_song();
    const uint8_t *f = player_part_flags();
    int n = s->nparts;
    bool any_solo = false;
    int playing = 0;
    for (int i = 0; i < n; i++) {
        if (f[i] & PART_SOLO) {
            any_solo = true;
        }
    }
    for (int i = 0; i < n; i++) {
        bool sounds = any_solo ? (f[i] & PART_SOLO) : !(f[i] & PART_MUTE);
        playing += sounds;
    }
    char right[32];
    snprintf(right, sizeof(right), "%d of %d playing", playing, n);
    w_status(g, "Tracks", right, C_ACC);

    if (n == 0) {
        gfx_text_box(g, &font_main, 0, 100, g->w, ALIGN_CENTER, "This song has no notes", C_DIM);
    }
    if (s_sel >= n) {
        s_sel = n ? n - 1 : 0;
    }
    int top = w_list_top(s_sel, n, ROWS);
    for (int r = 0; r < ROWS && top + r < n; r++) {
        int i = top + r, y = CONTENT_Y + r * ROW_H;
        const smf_part_t *p = &s->parts[i];
        bool sel = i == s_sel;
        bool solo = f[i] & PART_SOLO;
        bool sounds = any_solo ? solo : !(f[i] & PART_MUTE);
        bool vis = !(f[i] & PART_HIDE);
        w_row(g, y, ROW_H, sel);
        uint16_t tc = !sounds ? C_DIM : (sel ? C_ACC : C_TEXT);
        char num[12], ch[16];
        snprintf(num, sizeof(num), "%d", i + 1);
        snprintf(ch, sizeof(ch), "ch %d", p->channel + 1);
        gfx_text(g, &font_small, 8, y + 6, num, C_DIM);
        gfx_text_box(g, &font_main, 26, y + 4, 92, ALIGN_LEFT, p->name, tc);
        gfx_text(g, &font_small, 122, y + 6, ch, C_DIM);
        int lv = player_part_level(i);
        gfx_fill(g, 160, y + 8, 80, 6, C_LINE);
        if (sounds) {
            gfx_fill(g, 160, y + 8, lv * 80 / 127, 6, C_ACC);
        }
        if (solo) {
            gfx_text(g, &font_small, 245, y + 6, "SOLO", C_AMBER);
        } else if (!sounds) {
            gfx_text(g, &font_small, 245, y + 6, "MUTE", C_REC);
        }
        gfx_icon(g, sounds ? &icon_sound_16x16 : &icon_sound_off_16x16, 276, y + 3, sounds ? tc : C_REC);
        gfx_icon(g, vis ? &icon_eye_16x16 : &icon_eye_off_16x16, 298, y + 3, vis ? tc : C_DIM);
    }
    if (n > 0) {
        const smf_part_t *p = &s->parts[s_sel];
        char info[80];
        snprintf(info, sizeof(info), "ch %d " UI_MIDDOT " %s " UI_MIDDOT " %d/%d", p->channel + 1,
                 p->channel == 9 ? "Drum kit" : gm_program_name(p->program == 0xFF ? 0 : p->program), s_sel + 1, n);
        gfx_text_box(g, &font_small, 8, 198, 304, ALIGN_LEFT, info, C_DIM);
    }
    player_unlock();

    static const char *const normal[5] = { "Back", "Sound", "Solo", "Visual", "Next" };
    static const char *const shifted[5] = { "All on", "Mute all", "@playpause", "Show all", "Prev" };
    w_softkeys2(g, normal, shifted);
}

const screen_t scr_tracks = {
    .name = "tracks", .enter = enter, .leave = leave, .key = key, .tick = tick, .draw = draw,
};
