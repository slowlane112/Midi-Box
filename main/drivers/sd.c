#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "board.h"
#include "bus.h"
#include "sd.h"

static const char *TAG = "sd";
static sdmmc_card_t *s_card;

#define COPY_CHUNK (16 * 1024)

bool sd_init(void)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BOARD_SPI3_HOST;
    host.max_freq_khz = BOARD_SD_KHZ;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = BOARD_SD_CS;
    slot.host_id = BOARD_SPI3_HOST;

    esp_vfs_fat_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 6,
        .allocation_unit_size = 0,
    };

    bus_lock();
    esp_err_t err = esp_vfs_fat_sdspi_mount(SD_ROOT, &host, &slot, &mount, &s_card);
    bus_unlock();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
        s_card = NULL;
        return false;
    }
    ESP_LOGI(TAG, "mounted %s", s_card->cid.name);
    return true;
}

bool sd_mounted(void) { return s_card != NULL; }

bool sd_space(uint64_t *total, uint64_t *free_bytes)
{
    if (!s_card) {
        return false;
    }
    bus_lock();
    esp_err_t err = esp_vfs_fat_info(SD_ROOT, total, free_bytes);
    bus_unlock();
    return err == ESP_OK;
}

/* ------------------------------------------------------------ paths */

void path_join(char *out, size_t n, const char *dir, const char *name)
{
    size_t dl = strlen(dir);
    if (dl > 0 && dir[dl - 1] == '/') {
        snprintf(out, n, "%s%s", dir, name);
    } else {
        snprintf(out, n, "%s/%s", dir, name);
    }
}

void path_parent(char *path)
{
    char *s = strrchr(path, '/');
    if (!s) {
        return;
    }
    if (s == path) {
        s[1] = '\0';
    } else {
        *s = '\0';
    }
    if (strlen(path) < strlen(SD_ROOT)) {
        strcpy(path, SD_ROOT);
    }
}

