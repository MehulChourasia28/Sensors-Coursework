#include "board.h"

#define RCC_AHB1ENR REGISTER(0x40023830u)
#define RCC_APB1ENR REGISTER(0x40023840u)
#define GPIOA_AFRL REGISTER(0x40020020u)
#define USART2_SR REGISTER(0x40004400u)
#define USART2_DR REGISTER(0x40004404u)
#define USART2_BRR REGISTER(0x40004408u)
#define USART2_CR1 REGISTER(0x4000440cu)
#define SYSTICK_CTRL REGISTER(0xe000e010u)
#define SYSTICK_LOAD REGISTER(0xe000e014u)
#define SYSTICK_VAL REGISTER(0xe000e018u)
#define DEMCR REGISTER(0xe000edfcu)
#define DWT_CTRL REGISTER(0xe0001000u)
#define DWT_CYCCNT REGISTER(0xe0001004u)

static volatile uint32_t uptime_ms;
static char serial_buffer[512];
static uint32_t serial_head;
static uint32_t serial_tail;

void systick_handler(void)
{
    ++uptime_ms;
}

void board_init(void)
{
    /* Hardware reset selects the internal 16 MHz HSI. */
    RCC_AHB1ENR |= 1u;
    RCC_APB1ENR |= 1u << 17;
    (void)RCC_AHB1ENR;
    (void)RCC_APB1ENR;
    GPIOA_BSRR = 1u << 21;
    GPIOA_MODER = (GPIOA_MODER & ~((3u << 10) | (3u << 4)))
                  | (1u << 10) | (2u << 4);
    GPIOA_AFRL = (GPIOA_AFRL & ~(15u << 8)) | (7u << 8);
    USART2_BRR = 139u;
    USART2_CR1 = (1u << 13) | (1u << 3);
    DEMCR |= 1u << 24;
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1u;
    SYSTICK_LOAD = 16000u - 1u;
    SYSTICK_VAL = 0;
    SYSTICK_CTRL = 7u;
}

uint32_t millis(void)
{
    return uptime_ms;
}

void delay_ms(uint32_t milliseconds)
{
    uint32_t start = millis();
    while ((uint32_t)(millis() - start) < milliseconds) {
        serial_poll();
    }
}

void delay_us(uint32_t microseconds)
{
    uint32_t start = DWT_CYCCNT;
    while ((uint32_t)(DWT_CYCCNT - start) < microseconds * 16u) {
    }
}

void serial_poll(void)
{
    if (serial_head != serial_tail && (USART2_SR & (1u << 7)) != 0) {
        USART2_DR = (uint8_t)serial_buffer[serial_tail];
        serial_tail = (serial_tail + 1u) % sizeof(serial_buffer);
    }
}

void serial_write(const char *message)
{
    while (*message != '\0') {
        uint32_t next = (serial_head + 1u) % sizeof(serial_buffer);
        while (next == serial_tail) {
            serial_poll();
        }
        serial_buffer[serial_head] = *message++;
        serial_head = next;
    }
}

void serial_unsigned(uint32_t value)
{
    char buffer[11];
    char *position = &buffer[10];
    *position = '\0';
    do {
        *--position = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0);
    serial_write(position);
}

void serial_signed(int32_t value)
{
    if (value < 0) {
        serial_write("-");
        serial_unsigned(0u - (uint32_t)value);
    } else {
        serial_unsigned((uint32_t)value);
    }
}

void led_set(bool on)
{
    GPIOA_BSRR = on ? 1u << 5 : 1u << 21;
}
