#pragma once
#include "esp_err.h"
void esp_log_write(int, const char *, const char *, ...) __attribute__((format(printf, 3, 4)));
#define ESP_LOGE(t, f, ...) esp_log_write(1, t, f, ##__VA_ARGS__)
#define ESP_LOGW(t, f, ...) esp_log_write(2, t, f, ##__VA_ARGS__)
#define ESP_LOGI(t, f, ...) esp_log_write(3, t, f, ##__VA_ARGS__)
