#ifndef BLE_SCAN_H
#define BLE_SCAN_H

#include <stdint.h>
#include <stdbool.h>
#include "host/ble_hs.h"

#define BLE_SCAN_MAX_DEVICES	64

typedef struct {
	bool used;
	ble_addr_t addr;
	int8_t rssi;
	char name[48];
} ble_scan_device_t;

extern ble_scan_device_t ble_scan_devices[BLE_SCAN_MAX_DEVICES];
extern int8_t ble_scan_device_count;
void ble_scan_start(void);
void ble_scan_stop(void);

#endif