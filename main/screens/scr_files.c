/*
 * Player (the browser): folders and .mid files only; play songs, copy / move / rename / delete / new folder.
 * Opens in the folder last played from; 8 on a .mid opens it in the now-playing screen.
 * Copy and Move put the item on a clipboard; go to a folder and press Paste (button 7).
 */
#include <stdio.h>
#include <string.h>
#include "sd.h"
#include "board.h"
#include "settings.h"
#include "theme.h"
#include "widgets.h"
#include "dialog.h"
#include "app.h"

static char s_dir[FS_PATH_MAX];
static fs_list_t s_list;
static int s_sel;              /* index into rows: row 0 is ".." when not at root */
static char s_space[32];
static uint64_t s_total, s_free;

static struct {
    bool active;
    bool move;
    char path[FS_PATH_MAX];
} s_clip;

/* file being acted on by a dialog callback */
static char s_target[FS_PATH_MAX];
static bool s_target_dir;

/* screen-2 details of the selected file */
static struct {
    char path[FS_PATH_MAX];
    int midi_format;           /* -1 = not MIDI */
    int midi_tracks;
} s_info;

static bool at_root(void) { return strcmp(s_dir, SD_ROOT) == 0; }
static int has_up(void) { return at_root() ? 0 : 1; }
static int row_count(void) { return s_list.count + has_up(); }

static const fs_entry_t *sel_entry(void)
{
    int i = s_sel - has_up();
    return (i >= 0 && i < s_list.count) ? &s_list.items[i] : NULL;
}

static void sel_path(char *out, size_t n)
{
    const fs_entry_t *e = sel_entry();
    if (e) {
        path_join(out, n, s_dir, e->name);
    } else {
        out[0] = '\0';
    }
}

static void refresh_space(void)
{
    s_total = s_free = 0;
    if (sd_space(&s_total, &s_free)) {
        char f[16];
        fmt_size(f, sizeof(f), s_free);
        snprintf(s_space, sizeof(s_space), "%s free", f);
    } else {
        s_space[0] = '\0';
    }
}

/* reload the folder; keep the selection on 'keep' if given */
static void load(const char *keep)
{
    fs_list_free(&s_list);
    s_list = fs_list(s_dir, ".mid");   /* folders and MIDI files only */
    s_sel = 0;
    if (keep) {
        for (int i = 0; i < s_list.count; i++) {
            if (!strcmp(s_list.items[i].name, keep)) {
                s_sel = i + has_up();
                break;
            }
        }
    } else if (s_list.count > 0) {
        s_sel = has_up();
    }
    s_info.path[0] = '\0';
    refresh_space();
    ui_invalidate();
}

static void go_up(void)
{
    char child[FS_PATH_MAX];
    snprintf(child, sizeof(child), "%s", path_name(s_dir));
    path_parent(s_dir);
    load(child);
}

static void open_dir(const char *name)
{
    char p[FS_PATH_MAX];
    path_join(p, sizeof(p), s_dir, name);
    snprintf(s_dir, sizeof(s_dir), "%s", p);
    load(NULL);
}

static void (*s_on_pick)(const char *path);
static const char *s_pick_title;

static void play_selected(void)
{
    static char path[FS_PATH_MAX];
    static player_args_t pa;
    sel_path(path, sizeof(path));
    if (s_on_pick) {
        void (*cb)(const char *) = s_on_pick;
        snprintf(g_settings.last_dir, sizeof(g_settings.last_dir), "%s", s_dir);
        settings_changed();
        ui_pop();
        cb(path);
        return;
    }
    snprintf(g_settings.last_dir, sizeof(g_settings.last_dir), "%s", s_dir);
    settings_changed();
    pa.path = path;
    pa.start_ms = 0;
    ui_push(&scr_player, &pa);
}

/* ------------------------------------------------------------------ operations */

static bool copy_progress(uint32_t done, uint32_t total, void *ctx)
{
    (void)ctx;
    static uint32_t last;
    uint32_t now = ui_now();
    if (now - last > 150) {
        last = now;
        dlg_busy("Copying" UI_ELLIPSIS, done, total);
    }
    return true;
}

static void paste(void)
{
    if (!s_clip.active) {
        return;
    }
    if (!fs_exists(s_clip.path)) {
        ui_toast("The item no longer exists");
        s_clip.active = false;
        return;
    }
    char dst[FS_PATH_MAX];
    path_join(dst, sizeof(dst), s_dir, path_name(s_clip.path));
    bool ok;
    if (s_clip.move) {
        if (!strcmp(dst, s_clip.path)) {
            ui_toast("It is already in this folder");
            return;
        }
        if (fs_exists(dst)) {
            ui_toast("A file with that name is here");
            return;
        }
        ok = fs_rename(s_clip.path, dst);
        if (ok) {
            s_clip.active = false;
        }
    } else {
        path_unique(dst, sizeof(dst));
        dlg_busy("Copying" UI_ELLIPSIS, 0, 0);
        ok = fs_copy(s_clip.path, dst, copy_progress, NULL);
        dlg_busy_end();
    }
    ui_toast(ok ? (s_clip.move ? "Moved" : "Copied") : "Failed. Is the card full?");
    load(ok ? path_name(dst) : NULL);
}

