#include "ble_midi.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "esp_timer.h"
#include "midi_parse.h"
#include "midi_in.h"
//#include "esp_ble_midi.h"

static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t midi_val_handle = 0;
static uint16_t midi_cccd_handle = 0;
static uint16_t midi_service_end_handle = 0;
static bool connecting = false;
static bool midi_security_started = false;
ble_midi_state_t ble_midi_state = BLE_MIDI_IDLE;


static bool was_connected = false;


// Bluetooth MIDI Service UUID 03B80E5A-EDE8-4B33-A751-6CE34EC4C700
static const ble_uuid128_t midi_service_uuid = BLE_UUID128_INIT(
    0x00, 0xc7, 0xc4, 0x4e,
    0xe3, 0x6c,
    0x51, 0xa7,
    0x33, 0x4b,
    0xe8, 0xed,
    0x5a, 0x0e, 0xb8, 0x03
);


// Bluetooth MIDI Characteristic UUID 7772E5DB-3868-4112-A1A9-F2669D106BF3
static const ble_uuid128_t midi_chr_uuid = BLE_UUID128_INIT(
    0xf3, 0x6b, 0x10, 0x9d,
    0x66, 0xf2,
    0xa9, 0xa1,
    0x12, 0x41,
    0x68, 0x38,
    0xdb, 0xe5,
    0x72, 0x77
);

static int on_cccd_write(uint16_t conn_hdl, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    if (error->status == 0) {

        printf("MIDI NOTIFICATIONS ENABLED!\n READY TO RECEIVE MIDI DATA.\n");

        ble_midi_state = BLE_MIDI_CONNECTED;
		
		was_connected = true;

    } 
	else {

        printf("CCCD Write Failed: status=%d (0x%04X)\n", error->status, error->status);

        if (attr != NULL) {

            printf("CCCD write attribute handle: %u\n", attr->handle);
        }

        ble_midi_state = BLE_MIDI_CONNECT_ERROR;
    }

    return 0;
}







/* ---- BLE-MIDI packet -> MIDI bytes -> v2 parser -> midi_in_dispatch() ----
 * Each BLE-MIDI packet is: header byte, then timestamp-low bytes in front of
 * status bytes. A byte with the top bit set that arrives between messages is a
 * timestamp and is skipped (same rule as v1). */
static midi_parser_t s_parser;
static uint32_t s_last_byte_ms;

static void on_msg(const uint8_t *msg, uint16_t len, void *ctx)
{
    (void)ctx;
    midi_in_dispatch(msg, len, MIDI_SRC_BLE);
}

static void process_bluetooth_midi_data(const uint8_t *packet, uint16_t len)
{
    if (packet == NULL || len < 2) {
        return;
    }
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - s_last_byte_ms > 2000 && !midi_parser_idle(&s_parser)) {
        midi_parser_reset(&s_parser);   /* a message was cut off: start clean */
    }
    s_last_byte_ms = now;
    bool can_skip_timestamp = false;
    for (int i = 2; i < len; i++) {     /* skip header and first timestamp */
        uint8_t b = packet[i];
        if (can_skip_timestamp && midi_parser_idle(&s_parser) && (b & 0x80) != 0) {
            can_skip_timestamp = false;  /* timestamp before a status byte */
        } else {
            midi_parser_feed(&s_parser, b);
            can_skip_timestamp = true;
        }
    }
}

static void subscribe_to_midi(uint16_t cccd_handle) {
	
    static uint8_t cccd_val[2] = {0x01, 0x00};

    if (cccd_handle == 0) {
        printf("ERROR: Invalid CCCD handle.\n");
        ble_midi_state = BLE_MIDI_CONNECT_ERROR;
        return;
    }

    printf("Subscribing: Writing 0x0001 to CCCD (Handle %u)...\n", cccd_handle);
    int rc = ble_gattc_write_flat(conn_handle, cccd_handle, cccd_val, sizeof(cccd_val), on_cccd_write, NULL);

    if (rc != 0) {
        printf("ble_gattc_write_flat failed; rc=%d (0x%04X)\n", rc, rc);
        ble_midi_state = BLE_MIDI_CONNECT_ERROR;
    }
}

