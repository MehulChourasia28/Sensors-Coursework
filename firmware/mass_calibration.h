#ifndef MASS_CALIBRATION_H
#define MASS_CALIBRATION_H

#include <stdbool.h>
#include <stdint.h>

/* Feed actual ADC conversions, not interpolated/repeated samples, to tare. */
#define MASS_SETTLE_SAMPLES 50u
#define MASS_TARE_SAMPLES 32u

typedef struct {
    int32_t tare_sum;
    int32_t zero_counts;
    uint32_t tare_samples;
    uint32_t settle_samples;
    float grams_per_count;
} mass_calibration;

/* Call at startup/reset with a measured, nonzero calibration coefficient.
 * Assume zero wind in the final mounting position. Discard 50 actual ADC
 * conversions to settle, then average 32 conversions to establish a fixed zero.
 */
void mass_calibration_init(mass_calibration *state, float grams_per_count);
/* Use the fitted 0/20/30/35 g coefficient recorded on 2026-11-04. */
void mass_calibration_start(mass_calibration *state);
bool mass_calibration_tare(mass_calibration *state, int32_t raw_counts);
bool mass_calibration_grams(const mass_calibration *state, int32_t raw_counts, float *grams);

#endif
