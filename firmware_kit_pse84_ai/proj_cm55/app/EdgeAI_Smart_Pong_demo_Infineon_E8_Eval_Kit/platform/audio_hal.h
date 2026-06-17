#pragma once

#include <stdbool.h>
#include <stdint.h>

bool audio_hal_init(void);
void audio_hal_update(void);
void audio_hal_set_volume(uint8_t percent);
void audio_hal_queue_wall_bounce(uint8_t count);
void audio_hal_queue_paddle_hit(uint8_t count);
void audio_hal_queue_point_scored(uint8_t count);
void audio_hal_queue_win_tune(void);
