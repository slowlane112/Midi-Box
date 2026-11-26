/* Studio > Projects: new project or open one from /sdcard/projects. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sd.h"
#include "song.h"
#include "theme.h"
#include "widgets.h"
#include "dialog.h"
#include "settings.h"
#include "app.h"

char g_project_path[300];

static fs_list_t s_list;
static int s_sel;            /* 0 = New project */
static char s_target[FS_PATH_MAX];

static void load_list(void)
{
    fs_list_free(&s_list);
    if (!fs_is_dir(PROJECTS_DIR)) {
        fs_mkdir(PROJECTS_DIR);
    }
    s_list = fs_list(PROJECTS_DIR, ".mbp");
    /* folders are not projects */
    int k = 0;
    for (int i = 0; i < s_list.count; i++) {
        if (s_list.items[i].is_dir) {
            free(s_list.items[i].name);
        } else {
            s_list.items[k++] = s_list.items[i];
        }
    }
    s_list.count = k;
    if (s_sel > s_list.count) {
        s_sel = s_list.count;
    }
    ui_invalidate();
}

static void enter(void *arg)
{
    (void)arg;
    s_sel = 0;
    load_list();
}

static void leave(void) { fs_list_free(&s_list); }
static void resume(void) { load_list(); }

static void open_studio(void)
{
    ui_push(&scr_studio, NULL);
}

static void on_new_name(const char *name)
{
    char path[FS_PATH_MAX], file[80];
    snprintf(file, sizeof(file), "%.70s.mbp", name);
    path_join(path, sizeof(path), PROJECTS_DIR, file);
    path_unique(path, sizeof(path));
    song_lock();
    song_new(name);
    bool ok = song_save(path);
    song_unlock();
    if (!ok) {
        ui_toast("Could not create the project");
        return;
    }
    snprintf(g_project_path, sizeof(g_project_path), "%s", path);
    open_studio();
}

static void open_selected(void)
{
    char path[FS_PATH_MAX];
    path_join(path, sizeof(path), PROJECTS_DIR, s_list.items[s_sel - 1].name);
    dlg_busy("Opening" UI_ELLIPSIS, 0, 0);
    song_lock();
    bool ok = song_load(path);
    song_unlock();
    dlg_busy_end();
    if (!ok) {
        ui_toast("This project file is damaged");
        return;
    }
    snprintf(g_project_path, sizeof(g_project_path), "%s", path);
    open_studio();
}

static void do_delete(void)
{
    bool ok = fs_delete(s_target);
    /* the exported .mid next to it stays: it may be in a playlist */
    ui_toast(ok ? "Project deleted" : "Could not delete");
    load_list();
}

static bool key(const btn_event_t *e)
{
    int n = s_list.count + 1;
    switch (e->key) {
    case K_UP: s_sel = (s_sel + n - 1) % n; return true;
    case K_DOWN: s_sel = (s_sel + 1) % n; return true;
    case K_S1:
        if (!e->repeat) {
            ui_pop();
        }
        return true;
    case K_S4:
        if (!e->repeat && s_sel > 0) {
            path_join(s_target, sizeof(s_target), PROJECTS_DIR, s_list.items[s_sel - 1].name);
            char msg[96];
            snprintf(msg, sizeof(msg), "Delete %.60s?", s_list.items[s_sel - 1].name);
            dlg_confirm("Delete project", msg, "Delete", do_delete);
        }
        return true;
    case K_S5:
        if (e->repeat) {
            return true;
        }
        if (!sd_mounted()) {
            ui_toast("Insert an SD card first");
        } else if (s_sel == 0) {
            char name[24];
            snprintf(name, sizeof(name), "song_%02d", s_list.count + 1);
            dlg_text("New project name", name, on_new_name);
        } else {
            open_selected();
        }
        return true;
    default:
        return false;
    }
}

