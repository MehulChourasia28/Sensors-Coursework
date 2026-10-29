#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

#define REGISTER(address) (*(volatile uint32_t *)(address))
#define GPIOA_MODER REGISTER(0x40020000u)
#define GPIOA_OTYPER REGISTER(0x40020004u)
#define GPIOA_OSPEEDR REGISTER(0x40020008u)
#define GPIOA_PUPDR REGISTER(0x4002000cu)
#define GPIOA_IDR REGISTER(0x40020010u)
#define GPIOA_BSRR REGISTER(0x40020018u)

void board_init(void);
uint32_t millis(void);
void delay_ms(uint32_t milliseconds);
void delay_us(uint32_t microseconds);
void serial_write(const char *message);
void serial_poll(void);
void serial_unsigned(uint32_t value);
void serial_signed(int32_t value);
void led_set(bool on);

#endif
