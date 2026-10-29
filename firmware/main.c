#include "board.h"
#include "config.h"
#include "hx711.h"
#include "interpolation.h"

_Static_assert(DISPLAY_POLARITY == 1 || DISPLAY_POLARITY == -1, "Display polarity must be +1 or -1");
_Static_assert(DISPLAY_COUNTS_PER_UNIT > 0u && DISPLAY_COUNTS_PER_UNIT <= 1000000u, "Invalid display scale");

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

static void announce(void)
{
    serial_write("# FIRMWARE=" FIRMWARE_ID "; MCU=STM32F411RE; HSI=16000000; UART=115200\r\n");
    serial_write("# HX711 channel=A gain=128 DOUT=PA");
    serial_unsigned(HX711_DOUT_PIN);
    serial_write(" SCK=PA");
    serial_unsigned(HX711_SCK_PIN);
    serial_write("; power=3V3; measured raw counts in ADC comments\r\n");
    serial_write("# UNITS=normalized_zeroed_counts; PROVISIONAL display; NOT calibrated m/s\r\n");
    serial_write("# SCALE zero_offset_counts=");
    serial_signed(ZERO_OFFSET_COUNTS);
    serial_write(" counts_per_unit=");
    serial_unsigned(DISPLAY_COUNTS_PER_UNIT);
    serial_write(" polarity=");
    serial_signed(DISPLAY_POLARITY);
    serial_write("\r\n");
    serial_write("# INTERPOLATION=linear; output_hz=200; delay_ms=150; ADC rate unchanged\r\n");
}

static void publish_normalized(int32_t sample)
{
    int32_t zeroed = DISPLAY_POLARITY * (sample - ZERO_OFFSET_COUNTS);
    uint32_t magnitude = zeroed < 0 ? 0u - (uint32_t)zeroed : (uint32_t)zeroed;
    if (zeroed < 0) {
        serial_write("-");
    }
    serial_unsigned(magnitude / DISPLAY_COUNTS_PER_UNIT);
    serial_write(".");
    uint32_t fraction = magnitude % DISPLAY_COUNTS_PER_UNIT;
    for (uint32_t digit = 0; digit < 6u; ++digit) {
        fraction *= 10u;
        serial_unsigned(fraction / DISPLAY_COUNTS_PER_UNIT);
        fraction %= DISPLAY_COUNTS_PER_UNIT;
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
    uint32_t consecutive = 0;
    uint32_t last_announcement = millis();
    uint32_t last_measurement = millis();
    uint32_t next_output = 0;
    bool streaming = false;
    announce();
    while (1) {
        serial_poll();
        uint32_t now = millis();
        if ((uint32_t)(now - last_announcement) >= 2000u) {
            announce();
            last_announcement = now;
        }
        hx711_result result = HX711_OK;
        if (hx711_is_ready()) {
            int32_t sample;
            result = hx711_read(&sample);
            if (result == HX711_OK) {
                last_measurement = now;
                interpolation_push(&history, now, sample);
                measured_diagnostic(now, sample);
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
                publish_normalized(interpolated);
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