static void draw(gfx_t *g)
{
    char right[24];
    snprintf(right, sizeof(right), "%d project%s", s_list.count, s_list.count == 1 ? "" : "s");
    w_status(g, "Studio", right, C_ACC);
    int n = s_list.count + 1;
    int top = w_list_top(s_sel, n, LIST_ROWS);
    for (int r = 0; r < LIST_ROWS && top + r < n; r++) {
        int i = top + r, y = CONTENT_Y + r * ROW_H;
        bool sel = i == s_sel;
        w_row(g, y, ROW_H, sel);
        if (i == 0) {
            gfx_text(g, &font_main, 10, y + 4, "+  New project", sel ? C_ACC : C_TEXT);
            continue;
        }
        const fs_entry_t *it = &s_list.items[i - 1];
        char name[FS_PATH_MAX];
        snprintf(name, sizeof(name), "%s", it->name);
        char *dot = strrchr(name, '.');
        if (dot) {
            *dot = '\0';
        }
        char sz[16];
        fmt_size(sz, sizeof(sz), it->size);
        gfx_icon(g, &icon_music_16x16, 8, y + 3, sel ? C_ACC : C_DIM);
        int w = gfx_text_width(&font_small, sz);
        gfx_text_right(g, &font_small, g->w - 8, y + 6, sz, C_DIM);
        gfx_text_box(g, &font_main, 30, y + 4, g->w - 46 - w, ALIGN_LEFT, name, sel ? C_ACC : C_TEXT);
    }
    w_scrollbar(g, CONTENT_Y, LIST_ROWS * ROW_H, top, LIST_ROWS, n);
    const char *keys[5] = { "Back", NULL, NULL, s_sel ? "Delete" : NULL, s_sel ? "Open" : "New" };
    w_softkeys(g, keys, false);
}

static void draw2(gfx_t *g)
{
    gfx_clear(g, C_BG);
    gfx_text(g, &font_huge, 10, 8, "Studio", C_ACC);
    if (s_sel == 0) {
        gfx_text(g, &font_large, 12, 56, "New project", C_TEXT);
        gfx_text_box(g, &font_small, 12, 82, 200, ALIGN_LEFT, "16 tracks, one instrument each.", C_DIM);
        gfx_text_box(g, &font_small, 12, 96, 200, ALIGN_LEFT, "Record from DIN or Bluetooth,", C_DIM);
        gfx_text_box(g, &font_small, 12, 110, 200, ALIGN_LEFT, "edit, then export a .mid file.", C_DIM);
    } else {
        const fs_entry_t *it = &s_list.items[s_sel - 1];
        char name[FS_PATH_MAX];
        snprintf(name, sizeof(name), "%s", it->name);
        char *dot = strrchr(name, '.');
        if (dot) {
            *dot = '\0';
        }
        gfx_text_box(g, &font_large, 12, 56, 200, ALIGN_LEFT, name, C_TEXT);
        char sz[16];
        fmt_size(sz, sizeof(sz), it->size);
        gfx_text(g, &font_main, 12, 82, sz, C_DIM);
        gfx_text_box(g, &font_small, 12, 110, 200, ALIGN_LEFT, "Open to play, record and edit", C_DIM);
    }
    gfx_vline(g, 222, 10, 122, C_LINE);
    char n[12];
    snprintf(n, sizeof(n), "%d", s_list.count);
    struct { const char *k, *v; } rows[] = {
        { "Projects", n },
        { "Folder", "/projects" },
        { "Exports", "next to the project" },
        { "Sound bank", g_settings.bank ? "MT-32" : "General MIDI" },
    };
    for (int i = 0; i < 4; i++) {
        int y = 10 + i * 20;
        gfx_text(g, &font_small, 232, y, rows[i].k, C_DIM);
        gfx_text_right(g, &font_small, g->w - 8, y, rows[i].v, C_TEXT);
    }
}

const screen_t scr_projects = {
    .name = "projects", .enter = enter, .resume = resume, .leave = leave, .key = key, .draw = draw, .draw2 = draw2,
};