static void do_delete(void)
{
    dlg_busy("Deleting" UI_ELLIPSIS, 0, 0);
    bool ok = fs_delete(s_target);
    dlg_busy_end();
    ui_toast(ok ? "Deleted" : "Could not delete");
    int keep = s_sel;
    load(NULL);
    s_sel = keep < row_count() ? keep : row_count() - 1;
    if (s_sel < 0) {
        s_sel = 0;
    }
}

static void do_rename(const char *name)
{
    char base[FS_PATH_MAX], dst[FS_PATH_MAX], newname[FS_PATH_MAX];
    snprintf(base, sizeof(base), "%s", s_target);
    path_parent(base);
    /* files keep their extension; you only edit the name */
    const char *ext = s_target_dir ? NULL : strrchr(path_name(s_target), '.');
    snprintf(newname, sizeof(newname), "%s%s", name, ext ? ext : "");
    path_join(dst, sizeof(dst), base, newname);
    if (!strcmp(dst, s_target)) {
        return;
    }
    if (fs_exists(dst)) {
        ui_toast("That name is already used");
        return;
    }
    bool ok = fs_rename(s_target, dst);
    ui_toast(ok ? "Renamed" : "Could not rename");
    load(ok ? newname : NULL);
}

static void do_mkdir(const char *name)
{
    char p[FS_PATH_MAX];
    path_join(p, sizeof(p), s_dir, name);
    if (fs_exists(p)) {
        ui_toast("That name is already used");
        return;
    }
    bool ok = fs_mkdir(p);
    ui_toast(ok ? "Folder created" : "Could not create folder");
    load(ok ? name : NULL);
}

/* ------------------------------------------------------------------ menu */

typedef enum { A_PLAY, A_OPEN, A_COPY, A_MOVE, A_RENAME, A_DELETE, A_MKDIR, A_PASTE } action_t;
static const char *s_menu_txt[8];
static action_t s_menu_act[8];
static int s_menu_n;

static void menu_add(const char *t, action_t a)
{
    s_menu_txt[s_menu_n] = t;
    s_menu_act[s_menu_n] = a;
    s_menu_n++;
}

static void on_menu(int i)
{
    const fs_entry_t *e = sel_entry();
    switch (s_menu_act[i]) {
    case A_PLAY:
        play_selected();
        break;
    case A_OPEN:
        if (e) {
            open_dir(e->name);
        }
        break;
    case A_COPY:
    case A_MOVE:
        sel_path(s_clip.path, sizeof(s_clip.path));
        s_clip.move = s_menu_act[i] == A_MOVE;
        s_clip.active = true;
        ui_toast("Open a folder, then press Paste");
        break;
    case A_RENAME: {
        if (!e) {
            break;
        }
        sel_path(s_target, sizeof(s_target));
        s_target_dir = e->is_dir;
        char name[FS_PATH_MAX];
        snprintf(name, sizeof(name), "%s", e->name);
        char *dot = e->is_dir ? NULL : strrchr(name, '.');
        if (dot && dot != name) {
            *dot = '\0';
        }
        dlg_text(e->is_dir ? "Rename folder" : "Rename file", name, do_rename);
        break;
    }
    case A_DELETE: {
        if (!e) {
            break;
        }
        sel_path(s_target, sizeof(s_target));
        char msg[96];
        if (e->is_dir) {
            snprintf(msg, sizeof(msg), "Delete %.40s and everything in it?", e->name);
        } else {
            snprintf(msg, sizeof(msg), "Delete %.60s?", e->name);
        }
        dlg_confirm(e->is_dir ? "Delete folder" : "Delete file", msg, "Delete", do_delete);
        break;
    }
    case A_MKDIR:
        dlg_text("New folder", "New folder", do_mkdir);
        break;
    case A_PASTE:
        paste();
        break;
    }
}

static void open_menu(void)
{
    const fs_entry_t *e = sel_entry();
    s_menu_n = 0;
    if (e && !e->is_dir && path_has_ext(e->name, ".mid")) {
        menu_add("Play", A_PLAY);
    }
    if (e && e->is_dir) {
        menu_add("Open", A_OPEN);
    }
    if (s_clip.active) {
        menu_add(s_clip.move ? "Move here" : "Paste here", A_PASTE);
    }
    if (e) {
        menu_add("Copy", A_COPY);
        menu_add("Move", A_MOVE);
        menu_add("Rename", A_RENAME);
        menu_add("Delete", A_DELETE);
    }
    menu_add("New folder", A_MKDIR);
    dlg_menu(s_menu_txt, s_menu_n, 0, on_menu);
}