static int midi_dsc_cb(uint16_t conn_handle_param, const struct ble_gatt_error *error, uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg)
{

    if (error->status == 0 && dsc != NULL) {

        printf("Descriptor discovered: handle=%u\n", dsc->handle);

         // CCCD UUID = 0x2902

        if (ble_uuid_cmp(&dsc->uuid.u, BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)) == 0) {
            printf("*** CCCD FOUND: handle=%u ***\n", dsc->handle);
            midi_cccd_handle = dsc->handle;
		}

        return 0;
    }

    if (error->status == BLE_HS_EDONE) {

        printf("Descriptor discovery complete for characteristic %u.\n", chr_val_handle);

        if (midi_cccd_handle == 0) {
            printf("ERROR: No CCCD found for MIDI characteristic!\n");
            ble_midi_state = BLE_MIDI_CONNECT_ERROR;
            return 0;
        }

        printf("CCCD found at handle %u.\n", midi_cccd_handle);
		 
		 // encryption

        if (!midi_security_started) {

            midi_security_started = true;
            printf("Starting BLE security before CCCD write...\n");
            int rc = ble_gap_security_initiate(conn_handle);

            if (rc != 0) {
                printf("ble_gap_security_initiate failed: rc=%d (0x%04X)\n", rc, rc);
                ble_midi_state = BLE_MIDI_CONNECT_ERROR;
            }

        } 
		else {
			// Security was already started/completed.
			subscribe_to_midi(midi_cccd_handle);
        }

        return 0;
    }

    printf("Descriptor discovery error: status=%d (0x%04X)\n", error->status, error->status);
    ble_midi_state = BLE_MIDI_CONNECT_ERROR;

    return 0;
}

static int midi_chr_cb(uint16_t conn_handle_param, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr, void *arg)
{
     // Characteristic found

    if (error->status == 0 && chr != NULL) {

        printf("Characteristic discovered: def=%u value=%u properties=0x%02X\n", chr->def_handle, chr->val_handle, chr->properties);

         // Only accept the standard Bluetooth MIDI characteristic.

        if (ble_uuid_cmp(&chr->uuid.u, &midi_chr_uuid.u) == 0) {

            printf("BLUETOOTH MIDI CHARACTERISTIC FOUND!\n");
            printf("Value handle: %u\n", chr->val_handle);
            printf("Properties:   0x%02X\n", chr->properties);

            midi_val_handle = chr->val_handle;
            midi_cccd_handle = 0;

            printf("Discovering descriptors for characteristic value handle %u through service end handle %u...\n", chr->val_handle, midi_service_end_handle);

            int rc = ble_gattc_disc_all_dscs(conn_handle, chr->val_handle, midi_service_end_handle, midi_dsc_cb, NULL);

            if (rc != 0) {
                printf("ble_gattc_disc_all_dscs failed: rc=%d (0x%04X)\n", rc, rc);
                ble_midi_state = BLE_MIDI_CONNECT_ERROR;
            }

            return 0;
        }

		// Not the MIDI characteristic.

        return 0;
    }

    if (error->status == BLE_HS_EDONE) {

        printf("MIDI service characteristic discovery complete.\n");

        if (midi_val_handle == 0) {
            printf("ERROR: Bluetooth MIDI characteristic not found!\n");
            ble_midi_state = BLE_MIDI_CONNECT_ERROR;
        }

        return 0;
    }

    printf("MIDI characteristic discovery error: status=%d (0x%04X)\n", error->status, error->status);
    ble_midi_state = BLE_MIDI_CONNECT_ERROR;

    return 0;
}

