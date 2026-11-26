/*
 * settings.h - persistent settings (NVS). Change fields, then call settings_changed();
 * they are written 2 s after the last change so holding a key does not wear the flash.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "sd.h"

typedef struct {
    uint8_t version;
    uint8_t brightness;     /* 0..10 */
    uint8_t accent;         /* index into theme palette */
    uint8_t volume;         /* 0..20 */
    uint8_t bank;           /* 0 = General MIDI, 1 = MT-32 */
    uint8_t thru;           /* DIN -> synth */
    uint8_t viz;            /* screen-2 visualiser (viz.c mode) */
    uint8_t viz_opt[16];    /* per visualiser: button 10 option */
    uint8_t viz_col[16];    /* per visualiser: button 11 colour */
    uint8_t din_port;       /* 0 = back (DIN 1), 1 = front (DIN 2) */
    uint8_t play_mode;      /* player_mode_t */
    uint8_t bt_addr_type;   /* last Bluetooth MIDI device, for one-press reconnect */
    uint8_t bt_addr[6];
    char bt_name[48];
    uint8_t click;          /* studio metronome on */
    uint8_t countin;        /* studio count-in bars (0..2) */
    uint32_t resume_ms;     /* position of the last song */
    char resume_path[FS_PATH_MAX];
    char last_dir[FS_PATH_MAX];
    /* --- added in settings version 5 (firmware 2.1); new fields go below --- */
    uint8_t autoplay;       /* songs start playing as soon as they are loaded */
} settings_t;

extern settings_t g_settings;

void settings_load(void);
void settings_changed(void);
void settings_service(uint32_t now_ms);  /* call from the UI loop */
void settings_save_now(void);
