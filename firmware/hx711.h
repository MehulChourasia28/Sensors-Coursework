#ifndef HX711_H
#define HX711_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    HX711_OK,
    HX711_READY_TIMEOUT,
    HX711_DOUT_STUCK_LOW
} hx711_result;

void hx711_init(void);
void hx711_reset(void);
bool hx711_is_ready(void);
hx711_result hx711_read(int32_t *sample);

#endif
