#ifndef BLE_H
#define BLE_H

#include <stdint.h>
#include <stdbool.h>
#include "host/ble_hs.h"

extern uint8_t ble_own_addr_type;
extern ble_addr_t ble_addr;
extern char ble_name[48];

void ble_init(void);

#endif