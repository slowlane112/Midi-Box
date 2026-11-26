#pragma once
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_DMA (1<<3)
#define MALLOC_CAP_INTERNAL (1<<11)
#define MALLOC_CAP_SPIRAM (1<<10)
#define MALLOC_CAP_DEFAULT (1<<12)
void *heap_caps_malloc(size_t, uint32_t);
void *heap_caps_realloc(void *, size_t, uint32_t);
void *heap_caps_aligned_alloc(size_t, size_t, uint32_t);
size_t heap_caps_get_free_size(uint32_t);
size_t heap_caps_get_largest_free_block(uint32_t);
