#ifndef INTERPOLATION_H
#define INTERPOLATION_H

#include <stdbool.h>
#include <stdint.h>

#define INTERPOLATION_CAPACITY 32u

typedef struct {
    uint32_t timestamp_ms;
    int32_t count;
} measured_sample;

typedef struct {
    measured_sample samples[INTERPOLATION_CAPACITY];
    uint32_t first;
    uint32_t length;
} interpolation_history;

void interpolation_clear(interpolation_history *history);
void interpolation_push(interpolation_history *history, uint32_t timestamp_ms, int32_t count);
bool interpolation_at(const interpolation_history *history, uint32_t target_ms, int32_t *count);

#endif
