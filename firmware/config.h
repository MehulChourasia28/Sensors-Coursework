#ifndef CONFIG_H
#define CONFIG_H

/* Nucleo Arduino D7 = PA8 (DT), D8 = PA9 (SCK). Power: 3V3 and GND. */
#define HX711_DOUT_PIN 8u
#define HX711_SCK_PIN 9u
#define HX711_READY_TIMEOUT_MS 1000u
#define OUTPUT_PERIOD_MS 5u
#define INTERPOLATION_DELAY_MS 150u
#define ZERO_OFFSET_COUNTS 491064
#define DISPLAY_COUNTS_PER_UNIT 100000u
#define DISPLAY_POLARITY (-1)
#define FIRMWARE_ID "nucleo-f411re-hx711-v5"

#endif
