#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "platform/touch_hal.h"

typedef struct
{
    bool p1_active;
    float p1_y;
    float p1_z;

    bool p2_active;
    float p2_y;
    float p2_z;

    bool mode_toggle;

    float accel_ax;
    float accel_ay;
    bool accel_active;
    bool accel_bang;

    bool touch_active;
    bool touch_pressed;
    float touch_x;
    float touch_y;

    bool vol_dn_pressed;
    bool vol_up_pressed;
} platform_input_t;

typedef struct
{
    bool prev_touch_active;
    bool capsense_btns_initialized;
    bool capsense_i2c_available;
    bool capsense_i2c_initialized;
    bool capsense_btn0_idle_raw;
    bool capsense_btn1_idle_raw;
    uint8_t capsense_i2c_btn0_idle;
    uint8_t capsense_i2c_btn1_idle;
    uint8_t capsense_btn0_prev_status;
    uint8_t capsense_btn1_prev_status;
    bool prev_vol_dn_raw;
    bool prev_vol_up_raw;
    bool accel_present;
    uint8_t accel_addr7;
    float accel_ax_lp;
    float accel_ay_lp;
    float accel_az_lp;
    float accel_base_x;
    float accel_base_y;
    float accel_base_z;
    bool accel_base_valid;
    float accel_axis_x_activity;
    float accel_axis_y_activity;
    float accel_axis_z_activity;
    uint8_t accel_primary_axis;
    uint8_t accel_secondary_axis;
} input_hal_t;

bool input_hal_init(input_hal_t *s);
void input_hal_poll(input_hal_t *s, platform_input_t *out);
