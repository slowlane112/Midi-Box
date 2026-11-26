/*
 * sd.h - SD card (FAT) on the shared SPI3 bus, plus file helpers.
 * Every helper takes the bus lock around each FatFs call, so the display
 * keeps refreshing during long copies.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SD_ROOT      "/sdcard"
#define FS_PATH_MAX  300

typedef struct {
    char *name;
    bool is_dir;
    uint32_t size;
} fs_entry_t;

typedef struct {
    fs_entry_t *items;
    int count;
    bool ok;
} fs_list_t;

/* return false to cancel a long operation */
typedef bool (*fs_progress_cb)(uint32_t done, uint32_t total, void *ctx);

bool sd_init(void);
bool sd_mounted(void);
bool sd_space(uint64_t *total, uint64_t *free_bytes);

/* List a folder, folders first then files, case-insensitive A-Z. ext: e.g. ".mid" or NULL for all. */
fs_list_t fs_list(const char *dir, const char *ext);
void fs_list_free(fs_list_t *l);

/* Read a whole file into PSRAM. Caller frees with free(). */
uint8_t *fs_read_all(const char *path, size_t *len);
/* Read up to n bytes from the start of a file. Returns bytes read. */
size_t fs_read_head(const char *path, void *buf, size_t n);
bool fs_write_all(const char *path, const void *data, size_t len);

bool fs_exists(const char *path);
bool fs_is_dir(const char *path);
bool fs_mkdir(const char *path);
bool fs_rename(const char *from, const char *to);
bool fs_delete(const char *path);                       /* files and folders (recursive) */
bool fs_copy(const char *from, const char *to, fs_progress_cb cb, void *ctx);  /* files and folders */

/* path helpers */
void path_join(char *out, size_t n, const char *dir, const char *name);
void path_parent(char *path);                           /* in place: /a/b/c -> /a/b */
const char *path_name(const char *path);                /* pointer to last segment */
bool path_has_ext(const char *name, const char *ext);   /* case-insensitive */
/* Make dst unique by adding " (2)", " (3)"... before the extension if it exists. */
void path_unique(char *path, size_t n);
