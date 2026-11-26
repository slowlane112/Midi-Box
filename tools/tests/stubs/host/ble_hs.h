/* Minimal NimBLE stand-in for host compile checks only. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#define BLE_HS_CONN_HANDLE_NONE 0xffff
#define BLE_HS_EALREADY 2
#define BLE_HS_EDONE 14
#define BLE_HS_FOREVER 0x7fffffff
#define BLE_OWN_ADDR_PUBLIC 0
#define BLE_ERR_REM_USER_CONN_TERM 0x13
typedef struct { uint8_t type; uint8_t val[6]; } ble_addr_t;
typedef struct { uint8_t type; } ble_uuid_t;
typedef struct { ble_uuid_t u; uint8_t value[16]; } ble_uuid128_t;
typedef struct { ble_uuid_t u; uint16_t value; } ble_uuid16_t;
typedef union { ble_uuid_t u; ble_uuid16_t u16; ble_uuid128_t u128; } ble_uuid_any_t;
#define BLE_UUID128_INIT(...) { .u = { .type = 128 }, .value = { __VA_ARGS__ } }
#define BLE_UUID16_DECLARE(v) ((const ble_uuid_t *)(&(ble_uuid16_t){ { 16 }, (v) }))
#define BLE_GATT_DSC_CLT_CFG_UUID16 0x2902
int ble_uuid_cmp(const ble_uuid_t *a, const ble_uuid_t *b);
int ble_addr_cmp(const ble_addr_t *a, const ble_addr_t *b);
struct os_mbuf { uint8_t *om_data; uint16_t om_len; };
#define OS_MBUF_PKTLEN(om) ((om)->om_len)
struct os_mbuf *ble_hs_mbuf_from_flat(const void *buf, uint16_t len);
int ble_hs_mbuf_to_flat(const struct os_mbuf *om, void *flat, uint16_t max_len, uint16_t *out_len);
int ble_hs_id_infer_auto(int privacy, uint8_t *out_addr_type);
struct ble_hs_cfg_t { void (*sync_cb)(void); };
extern struct ble_hs_cfg_t ble_hs_cfg;
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
