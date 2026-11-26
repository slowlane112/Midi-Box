/*
 * bus.h - SPI3 is shared by the main display and the SD card.
 * Anything that touches either must hold the bus lock.
 */
#pragma once
#include <stdbool.h>

void bus_init(void);
void bus_lock(void);
void bus_unlock(void);
/* For the display DMA-done ISR. Returns true if a higher-priority task woke. */
bool bus_unlock_from_isr(void);
