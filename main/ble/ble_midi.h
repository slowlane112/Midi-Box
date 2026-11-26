#ifndef BLE_MIDI_H
#define BLE_MIDI_H

#include <stdint.h>
#include <stdbool.h>
#include "host/ble_gap.h"


typedef enum {
    BLE_MIDI_IDLE,
    BLE_MIDI_CONNECTING,
    BLE_MIDI_CONNECTED,
    BLE_MIDI_CONNECT_ERROR,
	BLE_MIDI_NO_MIDI_ERROR,
	BLE_MIDI_DISCONNECTED
} ble_midi_state_t;

extern ble_midi_state_t ble_midi_state;
/* Incoming BLE MIDI goes to midi_in_dispatch(..., MIDI_SRC_BLE): thru, monitor, recorder. */
void ble_midi_init(void);
void ble_midi_connect(const ble_addr_t *addr, uint8_t own_addr_type);
void ble_midi_disconnect(void);

#endif
