#include <string.h>
#include <stddef.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "settings.h"

#define SETTINGS_VERSION 5
/* version 4 (firmware 2.0) is the same layout up to last_dir: kept on upgrade */
#define V4_SIZE offsetof(settings_t, autoplay)
#define SAVE_DELAY_MS    2000

settings_t g_settings;
static bool s_dirty;
static uint32_t s_dirty_at;

/* visualisers added in 2.1: kaleidoscope 8 slices on purple, synthwave medium speed,
 * spirograph with trails */
static void new_viz_defaults(void)
{
    g_settings.viz_opt[11] = 1;
    g_settings.viz_col[11] = 3;   /* background: Purple */
    g_settings.viz_opt[12] = 1;
    g_settings.viz_opt[13] = 1;
}

static void defaults(void)
{
    memset(&g_settings, 0, sizeof(g_settings));
    g_settings.version = SETTINGS_VERSION;
    g_settings.brightness = 8;
    g_settings.volume = 16;
    g_settings.thru = 1;
    g_settings.click = 1;
    g_settings.countin = 1;
    /* visualiser defaults (index = viz.c order): piano note names on, spectrum peaks on,
     * piano roll medium speed, visualiser one blobs on, fireworks trails on */
    g_settings.viz_opt[0] = 1;
    g_settings.viz_opt[1] = 1;
    g_settings.viz_opt[4] = 1;
    g_settings.viz_opt[5] = 1;
    g_settings.viz_opt[10] = 1;   /* fireworks trails */
    new_viz_defaults();
    strcpy(g_settings.last_dir, SD_ROOT);
}

void settings_load(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    defaults();
    nvs_handle_t h;
    if (nvs_open("midibox", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    settings_t tmp;
    size_t len = sizeof(tmp);
    memset(&tmp, 0, sizeof(tmp));
    esp_err_t r = nvs_get_blob(h, "cfg", &tmp, &len);
    bool ok = r == ESP_OK && len == sizeof(tmp) && tmp.version == SETTINGS_VERSION;
    if (!ok && r == ESP_OK && tmp.version == 4 && len >= V4_SIZE && len < sizeof(tmp)) {
        /* upgrade from 2.0: keep everything, new fields take their defaults */
        settings_t def = g_settings;
        memcpy((uint8_t *)&def, &tmp, V4_SIZE);
        def.version = SETTINGS_VERSION;
        tmp = def;
        g_settings = tmp;
        new_viz_defaults();
        tmp = g_settings;
        ok = true;
        settings_changed();
    }
    if (ok) {
        g_settings = tmp;
        g_settings.resume_path[sizeof(g_settings.resume_path) - 1] = '\0';
        g_settings.last_dir[sizeof(g_settings.last_dir) - 1] = '\0';
        g_settings.bt_name[sizeof(g_settings.bt_name) - 1] = '\0';
    }
    nvs_close(h);
}

void settings_save_now(void)
{
    nvs_handle_t h;
    if (nvs_open("midibox", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (nvs_set_blob(h, "cfg", &g_settings, sizeof(g_settings)) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
    s_dirty = false;
}

static uint32_t s_now;

void settings_changed(void)
{
    s_dirty = true;
    s_dirty_at = s_now;
}

void settings_service(uint32_t now_ms)
{
    s_now = now_ms;
    if (s_dirty && now_ms - s_dirty_at >= SAVE_DELAY_MS) {
        settings_save_now();
    }
}
