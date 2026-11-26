/* app.h - all screens. */
#pragma once
#include "ui.h"

#define MIDIBOX_VERSION "2.1.1"


typedef struct {
    const char *dir;          /* start folder, NULL = root */
    void (*on_pick)(const char *path);   /* set: choosing a .mid calls this instead of playing it */
    const char *title;        /* heading while picking */
} files_args_t;

typedef struct {
    const char *path;         /* song to play */
    uint32_t start_ms;
} player_args_t;

extern const screen_t scr_home;
extern const screen_t scr_files;
extern const screen_t scr_player;
extern const screen_t scr_tracks;
extern const screen_t scr_livein;
extern const screen_t scr_settings;
extern const screen_t scr_soon;      /* arg: const char *app name */
extern const screen_t scr_bt;
extern const screen_t scr_learn;
extern const screen_t scr_lrange;
extern const screen_t scr_lsongs;
extern const screen_t scr_lsetup;
extern const screen_t scr_lplay;
extern const screen_t scr_lresults;
extern const screen_t scr_lex;
extern const screen_t scr_lexplay;
extern const screen_t scr_lnr;
extern const screen_t scr_lear;
extern const screen_t scr_notes;
extern const screen_t scr_transfer;
extern const screen_t scr_projects;
extern const screen_t scr_studio;
extern const screen_t scr_record;
extern const screen_t scr_edit;

/* Studio state shared by its screens */
#define PROJECTS_DIR "/sdcard/projects"
extern char g_project_path[300];
extern int g_studio_track;           /* selected / armed track */
extern uint32_t g_studio_playhead;   /* ticks */
void studio_select_track(int t);
