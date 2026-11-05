#include <assert.h>
#include <stdio.h>
#include "mass_calibration.h"

static void near_value(float actual, float expected)
{
    float error = actual - expected;
    assert(error > -0.0001f && error < 0.0001f);
}

static void settle(mass_calibration *state)
{
    /* Power-up movement must not leak into the subsequent zero average. */
    for (unsigned i = 0; i < MASS_SETTLE_SAMPLES; ++i) {
        assert(!mass_calibration_tare(state, 800000 - (int32_t)i * 1000));
        assert(state->tare_samples == 0);
        assert(state->zero_counts == 0);
        float grams;
        assert(!mass_calibration_grams(state, 490000, &grams));
    }
    assert(state->settle_samples == MASS_SETTLE_SAMPLES);
}

int main(void)
{
    mass_calibration state;
    float grams = 999.0f;
    mass_calibration_init(&state, -0.001f);
    assert(!mass_calibration_grams(&state, 490000, &grams));
    assert(grams == 999.0f);
    settle(&state);
    for (unsigned i = 0; i < MASS_TARE_SAMPLES - 1; ++i) {
        assert(!mass_calibration_tare(&state, 490000));
    }
    assert(!mass_calibration_grams(&state, 470000, &grams));
    assert(mass_calibration_tare(&state, 490000));
    assert(state.zero_counts == 490000);
    assert(mass_calibration_grams(&state, 490000, &grams));
    near_value(grams, 0);
    assert(mass_calibration_grams(&state, 470000, &grams));
    near_value(grams, 20);
    assert(mass_calibration_grams(&state, 460000, &grams));
    near_value(grams, 30);
    assert(mass_calibration_grams(&state, 455000, &grams));
    near_value(grams, 35);
    /* Once tared, a held load must never become the new zero. */
    assert(mass_calibration_tare(&state, 455000));
    assert(state.zero_counts == 490000);
    assert(mass_calibration_grams(&state, 490100, &grams));
    near_value(grams, -0.1f);
    assert(!mass_calibration_grams(&state, -8388608, &grams));
    assert(!mass_calibration_grams(&state, 8388607, &grams));

    /* Restart with a different mechanical baseline and positive polarity. */
    mass_calibration_init(&state, 0.002f);
    settle(&state);
    for (unsigned i = 0; i < 10; ++i) {
        assert(!mass_calibration_tare(&state, -200));
    }
    assert(!mass_calibration_tare(&state, 8388607));
    assert(state.tare_samples == 0);
    assert(state.settle_samples == 0);
    settle(&state);
    for (unsigned i = 0; i < MASS_TARE_SAMPLES; ++i) {
        mass_calibration_tare(&state, -1000);
    }
    assert(mass_calibration_grams(&state, 9000, &grams));
    near_value(grams, 20);
    state.grams_per_count = 0;
    assert(!mass_calibration_grams(&state, 9000, &grams));

    /* Replay rounded means from the real 2026-11-04 plateaus. These check
     * coefficient wiring, not independent physical accuracy.
     */
    mass_calibration_start(&state);
    settle(&state);
    for (unsigned i = 0; i < MASS_TARE_SAMPLES; ++i) {
        mass_calibration_tare(&state, 444619);
    }
    assert(mass_calibration_grams(&state, 422274, &grams));
    assert(grams > 20.08f && grams < 20.10f);
    assert(mass_calibration_grams(&state, 411236, &grams));
    assert(grams > 30.00f && grams < 30.02f);
    assert(mass_calibration_grams(&state, 405758, &grams));
    assert(grams > 34.93f && grams < 34.95f);
    /* Re-powering in a new position changes only the zero, never the slope. */
    float fitted_scale = state.grams_per_count;
    mass_calibration_start(&state);
    assert(state.grams_per_count == fitted_scale);
    settle(&state);
    for (unsigned i = 0; i < MASS_TARE_SAMPLES; ++i) {
        mass_calibration_tare(&state, 492723 + (i % 2 ? 10 : -10));
    }
    assert(mass_calibration_grams(&state, 492723, &grams));
    near_value(grams, 0);
    assert(mass_calibration_grams(&state, 470378, &grams));
    assert(grams > 20.08f && grams < 20.10f);
    puts("PASS: power-up settling, startup tare, preserved scale, both polarities, 0/20/30/35 g, signed noise, saturation, loaded hold and restart");
    return 0;
}
