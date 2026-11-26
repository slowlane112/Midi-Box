/*
 * MIDI Box v2 - ESP32-S3 + SAM2695.
 * Start-up order matters: the SPI3 bus must exist before the display and SD card.
 */
#include "esp_log.h"
#include "bus.h"
#include "lcd.h"
#include "buttons.h"
#include "sd.h"
#include "settings.h"
#include "midi_out.h"
#include "midi_in.h"
#include "player.h"
#include "ble.h"
#include "ble_midi.h"
#include "song.h"
#include "seq.h"
#include "ln_engine.h"
#include "theme.h"
#include "app.h"

void app_main(void)
{
    backlight_init();          /* before anything slow (NVS can take a while) */
    settings_load();
    theme_apply(g_settings.accent);

    bus_init();
    lcd_init();
    buttons_init();
    if (!sd_init()) {
        ESP_LOGW("main", "no SD card");
    }

    midi_out_init();
    midi_out_reset(g_settings.bank, g_settings.volume);
    midi_in_init();
    midi_in_set_thru(g_settings.thru);
    player_init();
    song_init();
    seq_init();
    ln_engine_init();
    ble_init();
    ble_midi_init();

    ui_start(&scr_home, NULL);
}
