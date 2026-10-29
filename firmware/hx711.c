#include "board.h"
#include "config.h"
#include "hx711.h"

_Static_assert(HX711_DOUT_PIN == 8u || HX711_DOUT_PIN == 9u, "Use PA8 or PA9");
_Static_assert(HX711_SCK_PIN == 8u || HX711_SCK_PIN == 9u, "Use PA8 or PA9");
_Static_assert(HX711_DOUT_PIN != HX711_SCK_PIN, "DT and SCK must differ");

static bool dout_high(void)
{
    return (GPIOA_IDR & (1u << HX711_DOUT_PIN)) != 0;
}

bool hx711_is_ready(void)
{
    return !dout_high();
}

static void clock_set(bool high)
{
    GPIOA_BSRR = 1u << (HX711_SCK_PIN + (high ? 0u : 16u));
}

void hx711_reset(void)
{
    /* Deliberate power down, then restart at channel A / gain 128. */
    clock_set(true);
    delay_us(100);
    clock_set(false);
}

void hx711_init(void)
{
    clock_set(false);
    GPIOA_OTYPER &= ~(1u << HX711_SCK_PIN);
    GPIOA_OSPEEDR &= ~(3u << (HX711_SCK_PIN * 2u));
    /* A disconnected DT stays high, rather than producing fake zero samples. */
    GPIOA_PUPDR = (GPIOA_PUPDR & ~((3u << (HX711_DOUT_PIN * 2u))
                    | (3u << (HX711_SCK_PIN * 2u))))
                    | (1u << (HX711_DOUT_PIN * 2u));
    GPIOA_MODER = (GPIOA_MODER & ~((3u << (HX711_DOUT_PIN * 2u))
                    | (3u << (HX711_SCK_PIN * 2u))))
                    | (1u << (HX711_SCK_PIN * 2u));
    hx711_reset();
}

hx711_result hx711_read(int32_t *sample)
{
    uint32_t start = millis();
    while (dout_high()) {
        if ((uint32_t)(millis() - start) >= HX711_READY_TIMEOUT_MS) {
            return HX711_READY_TIMEOUT;
        }
    }
    delay_us(1);
    uint32_t bits = 0;
    uint32_t interrupt_state;
    /* Keep interrupts from stretching SCK high into power-down (>60 us). */
    __asm volatile ("mrs %0, primask\n cpsid i" : "=r" (interrupt_state) :: "memory");
    for (uint32_t bit = 0; bit < 24u; ++bit) {
        clock_set(true);
        delay_us(1);
        bits = (bits << 1) | (dout_high() ? 1u : 0u);
        clock_set(false);
        delay_us(1);
    }
    /* Pulse 25 selects channel A, gain 128, and must drive DOUT high. */
    clock_set(true);
    delay_us(1);
    clock_set(false);
    delay_us(1);
    bool conversion_started = dout_high();
    __asm volatile ("msr primask, %0" :: "r" (interrupt_state) : "memory");
    if (!conversion_started) {
        return HX711_DOUT_STUCK_LOW;
    }
    *sample = (bits & 0x800000u) ? (int32_t)bits - 16777216 : (int32_t)bits;
    return HX711_OK;
}
