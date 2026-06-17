#pragma once

#include <stdint.h>

void time_hal_init(void);
uint32_t time_hal_cycles(void);
uint32_t time_hal_cycles_to_us(uint32_t cycles);
uint32_t time_hal_elapsed_us(uint32_t start_cycles);
void time_hal_delay_us(uint32_t us);

