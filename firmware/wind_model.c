#include "wind_model.h"
#include "wind_model_config.h"
#include <math.h>

_Static_assert(WIND_FORCE_POLARITY == 1 || WIND_FORCE_POLARITY == -1,
               "Wind force polarity must be +1 or -1");

void wind_model_defaults(wind_model *model)
{
    model->area_m2 = WIND_PLATE_WIDTH_M * WIND_PLATE_HEIGHT_M;
    model->drag_coefficient = WIND_DRAG_COEFFICIENT;
    model->air_density_kg_m3 = WIND_AIR_PRESSURE_PA /
        (WIND_DRY_AIR_GAS_CONSTANT * (WIND_AIR_TEMPERATURE_C + 273.15f));
    model->force_polarity = WIND_FORCE_POLARITY;
    model->speed_gain = WIND_EMPIRICAL_SPEED_GAIN;
}

bool wind_model_speed_mps(const wind_model *model, float equivalent_grams, float *speed_mps)
{
    if (!isfinite(equivalent_grams) || !isfinite(model->area_m2) || model->area_m2 <= 0
        || !isfinite(model->drag_coefficient) || model->drag_coefficient <= 0
        || !isfinite(model->air_density_kg_m3) || model->air_density_kg_m3 <= 0
        || !isfinite(model->speed_gain) || model->speed_gain <= 0
        || (model->force_polarity != 1 && model->force_polarity != -1)) {
        return false;
    }
    float directed_grams = equivalent_grams * (float)model->force_polarity;
    if (directed_grams <= 0) {
        *speed_mps = 0;
        return true;
    }
    float force_n = directed_grams * (WIND_GRAVITY_M_S2 / 1000.0f);
    float denominator = model->air_density_kg_m3 * model->drag_coefficient * model->area_m2;
    float squared_speed = 2.0f * force_n / denominator;
    if (!isfinite(squared_speed) || squared_speed < 0 || !isfinite(denominator) || denominator <= 0) {
        return false;
    }
    *speed_mps = model->speed_gain * sqrtf(squared_speed);
    return isfinite(*speed_mps);
}
