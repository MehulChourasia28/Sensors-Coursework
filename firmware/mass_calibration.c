#include "mass_calibration.h"
#include "mass_calibration_coefficients.h"
#include <float.h>

static bool valid_sample(int32_t raw_counts)
{
    return raw_counts > -8388608 && raw_counts < 8388607;
}

void mass_calibration_init(mass_calibration *state, float grams_per_count)
{
    state->tare_sum = 0;
    state->zero_counts = 0;
    state->tare_samples = 0;
    state->settle_samples = 0;
    state->grams_per_count = grams_per_count;
}

void mass_calibration_start(mass_calibration *state)
{
    mass_calibration_init(state, MASS_GRAMS_PER_COUNT);
}

bool mass_calibration_tare(mass_calibration *state, int32_t raw_counts)
{
    if (state->tare_samples == MASS_TARE_SAMPLES) {
        return true;
    }
    if (!valid_sample(raw_counts)) {
        state->tare_sum = 0;
        state->tare_samples = 0;
        state->settle_samples = 0;
        return false;
    }
    /* Ignore power-up transients before measuring the no-wind baseline.
     * Once established, the zero remains fixed even under a sustained load.
     */
    if (state->settle_samples < MASS_SETTLE_SAMPLES) {
        ++state->settle_samples;
        return false;
    }
    /* 32 signed 24-bit conversions fit safely in a signed 32-bit sum. */
    state->tare_sum += raw_counts;
    ++state->tare_samples;
    if (state->tare_samples != MASS_TARE_SAMPLES) {
        return false;
    }
    state->zero_counts = state->tare_sum / (int32_t)MASS_TARE_SAMPLES;
    return true;
}

bool mass_calibration_grams(const mass_calibration *state, int32_t raw_counts, float *grams)
{
    float scale = state->grams_per_count;
    if (state->tare_samples != MASS_TARE_SAMPLES || !valid_sample(raw_counts)
        || scale == 0.0f || scale != scale || scale > FLT_MAX || scale < -FLT_MAX) {
        return false;
    }
    /* Subtract integer counts first to preserve precision around a large zero.
     * Signed noise is retained; no clamping or automatic tare while loaded.
     */
    *grams = (float)(raw_counts - state->zero_counts) * scale;
    return true;
}
