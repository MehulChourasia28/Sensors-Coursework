#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include "wind_model.h"

static void near_value(float actual, double expected)
{
    assert(fabs((double)actual - expected) <= 2e-6 * fmax(1.0, fabs(expected)));
}

int main(void)
{
    wind_model model;
    wind_model_defaults(&model);
    near_value(model.area_m2, 0.0174);
    near_value(model.speed_gain, 0.55);
    near_value(model.air_density_kg_m3, 101325.0 / (287.05 * 293.15));
    const double rho = 101325.0 / (287.05 * 293.15);
    float speed;
    for (int i = 0; i <= 150000; ++i) {
        float grams = (float)i / 10.0f;
        assert(wind_model_speed_mps(&model, grams, &speed));
        double expected = 0.55 * sqrt(2.0 * (double)grams * 0.00980665 / (rho * 1.28 * 0.0174));
        near_value(speed, expected);
    }
    assert(wind_model_speed_mps(&model, -20.0f, &speed));
    assert(speed == 0); /* Never turn an opposite force into positive wind. */
    assert(wind_model_speed_mps(&model, 20.0f, &speed));
    float base = speed;
    model.drag_coefficient *= 2;
    assert(wind_model_speed_mps(&model, 20.0f, &speed));
    near_value(speed, base / sqrt(2));
    model.area_m2 *= 2;
    assert(wind_model_speed_mps(&model, 20.0f, &speed));
    near_value(speed, base / 2.0);
    model.air_density_kg_m3 *= 2;
    assert(wind_model_speed_mps(&model, 20.0f, &speed));
    near_value(speed, base / sqrt(8));
    model.force_polarity = -1;
    assert(wind_model_speed_mps(&model, -20.0f, &speed));
    near_value(speed, base / sqrt(8));
    assert(wind_model_speed_mps(&model, 20.0f, &speed) && speed == 0);
    assert(!wind_model_speed_mps(&model, NAN, &speed));
    assert(!wind_model_speed_mps(&model, INFINITY, &speed));
    model.force_polarity = 0;
    assert(!wind_model_speed_mps(&model, 20.0f, &speed));
    wind_model_defaults(&model);
    model.area_m2 = 0;
    assert(!wind_model_speed_mps(&model, 20.0f, &speed));
    model.area_m2 = NAN;
    assert(!wind_model_speed_mps(&model, 20.0f, &speed));
    wind_model_defaults(&model);
    model.air_density_kg_m3 = -1;
    assert(!wind_model_speed_mps(&model, 20.0f, &speed));
    wind_model_defaults(&model);
    model.drag_coefficient = 0;
    assert(!wind_model_speed_mps(&model, 20.0f, &speed));
    wind_model_defaults(&model);
    model.area_m2 = FLT_MAX;
    model.drag_coefficient = FLT_MAX;
    assert(!wind_model_speed_mps(&model, 20.0f, &speed));
    wind_model_defaults(&model);
    model.speed_gain = 0;
    assert(!wind_model_speed_mps(&model, 20.0f, &speed));
    model.speed_gain = NAN;
    assert(!wind_model_speed_mps(&model, 20.0f, &speed));
    wind_model_defaults(&model);
    model.speed_gain = 1;
    assert(wind_model_speed_mps(&model, 20.0f, &speed));
    near_value(speed, 3.824513014622251);
    puts("PASS: 150001 independent double-precision comparisons, units, density/Cd/area sensitivity, sign and invalid inputs");
    return 0;
}
