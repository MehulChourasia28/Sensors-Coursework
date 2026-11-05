#ifndef WIND_MODEL_CONFIG_H
#define WIND_MODEL_CONFIG_H

/* Broad face perpendicular to uniform airflow; same load-cell sensing axis
 * and force centroid as the central calibration masses. These air conditions
 * and Cd are ASSUMPTIONS, not measurements or a wind-tunnel calibration.
 */
#define WIND_PLATE_WIDTH_M 0.150f
#define WIND_PLATE_HEIGHT_M 0.116f
#define WIND_DRAG_COEFFICIENT 1.28f
#define WIND_AIR_PRESSURE_PA 101325.0f
#define WIND_AIR_TEMPERATURE_C 20.0f
#define WIND_DRY_AIR_GAS_CONSTANT 287.05f
#define WIND_GRAVITY_M_S2 9.80665f
#define WIND_FORCE_POLARITY 1
/* Rough one-parameter guide from the intact-looking 17-22 s section of
 * 8_20261104_171022. User restored 0.55 on 2026-11-05 after trying 0.60.
 * No fitted offset, curve or delay.
 * Mount instability makes this provisional. Recheck after securing/remounting.
 */
#define WIND_EMPIRICAL_SPEED_GAIN 0.55f

#endif