const char *path_name(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

bool path_has_ext(const char *name, const char *ext)
{
    size_t nl = strlen(name), el = strlen(ext);
    return nl >= el && strcasecmp(name + nl - el, ext) == 0;
}

void path_unique(char *path, size_t n)
{
    if (!fs_exists(path)) {
        return;
    }
    char base[FS_PATH_MAX], ext[32] = "";
    snprintf(base, sizeof(base), "%s", path);
    char *dot = strrchr(base, '.');
    char *slash = strrchr(base, '/');
    if (dot && (!slash || dot > slash) && strlen(dot) < sizeof(ext)) {
        snprintf(ext, sizeof(ext), "%s", dot);
        *dot = '\0';
    }
    for (int i = 2; i < 1000; i++) {
        snprintf(path, n, "%.*s (%d)%s", (int)(n - 40), base, i, ext);
        if (!fs_exists(path)) {
            return;
        }
    }
}

/* ------------------------------------------------------------ listing */

static int entry_cmp(const void *a, const void *b)
{
    const fs_entry_t *x = a, *y = b;
    if (x->is_dir != y->is_dir) {
        return x->is_dir ? -1 : 1;
    }
    return strcasecmp(x->name, y->name);
}

fs_list_t fs_list(const char *dir, const char *ext)
{
    fs_list_t l = { 0 };
    if (!s_card) {
        return l;
    }
    bus_lock();
    DIR *d = opendir(dir);
    bus_unlock();
    if (!d) {
        return l;
    }
    int cap = 64;
    l.items = heap_caps_malloc(cap * sizeof(fs_entry_t), MALLOC_CAP_SPIRAM);
    l.ok = l.items != NULL;
    while (l.ok) {
        bus_lock();
        struct dirent *e = readdir(d);
        bus_unlock();
        if (!e) {
            break;
        }
        if (e->d_name[0] == '.') {
            continue;  /* hidden files, "." and ".." */
        }
        bool is_dir = e->d_type == DT_DIR;
        if (!is_dir && ext && !path_has_ext(e->d_name, ext)) {
            continue;
        }
        if (l.count == cap) {
            cap *= 2;
            fs_entry_t *n = heap_caps_realloc(l.items, cap * sizeof(fs_entry_t), MALLOC_CAP_SPIRAM);
            if (!n) {
                break;
            }
            l.items = n;
        }
        fs_entry_t *it = &l.items[l.count];
        it->name = strdup(e->d_name);
        it->is_dir = is_dir;
        it->size = 0;
        if (!it->name) {
            break;
        }
        if (!is_dir) {
            char p[FS_PATH_MAX];
            struct stat st;
            path_join(p, sizeof(p), dir, e->d_name);
            bus_lock();
            if (stat(p, &st) == 0) {
                it->size = (uint32_t)st.st_size;
            }
            bus_unlock();
        }
        l.count++;
    }
    bus_lock();
    closedir(d);
    bus_unlock();
    if (l.count > 1) {
        qsort(l.items, l.count, sizeof(fs_entry_t), entry_cmp);
    }
    return l;
}

void fs_list_free(fs_list_t *l)
{
    for (int i = 0; i < l->count; i++) {
        free(l->items[i].name);
    }
    free(l->items);
    l->items = NULL;
    l->count = 0;
}

/* ------------------------------------------------------------ files */

uint8_t *fs_read_all(const char *path, size_t *len)
{
    *len = 0;
    bus_lock();
    FILE *f = fopen(path, "rb");
    long sz = -1;
    if (f) {
        fseek(f, 0, SEEK_END);
        sz = ftell(f);
        fseek(f, 0, SEEK_SET);
    }
    bus_unlock();
    if (!f || sz <= 0) {
        if (f) {
            bus_lock(); fclose(f); bus_unlock();
        }
        return NULL;
    }
    uint8_t *buf = heap_caps_malloc((size_t)sz, MALLOC_CAP_SPIRAM);
    size_t got = 0;
    while (buf && got < (size_t)sz) {
        size_t want = (size_t)sz - got;
        if (want > COPY_CHUNK) {
            want = COPY_CHUNK;
        }
        bus_lock();
        size_t r = fread(buf + got, 1, want, f);
        bus_unlock();
        if (r == 0) {
            break;
        }
        got += r;
        vTaskDelay(1);
    }
    bus_lock();
    fclose(f);
    bus_unlock();
    if (!buf || got != (size_t)sz) {
        free(buf);
        return NULL;
    }
    *len = got;
    return buf;
}

size_t fs_read_head(const char *path, void *buf, size_t n)
{
    bus_lock();
    FILE *f = fopen(path, "rb");
    size_t r = f ? fread(buf, 1, n, f) : 0;
    if (f) {
        fclose(f);
    }
    bus_unlock();
    return r;
}

bool fs_write_all(const char *path, const void *data, size_t len)
{
    bus_lock();
    FILE *f = fopen(path, "wb");
    bus_unlock();
    if (!f) {
        return false;
    }
    const uint8_t *p = data;
    size_t done = 0;
    bool ok = true;
    while (done < len) {
        size_t n = len - done > COPY_CHUNK ? COPY_CHUNK : len - done;
        bus_lock();
        size_t w = fwrite(p + done, 1, n, f);
        bus_unlock();
        if (w != n) {
            ok = false;
            break;
        }
        done += n;
    }
    bus_lock();
    ok = (fclose(f) == 0) && ok;
    bus_unlock();
    return ok;
}

bool fs_exists(const char *path)
{
    struct stat st;
    bus_lock();
    bool r = stat(path, &st) == 0;
    bus_unlock();
    return r;
}

bool fs_is_dir(const char *path)
{
    struct stat st;
    bus_lock();
    bool r = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
    bus_unlock();
    return r;
}

bool fs_mkdir(const char *path)
{
    bus_lock();
    bool r = mkdir(path, 0775) == 0;
    bus_unlock();
    return r;
}

bool fs_rename(const char *from, const char *to)
{
    bus_lock();
    bool r = rename(from, to) == 0;
    bus_unlock();
    return r;
}

bool fs_delete(const char *path)
{
    if (!fs_is_dir(path)) {
        bus_lock();
        bool r = unlink(path) == 0;
        bus_unlock();
        return r;
    }
    fs_list_t l = fs_list(path, NULL);
    bool ok = true;
    for (int i = 0; i < l.count && ok; i++) {
        char p[FS_PATH_MAX];
        path_join(p, sizeof(p), path, l.items[i].name);
        ok = fs_delete(p);
    }
    fs_list_free(&l);
    if (ok) {
        bus_lock();
        ok = rmdir(path) == 0;
        bus_unlock();
    }
    return ok;
}

static bool copy_file(const char *from, const char *to, uint8_t *buf, fs_progress_cb cb, void *ctx)
{
    bus_lock();
    FILE *in = fopen(from, "rb");
    FILE *out = in ? fopen(to, "wb") : NULL;
    long total = 0;
    if (in) {
        fseek(in, 0, SEEK_END);
        total = ftell(in);
        fseek(in, 0, SEEK_SET);
    }
    bus_unlock();
    bool ok = in && out;
    uint32_t done = 0;
    while (ok) {
        bus_lock();
        size_t r = fread(buf, 1, COPY_CHUNK, in);
        size_t w = r ? fwrite(buf, 1, r, out) : 0;
        bus_unlock();
        if (r == 0) {
            break;
        }
        if (w != r) {
            ok = false;
            break;
        }
        done += (uint32_t)r;
        vTaskDelay(1);   /* let the display and idle task run during long copies */
        if (cb && !cb(done, (uint32_t)total, ctx)) {
            ok = false;
        }
    }
    bus_lock();
    if (in) {
        fclose(in);
    }
    if (out && fclose(out) != 0) {
        ok = false;
    }
    if (!ok && out) {
        unlink(to);
    }
    bus_unlock();
    return ok;
}

static bool copy_any(const char *from, const char *to, uint8_t *buf, fs_progress_cb cb, void *ctx)
{
    if (!fs_is_dir(from)) {
        return copy_file(from, to, buf, cb, ctx);
    }
    if (!fs_mkdir(to)) {
        return false;
    }
    fs_list_t l = fs_list(from, NULL);
    bool ok = l.ok || l.count == 0;
    for (int i = 0; i < l.count && ok; i++) {
        char a[FS_PATH_MAX], b[FS_PATH_MAX];
        path_join(a, sizeof(a), from, l.items[i].name);
        path_join(b, sizeof(b), to, l.items[i].name);
        ok = copy_any(a, b, buf, cb, ctx);
    }
    fs_list_free(&l);
    return ok;
}

bool fs_copy(const char *from, const char *to, fs_progress_cb cb, void *ctx)
{
    /* refuse to copy a folder into itself */
    size_t fl = strlen(from);
    if (strncmp(to, from, fl) == 0 && (to[fl] == '/' || to[fl] == '\0')) {
        return false;
    }
    uint8_t *buf = heap_caps_malloc(COPY_CHUNK, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return false;
    }
    bool ok = copy_any(from, to, buf, cb, ctx);
    free(buf);
    return ok;
}
