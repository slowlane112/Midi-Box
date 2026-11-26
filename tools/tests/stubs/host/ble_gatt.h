#pragma once
#include "host/ble_hs.h"
#define BLE_GATT_SVC_TYPE_PRIMARY 1
#define BLE_GATT_CHR_F_WRITE 0x0008
#define BLE_GATT_CHR_F_WRITE_NO_RSP 0x0004
#define BLE_GATT_CHR_F_NOTIFY 0x0010
struct ble_gatt_error { uint16_t status; uint16_t att_handle; };
struct ble_gatt_attr { uint16_t handle; uint16_t offset; struct os_mbuf *om; };
struct ble_gatt_svc { uint16_t start_handle; uint16_t end_handle; ble_uuid_any_t uuid; };
struct ble_gatt_chr { uint16_t def_handle; uint16_t val_handle; uint8_t properties; ble_uuid_any_t uuid; };
struct ble_gatt_dsc { uint16_t handle; ble_uuid_any_t uuid; };
struct ble_gatt_access_ctxt { uint8_t op; struct os_mbuf *om; };
typedef int ble_gatt_access_fn(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
struct ble_gatt_chr_def { const ble_uuid_t *uuid; ble_gatt_access_fn *access_cb; void *arg; void *descriptors; uint16_t flags; uint16_t *val_handle; };
struct ble_gatt_svc_def { uint8_t type; const ble_uuid_t *uuid; const void *includes; const struct ble_gatt_chr_def *characteristics; };
int ble_gattc_write_flat(uint16_t conn, uint16_t attr, const void *data, uint16_t len, int (*cb)(uint16_t, const struct ble_gatt_error *, struct ble_gatt_attr *, void *), void *arg);
int ble_gattc_disc_all_svcs(uint16_t conn, int (*cb)(uint16_t, const struct ble_gatt_error *, const struct ble_gatt_svc *, void *), void *arg);
int ble_gattc_disc_all_chrs(uint16_t conn, uint16_t start, uint16_t end, int (*cb)(uint16_t, const struct ble_gatt_error *, const struct ble_gatt_chr *, void *), void *arg);
int ble_gattc_disc_all_dscs(uint16_t conn, uint16_t start, uint16_t end, int (*cb)(uint16_t, const struct ble_gatt_error *, uint16_t, const struct ble_gatt_dsc *, void *), void *arg);
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *defs);
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *svcs);
int ble_gatts_notify_custom(uint16_t conn, uint16_t att, struct os_mbuf *om);
