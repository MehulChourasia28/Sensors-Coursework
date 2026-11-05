#ifndef WIND_MODEL_H
#define WIND_MODEL_H

#include <stdbool.h>

typedef struct {
    float area_m2;
    float drag_coefficient;
    float air_density_kg_m3;
    float speed_gain;
    int force_polarity;
} wind_model;

void wind_model_defaults(wind_model *model);
/* Equivalent grams -> F=m*g -> speed_gain * sqrt(2F/(rho*Cd*A)).
 * Negative directed force returns zero, never abs(force). An opposite load
 * is not evidence of airflow in the configured measurement direction.
 * This is an unvalidated aerodynamic estimate, not a calibrated airspeed.
 */
bool wind_model_speed_mps(const wind_model *model, float equivalent_grams, float *speed_mps);

#endif
