#include "board.h"
#include "config.h"
#include "hx711.h"
#include "interpolation.h"
#include "mass_calibration.h"
#include "mass_calibration_coefficients.h"
#include "wind_model.h"
#include "wind_model_config.h"

static void publish_decimal(float value);

extern uint32_t _stack_top, _data_load, _data_start, _data_end, _bss_start, _bss_end;
void reset_handler(void);
void systick_handler(void);

static void fault_handler(void)
{
    while (1) {
    }
}

__attribute__((section(".vectors"), used))
const uintptr_t vectors[] = {
    (uintptr_t)&_stack_top, (uintptr_t)reset_handler,
    (uintptr_t)fault_handler, (uintptr_t)fault_handler,
    (uintptr_t)fault_handler, (uintptr_t)fault_handler, (uintptr_t)fault_handler,
    0, 0, 0, 0, (uintptr_t)fault_handler, (uintptr_t)fault_handler, 0,
    (uintptr_t)fault_handler, (uintptr_t)systick_handler,
};

static void announce(const mass_calibration *scale, const wind_model *wind)
{
    serial_write("# FIRMWARE=" FIRMWARE_ID "; MCU=STM32F411RE; HSI=16000000; UART=115200\r\n");
    serial_write("# HX711 channel=A gain=128 DOUT=PA");
    serial_unsigned(HX711_DOUT_PIN);
    serial_write(" SCK=PA");
    serial_unsigned(HX711_SCK_PIN);
    serial_write("; power=3V3; measured raw counts in ADC comments\r\n");
    serial_write("# UNITS=m/s; METHOD=plate_drag_estimate; UNVALIDATED_AIRSPEED\r\n");
    serial_write("# MASS_CALIBRATION tare_counts=");
    serial_signed(scale->zero_counts);
    serial_write(" grams_per_count=" MASS_GRAMS_PER_COUNT_TEXT " tare_samples=");
    serial_unsigned(scale->tare_samples);
    serial_write(" ready=");
    serial_unsigned(scale->tare_samples == MASS_TARE_SAMPLES ? 1u : 0u);
    serial_write("\r\n");
    serial_write("# STARTUP_ZERO settle_samples=");
    serial_unsigned(scale->settle_samples);
    serial_write(" settle_target=50 tare_target=32; zero wind assumed at power-on/reset\r\n");
    serial_write("# INTERPOLATION=linear; output_hz=200; delay_ms=150; ADC rate unchanged\r\n");
    serial_write("# WIND_MODEL area_m2=");
    publish_decimal(wind->area_m2);
    serial_write("# WIND_MODEL cd=");
    publish_decimal(wind->drag_coefficient);
    serial_write("# WIND_MODEL rho_kg_m3=");
    publish_decimal(wind->air_density_kg_m3);
    serial_write("# WIND_MODEL pressure_pa=");
    publish_decimal(WIND_AIR_PRESSURE_PA);
    serial_write("# WIND_MODEL temperature_c=");
    publish_decimal(WIND_AIR_TEMPERATURE_C);
    serial_write("# WIND_MODEL gravity_m_s2=");
    publish_decimal(WIND_GRAVITY_M_S2);
    serial_write("# WIND_MODEL speed_gain=");
    publish_decimal(wind->speed_gain);
    serial_write("# CALIBRATION=rough_20261104_run; restored gain 0.55; unstable mount; UNVALIDATED\r\n");
    serial_write("# WIND_MODEL force_polarity=");
    serial_signed(wind->force_polarity);
    serial_write("; Cd and air density assumed; no wind-tunnel validation\r\n");
}

static void publish_decimal(float grams)
{
    float magnitude = grams < 0 ? -grams : grams;
    uint32_t whole = (uint32_t)magnitude;
    uint32_t fraction = (uint32_t)((magnitude - (float)whole) * 1000000.0f + 0.5f);
    if (fraction == 1000000u) {
        ++whole;
        fraction = 0;
    }
    if (grams < 0 && (whole != 0 || fraction != 0)) {
        serial_write("-");
    }
    serial_unsigned(whole);
    serial_write(".");
    for (uint32_t divisor = 100000u; divisor != 0; divisor /= 10u) {
        serial_unsigned(fraction / divisor);
        fraction %= divisor;
    }
    serial_write("\r\n");
}

