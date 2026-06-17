#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "edgeai_config.h"

bool display_hal_init(void);
void display_hal_fill(uint16_t rgb565);
void display_hal_fill_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t rgb565);
void display_hal_blit_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t *rgb565);
void display_hal_present_frame(void);
