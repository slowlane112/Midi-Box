/* mutex stand-ins for tests that link song.c */
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t s, unsigned t) { (void)s; (void)t; return 1; }
int xSemaphoreGive(SemaphoreHandle_t s) { (void)s; return 1; }
