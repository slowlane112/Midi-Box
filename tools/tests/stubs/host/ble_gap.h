#pragma once
#include "host/ble_hs.h"
#define BLE_GAP_EVENT_CONNECT 0
#define BLE_GAP_EVENT_DISCONNECT 1
#define BLE_GAP_EVENT_DISC 7
#define BLE_GAP_EVENT_DISC_COMPLETE 8
#define BLE_GAP_EVENT_ENC_CHANGE 10
#define BLE_GAP_EVENT_PASSKEY_ACTION 11
#define BLE_GAP_EVENT_NOTIFY_RX 12
#define BLE_GAP_EVENT_REPEAT_PAIRING 17
#define BLE_GAP_REPEAT_PAIRING_RETRY 1
#define BLE_GAP_CONN_MODE_UND 2
#define BLE_GAP_DISC_MODE_GEN 2
#define BLE_SM_IOACT_DISP 2
#define BLE_SM_IOACT_NUMCMP 4
struct ble_gap_conn_desc { ble_addr_t peer_id_addr; };
struct ble_gap_event {
    uint8_t type;
    union {
        struct { int status; uint16_t conn_handle; } connect;
        struct { int reason; } disconnect;
        struct { const uint8_t *data; uint8_t length_data; ble_addr_t addr; int8_t rssi; } disc;
        struct { int status; } enc_change;
        struct { uint16_t conn_handle; struct { uint8_t action; } params; } passkey;
        struct { uint16_t conn_handle; } repeat_pairing;
        struct { struct os_mbuf *om; } notify_rx;
    };
};
typedef int ble_gap_event_fn(struct ble_gap_event *event, void *arg);
struct ble_gap_adv_params { uint8_t conn_mode, disc_mode; };
struct ble_gap_disc_params { uint16_t itvl, window; uint8_t filter_policy, limited, passive, filter_duplicates; };
struct ble_hs_adv_fields { const uint8_t *name; uint8_t name_len; unsigned name_is_complete : 1; };
struct ble_sm_io { uint8_t action; union { uint32_t passkey; uint8_t numcmp_accept; }; };
int ble_gap_adv_start(uint8_t own_addr_type, const ble_addr_t *direct_addr, int32_t duration_ms, const struct ble_gap_adv_params *p, ble_gap_event_fn *cb, void *arg);
int ble_gap_adv_stop(void);
int ble_gap_adv_active(void);
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *f);
int ble_gap_terminate(uint16_t conn_handle, uint8_t reason);
int ble_gap_connect(uint8_t own_addr_type, const ble_addr_t *peer, int32_t duration_ms, const void *params, ble_gap_event_fn *cb, void *arg);
int ble_gap_disc(uint8_t own_addr_type, int32_t duration_ms, const struct ble_gap_disc_params *p, ble_gap_event_fn *cb, void *arg);
int ble_gap_disc_cancel(void);
int ble_gap_security_initiate(uint16_t conn_handle);
int ble_gap_conn_find(uint16_t handle, struct ble_gap_conn_desc *out);
int ble_sm_inject_io(uint16_t conn_handle, struct ble_sm_io *pkey);
int ble_store_util_delete_peer(const ble_addr_t *peer_id_addr);
