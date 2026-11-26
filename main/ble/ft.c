#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include "sd.h"
#include "ft.h"

static const char *TAG = "ft";
#define DEVICE_NAME "MIDI BOX"
#define CHUNK 240

extern uint16_t file_transfer_tx_val_handle;   /* ble.c */

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile ft_state_t s_state = FT_OFF;
static uint16_t s_conn;
static bool s_conn_valid;
static char s_name[128];
static size_t s_size;
static uint8_t *s_buf;
static volatile size_t s_off;
static char s_saved[300];
static char s_err[48];
static bool s_active;

static void set_state(ft_state_t st)
{
    portENTER_CRITICAL(&s_mux);
    s_state = st;
    portEXIT_CRITICAL(&s_mux);
}

static void notify(const char *msg)
{
    if (s_conn_valid && file_transfer_tx_val_handle) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(msg, (uint16_t)strlen(msg));
        if (om) {
            ble_gatts_notify_custom(s_conn, file_transfer_tx_val_handle, om);
        }
    }
}

static void drop_buffer(void)
{
    free(s_buf);
    s_buf = NULL;
    s_off = 0;
}

/* GATT write callback, registered in ble.c (runs in the NimBLE task) */
int file_chr_write_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    static uint8_t data[512];
    if (len > sizeof(data)) {
        len = sizeof(data);
    }
    if (ble_hs_mbuf_to_flat(ctxt->om, data, len, NULL) != 0) {
        return 0;
    }
    ft_state_t st = s_state;

    if ((st == FT_CONNECTED || st == FT_DONE || st == FT_FAILED) && len > 5 && memcmp(data, "META:", 5) == 0) {
        char meta[256];
        size_t n = len < sizeof(meta) - 1 ? len : sizeof(meta) - 1;
        memcpy(meta, data, n);
        meta[n] = '\0';
        char *colon = strchr(meta + 5, ':');
        if (colon) {
            *colon = '\0';
            s_size = (size_t)strtoul(meta + 5, NULL, 10);
            /* keep only the file name, never a path from the PC */
            const char *name = colon + 1;
            const char *slash = strrchr(name, '/');
            const char *bslash = strrchr(name, '\\');
            if (slash) {
                name = slash + 1;
            }
            if (bslash && bslash + 1 > name) {
                name = bslash + 1;
            }
            snprintf(s_name, sizeof(s_name), "%s", name[0] ? name : "file.bin");
            /* characters the SD card's file system can't store */
            for (char *c = s_name; *c; c++) {
                if ((unsigned char)*c < 0x20 || strchr("\\/:*?\"<>|", *c)) {
                    *c = '_';
                }
            }
            s_err[0] = '\0';
            set_state(FT_ASK);
            ESP_LOGI(TAG, "request: %s (%u bytes)", s_name, (unsigned)s_size);
        }
        return 0;
    }
    /* "EOF" ends the file only once every announced byte has arrived, so a file whose
     * last 3 bytes happen to be "EOF" is still received whole */
    if (st == FT_RECEIVING && len == 3 && memcmp(data, "EOF", 3) == 0 && s_off == s_size) {
        set_state(FT_SAVING);   /* written by ft_service() in the UI task */
        return 0;
    }
    if (st == FT_RECEIVING && s_buf) {
        if (s_off + len <= s_size) {
            memcpy(s_buf + s_off, data, len);
            s_off += len;
        } else {
            snprintf(s_err, sizeof(s_err), "More data than announced");
            drop_buffer();
            set_state(FT_FAILED);
            notify("FAILED:More data than announced");
        }
    }
    return 0;
}

static int gap_event(struct ble_gap_event *event, void *arg);

static void advertise(void)
{
    struct ble_gap_adv_params adv = { 0 };
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "advertising failed: %d", rc);
    }
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn = event->connect.conn_handle;
            s_conn_valid = true;
            set_state(FT_CONNECTED);
        } else if (s_active) {
            advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_valid = false;
        if (s_state == FT_RECEIVING || s_state == FT_ASK) {
            drop_buffer();
        }
        if (s_state != FT_SAVING && s_state != FT_DONE) {
            set_state(s_active ? FT_WAITING : FT_OFF);
        }
        if (s_active) {
            advertise();   /* ready for the next transfer */
        }
        break;
    default:
        break;
    }
    return 0;
}

void ft_start(void)
{
    s_active = true;
    ble_svc_gap_device_name_set(DEVICE_NAME);
    struct ble_hs_adv_fields f = { 0 };
    f.name = (uint8_t *)DEVICE_NAME;
    f.name_len = strlen(DEVICE_NAME);
    f.name_is_complete = 1;
    ble_gap_adv_set_fields(&f);
    set_state(FT_WAITING);
    advertise();
}

void ft_stop(void)
{
    s_active = false;
    if (s_conn_valid) {
        ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    for (int i = 0; i < 20 && ble_gap_adv_active(); i++) {
        ble_gap_adv_stop();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    drop_buffer();
    set_state(FT_OFF);
}

void ft_accept(void)
{
    if (s_state != FT_ASK) {
        return;
    }
    s_buf = s_size ? heap_caps_malloc(s_size, MALLOC_CAP_SPIRAM) : NULL;
    if (!s_buf && s_size) {
        snprintf(s_err, sizeof(s_err), "File too big for memory");
        set_state(FT_FAILED);
        notify("DECLINED:File too big for memory");
        return;
    }
    s_off = 0;
    set_state(FT_RECEIVING);
    notify("ACCEPTED");
}

void ft_decline(void)
{
    if (s_state != FT_ASK) {
        return;
    }
    notify("DECLINED");
    set_state(FT_CONNECTED);
    vTaskDelay(pdMS_TO_TICKS(100));
    if (s_conn_valid) {
        ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    }
}

void ft_service(void)
{
    if (s_state != FT_SAVING) {
        return;
    }
    if (!fs_is_dir(FT_DOWNLOAD_DIR)) {
        fs_mkdir(FT_DOWNLOAD_DIR);
    }
    char path[300];
    path_join(path, sizeof(path), FT_DOWNLOAD_DIR, s_name);
    path_unique(path, sizeof(path));
    bool ok = s_buf || s_size == 0;
    if (ok) {
        ok = fs_write_all(path, s_buf ? s_buf : (const uint8_t *)"", s_off);
    }
    drop_buffer();
    /* tell the PC how it ended (older scripts ignore this) */
    char msg[200];
    if (ok) {
        snprintf(s_saved, sizeof(s_saved), "%s", path);
        set_state(FT_DONE);
        snprintf(msg, sizeof(msg), "SAVED:%.180s", path_name(path));
    } else {
        snprintf(s_err, sizeof(s_err), "Could not write to the SD card");
        set_state(FT_FAILED);
        snprintf(msg, sizeof(msg), "FAILED:%s", s_err);
    }
    notify(msg);
}

void ft_ack(void)
{
    if (s_state == FT_DONE || s_state == FT_FAILED) {
        set_state(s_conn_valid ? FT_CONNECTED : (s_active ? FT_WAITING : FT_OFF));
    }
}

void ft_get(ft_status_t *o)
{
    portENTER_CRITICAL(&s_mux);
    o->state = s_state;
    o->size = s_size;
    o->received = s_off;
    portEXIT_CRITICAL(&s_mux);
    snprintf(o->name, sizeof(o->name), "%s", s_name);
    snprintf(o->saved_path, sizeof(o->saved_path), "%s", s_saved);
    snprintf(o->error, sizeof(o->error), "%s", s_err);
}
