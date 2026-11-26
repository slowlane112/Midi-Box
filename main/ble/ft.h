/*
 * ft.h - receive files from a PC over Bluetooth (same protocol as v1, so
 * extra/midi_box_transfer.py keeps working):
 *   PC -> box: "META:<size>:<name>", then 240-byte chunks, then "EOF"
 *   box -> PC: "ACCEPTED" / "DECLINED" notifications
 * Files are saved in /sdcard/downloads.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    FT_OFF,          /* not advertising */
    FT_WAITING,      /* advertising, no connection */
    FT_CONNECTED,    /* PC connected, no request yet */
    FT_ASK,          /* PC sent META: accept or decline */
    FT_RECEIVING,
    FT_SAVING,       /* all data in, writing to the card */
    FT_DONE,         /* saved (see ft_status.saved_path) */
    FT_FAILED,
} ft_state_t;

typedef struct {
    ft_state_t state;
    char name[128];
    size_t size;
    size_t received;
    char saved_path[300];
    char error[48];
} ft_status_t;

#define FT_DOWNLOAD_DIR "/sdcard/downloads"

void ft_start(void);         /* start advertising as "MIDI BOX" */
void ft_stop(void);
void ft_accept(void);
void ft_decline(void);
void ft_service(void);       /* call from the UI task: writes finished files to the card */
void ft_get(ft_status_t *out);
void ft_ack(void);           /* DONE / FAILED -> back to CONNECTED or WAITING */