/* ------------------------------------------------------------------ screen */

static void enter(void *arg)
{
    const files_args_t *a = arg;
    s_on_pick = a ? a->on_pick : NULL;
    s_pick_title = a ? a->title : NULL;
    const char *dir = (a && a->dir && a->dir[0]) ? a->dir : SD_ROOT;
    snprintf(s_dir, sizeof(s_dir), "%s", dir);
    if (!fs_is_dir(s_dir)) {
        snprintf(s_dir, sizeof(s_dir), "%s", SD_ROOT);
    }
    load(NULL);
}

static void leave(void)
{
    fs_list_free(&s_list);
}

static void move(int d)
{
    int n = row_count();
    if (n == 0) {
        return;
    }
    int s = s_sel + d;
    if (d == 1 || d == -1) {
        s = (s + n) % n;          /* single steps wrap around */
    } else {
        s = s < 0 ? 0 : (s >= n ? n - 1 : s);
    }
    s_sel = s;
}

static bool key(const btn_event_t *e)
{
    switch (e->key) {
    case K_UP: move(e->shift ? -LIST_ROWS : -1); return true;
    case K_DOWN: move(e->shift ? LIST_ROWS : 1); return true;
    case K_S2: move(-LIST_ROWS); return true;
    case K_S3: move(LIST_ROWS); return true;
    case K_S1:
        if (e->repeat) {
            return true;
        }
        if (e->shift) {
            snprintf(s_dir, sizeof(s_dir), "%s", SD_ROOT);
            load(NULL);
        } else if (at_root()) {
            ui_pop();
        } else {
            go_up();
        }
        return true;
    case K_S4:
        if (e->repeat) {
            return true;
        }
        if (s_clip.active && e->shift) {
            s_clip.active = false;
            ui_toast("Clipboard cleared");
        } else if (s_clip.active) {
            paste();
        } else {
            open_menu();
        }
        return true;
    case K_S5: {
        if (e->repeat) {
            return true;
        }
        if (e->shift) {
            open_menu();
            return true;
        }
        if (has_up() && s_sel == 0) {
            go_up();
            return true;
        }
        const fs_entry_t *it = sel_entry();
        if (!it) {
            return true;
        }
        if (it->is_dir) {
            open_dir(it->name);
        } else if (path_has_ext(it->name, ".mid")) {
            play_selected();
        } else {
            open_menu();
        }
        return true;
    }
    default:
        return false;
    }
}

static void draw(gfx_t *g)
{
    const char *rel = s_dir + strlen(SD_ROOT);
    char right[FS_PATH_MAX + 40];
    if (s_clip.active) {
        snprintf(right, sizeof(right), "%s: %s", s_clip.move ? "Move" : "Paste", path_name(s_clip.path));
    } else {
        snprintf(right, sizeof(right), "%s " UI_MIDDOT " %s", rel[0] ? rel : "/", s_space);
    }
    w_status(g, s_pick_title ? s_pick_title : "Player", right, s_clip.active ? C_AMBER : C_ACC);

    if (!sd_mounted()) {
        gfx_text_box(g, &font_large, 0, 80, g->w, ALIGN_CENTER, "No SD card", C_TEXT);
        gfx_text_box(g, &font_main, 0, 106, g->w, ALIGN_CENTER, "Insert a card and restart", C_DIM);
    } else {
        int n = row_count();
        int top = w_list_top(s_sel, n, LIST_ROWS);
        for (int r = 0; r < LIST_ROWS && top + r < n; r++) {
            int i = top + r, y = CONTENT_Y + r * ROW_H;
            bool sel = i == s_sel;
            w_row(g, y, ROW_H, sel);
            if (has_up() && i == 0) {
                gfx_icon(g, &icon_folder_16x16, 8, y + 3, C_AMBER);
                gfx_text(g, &font_main, 30, y + 4, "..", sel ? C_ACC : C_TEXT);
                continue;
            }
            const fs_entry_t *e = &s_list.items[i - has_up()];
            bool song = !e->is_dir && path_has_ext(e->name, ".mid");
            gfx_icon(g, e->is_dir ? &icon_folder_16x16 : (song ? &icon_music_16x16 : &icon_file_16x16), 8, y + 3,
                     e->is_dir ? C_AMBER : (song ? C_ACC : C_DIM));
            int szw = 0;
            if (!e->is_dir) {
                char sz[16];
                fmt_size(sz, sizeof(sz), e->size);
                szw = gfx_text_width(&font_small, sz);
                gfx_text_right(g, &font_small, g->w - 8, y + 6, sz, C_DIM);
            }
            gfx_text_box(g, &font_main, 30, y + 4, g->w - 30 - szw - 16, ALIGN_LEFT, e->name, sel ? C_ACC : C_TEXT);
        }
        w_scrollbar(g, CONTENT_Y, LIST_ROWS * ROW_H, top, LIST_ROWS, n);
        if (n == 0) {
            gfx_text_box(g, &font_main, 0, 100, g->w, ALIGN_CENTER, "No MIDI files here", C_DIM);
        }
    }
    const char *normal[5] = { "Back", "Page up", "Page dn", s_clip.active ? (s_clip.move ? "Move" : "Paste") : "Menu", "Open" };
    const char *shifted[5] = { "Root", NULL, NULL, s_clip.active ? "Cancel" : NULL, "Menu" };
    w_softkeys2(g, normal, shifted);
}

