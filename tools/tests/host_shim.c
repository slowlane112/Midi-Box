#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
void *heap_caps_malloc(size_t n, uint32_t c) { (void)c; return malloc(n); }
void *heap_caps_realloc(void *p, size_t n, uint32_t c) { (void)c; return realloc(p, n); }
void esp_log_write(int l, const char *t, const char *f, ...) { va_list a; va_start(a, f); printf("[%s] ", t); vprintf(f, a); printf("\n"); va_end(a); }
uint8_t *fs_read_all(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n); *len = fread(b, 1, n, f); fclose(f); return b; }
const char *path_name(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }
#include <stdbool.h>
bool fs_write_all(const char *path, const void *d, size_t n) { FILE *f = fopen(path, "wb"); if (!f) return false; fwrite(d, 1, n, f); fclose(f); return true; }
