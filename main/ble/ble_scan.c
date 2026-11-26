#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "ble_scan.h"
#include "ble.h"

ble_scan_device_t ble_scan_devices[BLE_SCAN_MAX_DEVICES];
int8_t ble_scan_device_count = 0;
static bool ble_scan_is_scanning = false;

static ble_scan_device_t *find_device(const ble_addr_t *addr)
{
    for (int i = 0; i < BLE_SCAN_MAX_DEVICES; i++) {
        if (!ble_scan_devices[i].used) continue;
        if (ble_addr_cmp(addr, &ble_scan_devices[i].addr) == 0) {
            return &ble_scan_devices[i];
        }
    }
    return NULL;
}

static ble_scan_device_t *create_device(const ble_addr_t *addr, int8_t rssi)
{
    for (int i = 0; i < BLE_SCAN_MAX_DEVICES; i++) {
        if (!ble_scan_devices[i].used) {
            memset(&ble_scan_devices[i], 0, sizeof(ble_scan_device_t));
            ble_scan_devices[i].used = true;
            memcpy(&ble_scan_devices[i].addr, addr, sizeof(ble_addr_t));
            ble_scan_devices[i].rssi = rssi;
			ble_scan_device_count++;
            return &ble_scan_devices[i];
        }
    }
    return NULL;
}

static void copy_name(char *dest, size_t dest_size, const uint8_t *src, size_t src_len)
{
    if (dest_size == 0) return;
    size_t out = 0;
    for (size_t i = 0; i < src_len && out < dest_size - 1; i++) {
        unsigned char c = src[i];
        dest[out++] = (c >= 32 && c <= 126) ? (char)c : '?';
    }
    dest[out] = '\0';
}

static bool parse_name(const uint8_t *data, uint8_t length, char *name, size_t name_size)
{
    uint8_t pos = 0;
    bool found_short = false;
    char short_name[64] = {0};

    while (pos < length) {
        uint8_t field_len = data[pos];
        if (field_len == 0 || (uint16_t)pos + field_len + 1 > length) break;

        uint8_t type = data[pos + 1];
        const uint8_t *value = &data[pos + 2];
        uint8_t value_len = field_len - 1;

        if (type == 0x09) {
            copy_name(name, name_size, value, value_len);
            return name[0] != '\0';
        }
        if (type == 0x08 && !found_short) {
            copy_name(short_name, sizeof(short_name), value, value_len);
            found_short = true;
        }
        pos += field_len + 1;
    }

    if (found_short) {
        snprintf(name, name_size, "%s", short_name);
        name[name_size - 1] = '\0';
        return true;
    }
    return false;
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
	case BLE_GAP_EVENT_DISC:
    {
        char packet_name[64] = {0};
        bool found = parse_name(event->disc.data, event->disc.length_data, packet_name, sizeof(packet_name));
        bool has_name = (found && packet_name[0] != '\0');

        ble_scan_device_t *device = find_device(&event->disc.addr);
        
        if (device == NULL) {
            // Rule: Only insert if they have a name
            if (!has_name) {
                return 0; 
            }

            device = create_device(&event->disc.addr, event->disc.rssi);
            if (device == NULL) return 0;

            // Set initial name for the newly created device
            snprintf(device->name, sizeof(device->name), "%.47s", packet_name);
            device->name[sizeof(device->name) - 1] = '\0';
        } else {
            // Rule: Update existing device if something changes (RSSI or Name)
            if (device->rssi != event->disc.rssi) {
                device->rssi = event->disc.rssi;
            }

            if (has_name && strcmp(device->name, packet_name) != 0) {
                snprintf(device->name, sizeof(device->name), "%.47s", packet_name);
                device->name[sizeof(device->name) - 1] = '\0';
            }
        }

        return 0;
    }
	case BLE_GAP_EVENT_DISC_COMPLETE:
		ble_scan_is_scanning = false;
		return 0;
	default:
		return 0;
	}
}

void ble_scan_start(void)
{
		
    if (ble_scan_is_scanning) return;
	
	
	//printf("--ble_scan_start--\n");
	
	memset(ble_scan_devices, 0, sizeof(ble_scan_devices));
	ble_scan_device_count = 0;

    struct ble_gap_disc_params params = {0};
    params.passive = 0;
    params.itvl = 0x0010;
    params.window = 0x0010;
    params.filter_duplicates = 0;

	int rc = ble_gap_disc(ble_own_addr_type, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc == 0) {
        ble_scan_is_scanning = true;
        //ESP_LOGI(TAG, "Scan started.");
    } else {
        //ESP_LOGE(TAG, "Scan start failed: %d", rc);
    }
}

void ble_scan_stop(void)
{
    if (!ble_scan_is_scanning) return;

    int rc = ble_gap_disc_cancel();
    if (rc == 0 || rc == BLE_HS_EALREADY) {
        ble_scan_is_scanning = false;
        //ESP_LOGI(TAG, "Scan stopped.");
    } else {
        //ESP_LOGE(TAG, "Scan stop failed: %d", rc);
    }
}

