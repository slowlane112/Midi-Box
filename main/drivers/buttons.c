/*
 * buttons.c - read by a small task of their own every 2 ms, so a press is never missed
 * while the UI task is busy drawing a frame, waiting for a display transfer or reading
 * the SD card. Events go into a queue that the UI drains with buttons_poll().
 *
 * Debounce: a button changes state after 4 equal reads in a row (8 ms), so any press
 * longer than ~8 ms counts, and contact bounce (usually < 5 ms) is ignored.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "board.h"
#include "buttons.h"

#define SAMPLE_MS          2
#define STABLE_MASK        0x0F   /* last 4 samples */
#define REPEAT_DELAY_MS    450
#define REPEAT_RATE_MS     90
#define QUEUE_LEN          32

static const gpio_num_t s_pins[BOARD_BUTTON_COUNT] = BOARD_BUTTON_PINS;

typedef struct {
    uint8_t hist;          /* recent raw samples, bit 0 = newest, 1 = pressed */
    bool down;             /* debounced state */
    uint32_t next_repeat;
} btn_t;

static btn_t s_btn[BOARD_BUTTON_COUNT];
static volatile bool s_shift;
static QueueHandle_t s_queue;

static void emit(int i, bool repeat)
{
    btn_event_t e = { .key = (uint8_t)(i + 1), .shift = s_btn[0].down, .repeat = repeat };
    if (s_queue) {
        xQueueSend(s_queue, &e, 0);   /* if the UI is 32 events behind, drop rather than block */
    }
}

/* one sample of every button; bit i of raw = button i+1 pressed */
void buttons_sample(uint32_t now_ms, uint32_t raw)
{
    for (int i = 0; i < BOARD_BUTTON_COUNT; i++) {
        btn_t *b = &s_btn[i];
        b->hist = (uint8_t)((b->hist << 1) | ((raw >> i) & 1));
        uint8_t recent = b->hist & STABLE_MASK;
        if (!b->down && recent == STABLE_MASK) {
            b->down = true;
            if (i == 0) {
                s_shift = true;
            } else {
                emit(i, false);
                b->next_repeat = now_ms + REPEAT_DELAY_MS;
            }
        } else if (b->down && recent == 0) {
            b->down = false;
            if (i == 0) {
                s_shift = false;
            }
        } else if (b->down && i != 0 && (int32_t)(now_ms - b->next_repeat) >= 0) {
            emit(i, true);
            b->next_repeat = now_ms + REPEAT_RATE_MS;
        }
    }
}

static void button_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        uint32_t raw = 0;
        for (int i = 0; i < BOARD_BUTTON_COUNT; i++) {
            if (gpio_get_level(s_pins[i]) == 0) {
                raw |= 1u << i;
            }
        }
        buttons_sample((uint32_t)(last * portTICK_PERIOD_MS), raw);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SAMPLE_MS));
    }
}

void buttons_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < BOARD_BUTTON_COUNT; i++) {
        mask |= 1ULL << s_pins[i];
    }
    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(btn_event_t));
    /* core 0 next to the UI; above the UI (5) so drawing never delays a sample,
     * well below the MIDI tasks on core 1. Takes a few microseconds every 2 ms. */
    xTaskCreatePinnedToCore(button_task, "buttons", 2048, NULL, 8, NULL, 0);
}

bool buttons_shift_held(void)
{
    return s_shift;
}

int buttons_poll(uint32_t now_ms, btn_event_t *out, int n)
{
    (void)now_ms;
    int count = 0;
    while (count < n && s_queue && xQueueReceive(s_queue, &out[count], 0) == pdTRUE) {
        count++;
    }
    return count;
}