static void measured_diagnostic(uint32_t timestamp, int32_t sample)
{
    serial_write("# ADC timestamp_ms=");
    serial_unsigned(timestamp);
    serial_write(" raw_counts=");
    serial_signed(sample);
    serial_write("\r\n");
}

void reset_handler(void)
{
    uint32_t *source = &_data_load;
    for (uint32_t *destination = &_data_start; destination < &_data_end;) {
        *destination++ = *source++;
    }
    for (uint32_t *destination = &_bss_start; destination < &_bss_end;) {
        *destination++ = 0;
    }
    board_init();
    hx711_init();
    interpolation_history history;
    interpolation_clear(&history);
    mass_calibration scale;
    mass_calibration_start(&scale);
    wind_model wind;
    wind_model_defaults(&wind);
    uint32_t consecutive = 0;
    uint32_t last_announcement = millis();
    uint32_t last_measurement = millis();
    uint32_t next_output = 0;
    bool streaming = false;
    announce(&scale, &wind);
    serial_write("# TARE: fan OFF, no masses; settle 50 then average 32 measured conversions (about 8 seconds)\r\n");
    while (1) {
        serial_poll();
        uint32_t now = millis();
        if ((uint32_t)(now - last_announcement) >= 2000u) {
            announce(&scale, &wind);
            last_announcement = now;
        }
        hx711_result result = HX711_OK;
        if (hx711_is_ready()) {
            int32_t sample;
            result = hx711_read(&sample);
            if (result == HX711_OK) {
                last_measurement = now;
                measured_diagnostic(now, sample);
                if (sample <= -8388608 || sample >= 8388607) {
                    streaming = false;
                    interpolation_clear(&history);
                    if (scale.tare_samples < MASS_TARE_SAMPLES) {
                        mass_calibration_start(&scale);
                    }
                    serial_write("# ERROR: HX711_SATURATED: wind output paused\r\n");
                    led_set(false);
                    continue;
                }
                if (scale.tare_samples < MASS_TARE_SAMPLES && mass_calibration_tare(&scale, sample)) {
                    serial_write("# TARE_COMPLETE: startup zero established; numeric output is estimated m/s\r\n");
                    announce(&scale, &wind);
                }
                if (scale.tare_samples == MASS_TARE_SAMPLES) {
                    interpolation_push(&history, now, sample);
                }
                if (consecutive < 5u && ++consecutive == 5u) {
                    serial_write("# HX711_LINK_OK: 5 measured conversions; numeric output is interpolated\r\n");
                }
            }
        } else if ((uint32_t)(now - last_measurement) >= HX711_READY_TIMEOUT_MS) {
            result = HX711_READY_TIMEOUT;
        }
        if (result != HX711_OK) {
            consecutive = 0;
            streaming = false;
            interpolation_clear(&history);
            if (scale.tare_samples < MASS_TARE_SAMPLES) {
                mass_calibration_start(&scale);
            }
            led_set(false);
            if (result == HX711_READY_TIMEOUT) {
                serial_write("# ERROR: HX711_READY_TIMEOUT: DT stayed high; check 3V3, GND, DT and SCK.\r\n");
            } else {
                serial_write("# ERROR: HX711_DOUT_STUCK_LOW: DT did not go high after 25 clocks; check DT/SCK wiring.\r\n");
            }
            hx711_reset();
            delay_ms(500);
            last_measurement = millis();
            continue;
        }
        int32_t interpolated;
        if (!streaming && interpolation_at(&history, now - INTERPOLATION_DELAY_MS, &interpolated)) {
            streaming = true;
            next_output = now;
        }
        if (streaming && (int32_t)(now - next_output) >= 0) {
            if (interpolation_at(&history, next_output - INTERPOLATION_DELAY_MS, &interpolated)) {
                float grams;
                if (mass_calibration_grams(&scale, interpolated, &grams)) {
                    float speed;
                    if (wind_model_speed_mps(&wind, grams, &speed)) {
                        publish_decimal(speed);
                    } else {
                        serial_write("# ERROR: WIND_MODEL_INVALID: check model parameters\r\n");
                    }
                }
                next_output += OUTPUT_PERIOD_MS;
            } else {
                /* Never extrapolate or hold old values when fresh ADC data stop. */
                streaming = false;
                serial_write("# WAITING: interpolation paused until measured samples bracket the output time\r\n");
            }
        }
        led_set(streaming && now % 1000u < 500u);
    }
}
