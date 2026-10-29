#include "interpolation.h"

void interpolation_clear(interpolation_history *history)
{
    history->first = 0;
    history->length = 0;
}

void interpolation_push(interpolation_history *history, uint32_t timestamp_ms, int32_t count)
{
    if (history->length == INTERPOLATION_CAPACITY) {
        history->first = (history->first + 1u) % INTERPOLATION_CAPACITY;
        --history->length;
    }
    uint32_t index = (history->first + history->length) % INTERPOLATION_CAPACITY;
    history->samples[index].timestamp_ms = timestamp_ms;
    history->samples[index].count = count;
    ++history->length;
}

bool interpolation_at(const interpolation_history *history, uint32_t target_ms, int32_t *count)
{
    for (uint32_t i = 1; i < history->length; ++i) {
        const measured_sample *a = &history->samples[(history->first + i - 1u) % INTERPOLATION_CAPACITY];
        const measured_sample *b = &history->samples[(history->first + i) % INTERPOLATION_CAPACITY];
        uint32_t span = b->timestamp_ms - a->timestamp_ms;
        uint32_t offset = target_ms - a->timestamp_ms;
        if (span == 0 || span > 1000u || offset > span) {
            continue;
        }
        int32_t delta = b->count - a->count;
        /* Split the product so the full 24-bit input range cannot overflow.
         * span <= 1000 bounds the remainder product below 1,000,000.
         * Integer division rounds the interpolated increment toward zero. */
        int32_t whole = (delta / (int32_t)span) * (int32_t)offset;
        int32_t fraction = ((delta % (int32_t)span) * (int32_t)offset) / (int32_t)span;
        *count = a->count + whole + fraction;
        return true;
    }
    return false;
}