static int service_cb(uint16_t conn_handle_param, const struct ble_gatt_error *error, const struct ble_gatt_svc *service, void *arg)
{

     // Service found

    if (error->status == 0 && service != NULL) {

        printf("Service discovered: start=%u end=%u\n", service->start_handle, service->end_handle);


         // Check for Bluetooth MIDI service.

        if (ble_uuid_cmp((const ble_uuid_t *)&service->uuid, &midi_service_uuid.u) == 0) {

            printf("BLUETOOTH MIDI SERVICE FOUND!\n");
            printf("Start handle: %u\n", service->start_handle);
            printf("End handle:   %u\n", service->end_handle);

            // Save service range.

            midi_service_end_handle = service->end_handle;
            midi_val_handle = 0;
            midi_cccd_handle = 0;

            // Start characteristic discovery.

            int rc = ble_gattc_disc_all_chrs(conn_handle, service->start_handle, service->end_handle, midi_chr_cb, NULL);

            if (rc != 0) {
                printf("ble_gattc_disc_all_chrs failed: rc=%d (0x%04X)\n", rc, rc);
                ble_midi_state = BLE_MIDI_CONNECT_ERROR;
            }

            return 0;
        }

        return 0;
    }

    if (error->status == BLE_HS_EDONE) {

        printf("Service discovery complete.\n");

        if (midi_val_handle == 0) {
            printf("Non-MIDI device detected. Disconnecting immediately.\n");
            ble_midi_state = BLE_MIDI_NO_MIDI_ERROR;
            ble_midi_disconnect();
        }

        return 0;
    }

    printf("Service discovery error: status=%d (0x%04X)\n", error->status, error->status);
    ble_midi_state = BLE_MIDI_CONNECT_ERROR;

    return 0;
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

	// connecting
    case BLE_GAP_EVENT_CONNECT:

        if (event->connect.status == 0) {

            conn_handle = event->connect.conn_handle;
            connecting = false;

            midi_val_handle = 0;
            midi_cccd_handle = 0;
            midi_service_end_handle = 0;
            midi_security_started = false;

            printf("Connected successfully! Handle: %u. Scanning services...\n", conn_handle);

            // Discover all services.
            int rc = ble_gattc_disc_all_svcs(conn_handle, service_cb, NULL);

            if (rc != 0) {
                printf("ble_gattc_disc_all_svcs failed: rc=%d (0x%04X)\n", rc, rc);
                ble_midi_state = BLE_MIDI_CONNECT_ERROR;
            }

        } 
		else {
            printf("Connection error: %d\n", event->connect.status);
            conn_handle = BLE_HS_CONN_HANDLE_NONE;
            connecting = false;
            
			ble_midi_state = BLE_MIDI_CONNECT_ERROR;
        }

        return 0;


     // encryption changed
    case BLE_GAP_EVENT_ENC_CHANGE:

        printf("Encryption changed: status=%d (0x%04X)\n", event->enc_change.status, event->enc_change.status);

        if (event->enc_change.status == 0) {

            printf("BLE encryption successful.\n");

             // Now it is safe to write the CCCD.
            if (midi_cccd_handle != 0) {
                printf("Now subscribing to MIDI CCCD handle %u...\n", midi_cccd_handle);
                subscribe_to_midi(midi_cccd_handle);

            } 
			else {
                printf("Encryption complete, but CCCD handle is missing.\n");
                ble_midi_state = BLE_MIDI_CONNECT_ERROR;
            }

        } 
		else {
            printf("BLE encryption FAILED: %d (0x%04X)\n", event->enc_change.status, event->enc_change.status);
            ble_midi_state = BLE_MIDI_CONNECT_ERROR;
        }

        return 0;

     // Passkey action

    case BLE_GAP_EVENT_PASSKEY_ACTION:
    {
        struct ble_sm_io pkey = {0};


        if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {

            pkey.action = BLE_SM_IOACT_NUMCMP;

            pkey.numcmp_accept = 1;


            ble_sm_inject_io(event->passkey.conn_handle, &pkey);

        } 
		else if (
        
			event->passkey.params.action == BLE_SM_IOACT_DISP) {
            pkey.action = BLE_SM_IOACT_DISP;
            pkey.passkey = 000000;

            ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        }

        return 0;
    }


	// Repeat pairing
    case BLE_GAP_EVENT_REPEAT_PAIRING:
    {
        struct ble_gap_conn_desc desc;

        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }

        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    // Disconnect
    case BLE_GAP_EVENT_DISCONNECT:

        printf("Disconnected: reason=%d\n", event->disconnect.reason);

        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        connecting = false;

        midi_val_handle = 0;
        midi_cccd_handle = 0;
        midi_service_end_handle = 0;
        midi_security_started = false;
		
		if (was_connected) {
			ble_midi_state = BLE_MIDI_DISCONNECTED;
		}

        return 0;

    // MIDI notification received
    case BLE_GAP_EVENT_NOTIFY_RX:

        if (event->notify_rx.om != NULL) {

            uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
            uint8_t data[512];

			//printf("len %d\n", len);

            if (len > sizeof(data)) {
				//printf("------LARGER------\n");
                len = sizeof(data);
            }

            if (ble_hs_mbuf_to_flat(event->notify_rx.om, data, len, NULL) == 0) {
                //esp_ble_midi_on_bep_received(data, len);
				process_bluetooth_midi_data(data, len);
            }
			
			/*if (ble_hs_mbuf_to_flat(event->notify_rx.om, data, len, NULL) == 0) {
				for (uint16_t i = 0; i < len; i++) {
					printf("%02X ", data[i]);
				}
				printf("\n");  // One line per notification
			}*/

        }

        return 0;


    // Other GAP events
    default:

        printf("Unhandled GAP event type: %d\n", event->type);

        return 0;
    }
}

void ble_midi_init(void)
{
    midi_parser_init(&s_parser, on_msg, NULL);
    conn_handle = BLE_HS_CONN_HANDLE_NONE;
    midi_val_handle = 0;
    midi_cccd_handle = 0;
    midi_service_end_handle = 0;
    connecting = false;
    midi_security_started = false;
    ble_midi_state = BLE_MIDI_IDLE;
}

void ble_midi_connect(const ble_addr_t *addr, uint8_t own_addr_type)
{
    if (connecting || conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        printf("Already connecting or connected.\n");
        return;
    }

	was_connected = false;
	
    ble_midi_state = BLE_MIDI_CONNECTING;

    connecting = true;

    midi_val_handle = 0;
    midi_cccd_handle = 0;
    midi_service_end_handle = 0;
    midi_security_started = false;

    printf("Initiating connection to target BLE device.\n");

    int rc = ble_gap_connect(own_addr_type, addr, 5000, NULL, gap_event, NULL);

    if (rc != 0) {
        printf("Connection failed to start: %d\n", rc);
        connecting = false;
        ble_midi_state = BLE_MIDI_CONNECT_ERROR;
    }
}

void ble_midi_disconnect(void)
{
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

