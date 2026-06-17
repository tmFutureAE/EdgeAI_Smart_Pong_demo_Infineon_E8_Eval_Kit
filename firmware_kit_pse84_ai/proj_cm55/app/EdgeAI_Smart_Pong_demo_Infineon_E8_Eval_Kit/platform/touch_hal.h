#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    bool active;
    float x;
    float y;
    uint8_t id;
} edgeai_touch_point_t;

typedef struct
{
    uint32_t count;
    edgeai_touch_point_t points[2];
} edgeai_touch_state_t;

void touch_hal_init(void);
void touch_hal_poll(edgeai_touch_state_t *out);
bool touch_hal_is_ok(void);
