#include <string.h>
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE";

uint8_t ble_own_addr_type;
ble_addr_t ble_addr;
char ble_name[48] = "";

// External write callback from file_transfer.c
extern int file_chr_write_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

// Access callback for TX notify characteristic (required by NimBLE stack)
static int file_transfer_tx_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return 0;
}


// Nordic UART Service (NUS) UUIDs
/*
static const ble_uuid128_t nus_service_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e
);

static const ble_uuid128_t nus_rx_chr_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e
);

static const ble_uuid128_t nus_tx_chr_uuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e
);
*/


static const ble_uuid128_t file_transfer_service_uuid = BLE_UUID128_INIT(
	0x23, 0xbd, 0xef, 0x05, 0xb8, 0x94, 0xd1, 0x9b,
	0x87, 0x44, 0xdd, 0xb1, 0xbb, 0xa3, 0x92, 0x17
);

static const ble_uuid128_t file_transfer_rx_chr_uuid = BLE_UUID128_INIT(
	0x43, 0x1e, 0x90, 0x40, 0x70, 0x2e, 0xd0, 0x93,
	0xb4, 0x4e, 0x42, 0x32, 0x7c, 0x0f, 0xe7, 0xf9
);

static const ble_uuid128_t file_transfer_tx_chr_uuid = BLE_UUID128_INIT(
	0xb0, 0x14, 0x20, 0x1f, 0x7e, 0x44, 0x45, 0x93,
	0xc2, 0x4d, 0x7a, 0x15, 0x69, 0xe9, 0x4a, 0x31
);



// Global handle exposed for notifications
uint16_t file_transfer_tx_val_handle = 0;

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &file_transfer_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &file_transfer_rx_chr_uuid.u,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .access_cb = file_chr_write_cb,
            },
            {
                .uuid = &file_transfer_tx_chr_uuid.u,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .access_cb = file_transfer_tx_chr_access_cb,
                .val_handle = &file_transfer_tx_val_handle,
                // CCCD descriptor is added automatically by NimBLE because of BLE_GATT_CHR_F_NOTIFY
            },
            {0}
        },
    },
    {0}
};

static void on_sync(void)
{
    ble_hs_id_infer_auto(0, &ble_own_addr_type);
    ESP_LOGI(TAG, "NimBLE ready and NUS services locked into GATT table.");
}

static void nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_init(void)
{
    ESP_ERROR_CHECK(nimble_port_init());
    
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Register GATT table globally before stack starts
    ESP_ERROR_CHECK(ble_gatts_count_cfg(gatt_svr_svcs));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(gatt_svr_svcs));

    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(nimble_host_task);
}