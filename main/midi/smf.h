/*
 * smf.h - Standard MIDI File (format 0/1) loader.
 *
 * The whole file stays in PSRAM; smf_load() scans it once to build:
 *  - the tempo map (tick <-> microseconds, for time display and seeking)
 *  - the list of "parts": one per (track, channel) pair that plays notes,
 *    with a display name and first program. The Tracks screen mutes / solos parts.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SMF_MAX_PARTS  128
#define SMF_NAME_LEN   28

typedef struct {
    const uint8_t *data;    /* first event (after MTrk header) */
    uint32_t len;
} smf_track_t;

typedef struct {
    uint32_t tick;
    uint32_t us_per_qn;
    uint64_t us;            /* time at this tick */
} smf_tempo_t;

typedef struct {
    uint16_t track;
    uint8_t channel;        /* 0..15 */
    uint8_t program;        /* first program change, 0xFF = none */
    uint32_t notes;         /* note-on count */
    char name[SMF_NAME_LEN];
} smf_part_t;

typedef struct {
    uint8_t *buf;           /* file contents (PSRAM), owned */
    size_t size;
    uint16_t format;
    uint16_t ntracks;
    uint16_t ppq;           /* ticks per quarter note (0 when SMPTE) */
    uint32_t smpte_us_per_tick;
    smf_track_t *tracks;
    smf_tempo_t *tempos;
    int ntempos;
    uint32_t total_ticks;
    uint64_t total_us;
    smf_part_t parts[SMF_MAX_PARTS];
    int nparts;
    int16_t *part_of;       /* [track * 16 + channel] -> part index or -1 */
    char title[48];         /* first track name of track 0, if any */
} smf_t;

typedef enum {
    SMF_OK = 0,
    SMF_ERR_READ,
    SMF_ERR_FORMAT,
    SMF_ERR_MEMORY,
    SMF_ERR_INCOMPLETE = 5,  /* cut short: a track or tracks missing their data (4 is Learn's "no notes") */
} smf_err_t;

smf_err_t smf_load(smf_t *s, const char *path);
void smf_free(smf_t *s);

uint64_t smf_tick_to_us(const smf_t *s, uint32_t tick);
uint32_t smf_us_to_tick(const smf_t *s, uint64_t us);
int smf_part_index(const smf_t *s, uint16_t track, uint8_t ch);

/* low-level helpers shared with the player */
bool smf_read_vlq(const uint8_t **p, const uint8_t *end, uint32_t *out);
uint8_t smf_data_len(uint8_t status);