/* peek at a MIDI header for screen 2 */
static void update_info(void)
{
    char p[FS_PATH_MAX];
    sel_path(p, sizeof(p));
    if (!strcmp(p, s_info.path)) {
        return;
    }
    snprintf(s_info.path, sizeof(s_info.path), "%s", p);
    s_info.midi_format = -1;
    s_info.midi_tracks = 0;
    const fs_entry_t *e = sel_entry();
    if (!e || e->is_dir || !path_has_ext(e->name, ".mid")) {
        return;
    }
    uint8_t b[14];
    if (fs_read_head(p, b, sizeof(b)) == sizeof(b) && !memcmp(b, "MThd", 4)) {
        s_info.midi_format = (b[8] << 8) | b[9];
        s_info.midi_tracks = (b[10] << 8) | b[11];
    }
}

/* screen 2: the highlighted (or clipboard) name scrolls when it doesn't fit */
static w_scroll_t s_name2;

static const char *name2(void)
{
    if (s_clip.active) {
        return path_name(s_clip.path);
    }
    const fs_entry_t *e = sel_entry();
    return e ? e->name : NULL;
}

static void tick(uint32_t now)
{
    const char *n = name2();
    if (n && w_scroll_tick(&s_name2, &font_large, n, LCD2_W - 12, now)) {
        ui_invalidate();
    }
}

static void draw2(gfx_t *g)
{
    gfx_clear(g, C_BG);
    const fs_entry_t *e = sel_entry();
    w_header2(g, s_clip.active ? "Clipboard" : "Selected", NULL, C_DIM);
    if (s_clip.active) {
        w_text_scroll(g, &font_large, 6, 18, g->w - 12, &s_name2, path_name(s_clip.path), C_AMBER);
        gfx_text_box(g, &font_main, 6, 48, g->w - 12, ALIGN_LEFT,
                     s_clip.move ? "Open a folder, then press Move (7)" : "Open a folder, then press Paste (7)", C_TEXT);
        gfx_text(g, &font_small, 6, 70, "Shift+7 clears the clipboard", C_DIM);
    } else if (e) {
        w_text_scroll(g, &font_large, 6, 18, g->w - 12, &s_name2, e->name, C_TEXT);
        char a[24], b[24];
        if (e->is_dir) {
            w_stat(g, 6, 44, 100, "Type", "Folder", C_TEXT);
        } else {
            update_info();
            fmt_size(a, sizeof(a), e->size);
            w_stat(g, 6, 44, 100, "Size", a, C_TEXT);
            if (s_info.midi_format >= 0) {
                snprintf(a, sizeof(a), "MIDI %d", s_info.midi_format);
                snprintf(b, sizeof(b), "%d", s_info.midi_tracks);
                w_stat(g, 111, 44, 100, "Type", a, C_TEXT);
                w_stat(g, 216, 44, 100, "Tracks", b, C_TEXT);
            }
        }
    } else {
        gfx_text(g, &font_main, 6, 24, sd_mounted() ? "Nothing selected" : "No SD card", C_DIM);
    }
    if (s_total) {
        char u[16], t[16], line[64];
        fmt_size(u, sizeof(u), s_total - s_free);
        fmt_size(t, sizeof(t), s_total);
        w_progress(g, 6, 112, g->w - 12, 5, (uint32_t)((s_total - s_free) >> 10), (uint32_t)(s_total >> 10), C_ACC);
        snprintf(line, sizeof(line), "SD card " UI_MIDDOT " %s used of %s", u, t);
        gfx_text(g, &font_small, 6, 122, line, C_DIM);
    }
}

static void resume(void) { ui_invalidate(); }

const screen_t scr_files = {
    .name = "files", .enter = enter, .resume = resume, .leave = leave, .key = key, .tick = tick, .draw = draw, .draw2 = draw2,
};
