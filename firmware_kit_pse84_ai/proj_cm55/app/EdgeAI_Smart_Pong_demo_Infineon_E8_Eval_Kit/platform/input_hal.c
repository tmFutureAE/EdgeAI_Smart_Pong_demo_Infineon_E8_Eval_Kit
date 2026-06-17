#include "platform/input_hal.h"

#include <string.h>

#include "edgeai_config.h"
#include "cy_scb_i2c.h"
#include "cy_syslib.h"
#include "cy_gpio.h"
#include "cycfg_peripherals.h"
#include "platform/bmi270/bmi270.h"

#define EDGEAI_I2C_RETRY_COUNT              (3u)
#define EDGEAI_I2C_TIMEOUT_MS               (12u)

#define BMI270_ADDR0                        (0x68u)
#define BMI270_ADDR1                        (0x69u)
#define CAPSENSE_I2C_ADDR_APP               (0x08u)
#define CAPSENSE_I2C_ADDR_BOOT_CMD          (0x09u)
#define CAPSENSE_I2C_READ_LEN               (3u)

#define CAPSENSE_I2C_BTN0_IDX               (0u)
#define CAPSENSE_I2C_BTN1_IDX               (1u)
#define CAPSENSE_I2C_SLIDER_IDX             (2u)
#define CAPSENSE_BTN0_NOT_PRESSED           (0u)
#define CAPSENSE_BTN1_NOT_PRESSED           (1u)

extern cy_stc_scb_i2c_context_t disp_touch_i2c_controller_context;

static struct bmi2_dev s_bmi2_dev;
static uint8_t s_bmi2_addr = BMI270_ADDR0;

static inline float clamp01f(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static inline float remap_touch_y(float ty, float ui_y_max)
{
    if (ty <= ui_y_max) return 0.0f;
    return clamp01f((ty - ui_y_max) / (1.0f - ui_y_max));
}

static inline float remap_touch_z_from_left_edge(float tx)
{
    return clamp01f(tx * (1.0f / EDGEAI_TOUCH_STRIP_W_NORM));
}

static inline float remap_touch_z_from_right_edge(float tx)
{
    float start = 1.0f - EDGEAI_TOUCH_STRIP_W_NORM;
    return clamp01f((tx - start) * (1.0f / EDGEAI_TOUCH_STRIP_W_NORM));
}

static inline float clamp1f(float v)
{
    if (v < -1.0f) return -1.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static inline float absf(float v)
{
    return (v < 0.0f) ? -v : v;
}

static inline uint8_t capsense_decode_btn_status(uint8_t raw)
{
    /* PSoC4000T CapSense app commonly sends ASCII digits for BTN bytes. */
    if ((raw >= (uint8_t)'0') && (raw <= (uint8_t)'9'))
    {
        return (uint8_t)(raw - (uint8_t)'0');
    }
    return raw;
}

static bool read_capsense_btn0_raw(void)
{
/* BTN0 / CS81 path: USER_BTN1 -> SW2 on this BSP. */
#if defined(CYBSP_USER_BTN1_PORT) && defined(CYBSP_USER_BTN1_NUM)
    return (Cy_GPIO_Read(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_NUM) != 0u);
#elif defined(CYBSP_SW2_PORT) && defined(CYBSP_SW2_NUM)
    return (Cy_GPIO_Read(CYBSP_SW2_PORT, CYBSP_SW2_NUM) != 0u);
#elif defined(CYBSP_CSD_BTN0_PORT) && defined(CYBSP_CSD_BTN0_NUM)
    return (Cy_GPIO_Read(CYBSP_CSD_BTN0_PORT, CYBSP_CSD_BTN0_NUM) != 0u);
#else
    return false;
#endif
}

static bool read_capsense_btn1_raw(void)
{
/* BTN1 / CS82 path: USER_BTN2 -> SW4 on this BSP. */
#if defined(CYBSP_USER_BTN2_PORT) && defined(CYBSP_USER_BTN2_NUM)
    return (Cy_GPIO_Read(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_NUM) != 0u);
#elif defined(CYBSP_SW4_PORT) && defined(CYBSP_SW4_NUM)
    return (Cy_GPIO_Read(CYBSP_SW4_PORT, CYBSP_SW4_NUM) != 0u);
#elif defined(CYBSP_CSD_BTN1_PORT) && defined(CYBSP_CSD_BTN1_NUM)
    return (Cy_GPIO_Read(CYBSP_CSD_BTN1_PORT, CYBSP_CSD_BTN1_NUM) != 0u);
#else
    return false;
#endif
}

static bool read_capsense_i2c_frame_addr(uint8_t addr7, uint8_t *out3)
{
    if (!out3) return false;

    for (uint32_t r = 0u; r < EDGEAI_I2C_RETRY_COUNT; r++)
    {
        cy_en_scb_i2c_status_t rc = CY_SCB_I2C_SUCCESS;

        if (disp_touch_i2c_controller_context.state == CY_SCB_I2C_IDLE)
        {
            rc = Cy_SCB_I2C_MasterSendStart(CYBSP_I2C_CONTROLLER_HW,
                                            addr7,
                                            CY_SCB_I2C_READ_XFER,
                                            EDGEAI_I2C_TIMEOUT_MS,
                                            &disp_touch_i2c_controller_context);
        }
        else
        {
            rc = Cy_SCB_I2C_MasterSendReStart(CYBSP_I2C_CONTROLLER_HW,
                                              addr7,
                                              CY_SCB_I2C_READ_XFER,
                                              EDGEAI_I2C_TIMEOUT_MS,
                                              &disp_touch_i2c_controller_context);
        }
        if (rc != CY_SCB_I2C_SUCCESS)
        {
            (void)Cy_SCB_I2C_MasterSendStop(CYBSP_I2C_CONTROLLER_HW,
                                            EDGEAI_I2C_TIMEOUT_MS,
                                            &disp_touch_i2c_controller_context);
            continue;
        }

        bool ok = true;
        for (uint32_t i = 0u; i < CAPSENSE_I2C_READ_LEN; i++)
        {
            cy_en_scb_i2c_command_t ack = (i + 1u < CAPSENSE_I2C_READ_LEN) ? CY_SCB_I2C_ACK : CY_SCB_I2C_NAK;
            rc = Cy_SCB_I2C_MasterReadByte(CYBSP_I2C_CONTROLLER_HW,
                                           ack,
                                           &out3[i],
                                           EDGEAI_I2C_TIMEOUT_MS,
                                           &disp_touch_i2c_controller_context);
            if (rc != CY_SCB_I2C_SUCCESS)
            {
                ok = false;
                break;
            }
        }

        (void)Cy_SCB_I2C_MasterSendStop(CYBSP_I2C_CONTROLLER_HW,
                                        EDGEAI_I2C_TIMEOUT_MS,
                                        &disp_touch_i2c_controller_context);
        if (ok) return true;
    }

    return false;
}

static bool read_capsense_i2c_frame(uint8_t *out3)
{
    if (read_capsense_i2c_frame_addr(CAPSENSE_I2C_ADDR_APP, out3))
    {
        return true;
    }
    return read_capsense_i2c_frame_addr(CAPSENSE_I2C_ADDR_BOOT_CMD, out3);
}

static void capsense_widget_scan_init(input_hal_t *s)
{
    if (!s) return;
    s->capsense_btn0_idle_raw = read_capsense_btn0_raw();
    s->capsense_btn1_idle_raw = read_capsense_btn1_raw();
    s->capsense_i2c_available = false;
    s->capsense_i2c_initialized = false;
    s->capsense_i2c_btn0_idle = CAPSENSE_BTN0_NOT_PRESSED;
    s->capsense_i2c_btn1_idle = CAPSENSE_BTN1_NOT_PRESSED;
    s->capsense_btn0_prev_status = 0u;
    s->capsense_btn1_prev_status = 0u;
    s->prev_vol_dn_raw = false;
    s->prev_vol_up_raw = false;
    s->capsense_btns_initialized = true;
}

static void capsense_widget_scan_poll(input_hal_t *s, bool *btn0_pressed, bool *btn1_pressed)
{
    if (!btn0_pressed || !btn1_pressed) return;
    *btn0_pressed = false;
    *btn1_pressed = false;
    if (!s) return;
    if (!s->capsense_btns_initialized)
    {
        capsense_widget_scan_init(s);
    }

    bool i2c_btn0_pressed = false;
    bool i2c_btn1_pressed = false;
    uint8_t frame[CAPSENSE_I2C_READ_LEN] = { 0u };

    /* Preferred path: read dedicated PSoC4000T CapSense status over I2C. */
    if (read_capsense_i2c_frame(frame))
    {
        uint8_t btn0 = capsense_decode_btn_status(frame[CAPSENSE_I2C_BTN0_IDX]);
        uint8_t btn1 = capsense_decode_btn_status(frame[CAPSENSE_I2C_BTN1_IDX]);

        s->capsense_i2c_available = true;
        if (!s->capsense_i2c_initialized)
        {
            s->capsense_btn0_prev_status = 0u;
            s->capsense_btn1_prev_status = 0u;
            s->capsense_i2c_initialized = true;
        }

        /* Match Infineon reference protocol:
         * BTN0 idle = 0, BTN1 idle = 1, active = any non-idle value. */
        bool btn0_active = (btn0 != CAPSENSE_BTN0_NOT_PRESSED);
        bool btn1_active = (btn1 != CAPSENSE_BTN1_NOT_PRESSED);
        i2c_btn0_pressed = (btn0_active && (s->capsense_btn0_prev_status == 0u));
        i2c_btn1_pressed = (btn1_active && (s->capsense_btn1_prev_status == 0u));
        s->capsense_btn0_prev_status = btn0_active ? 1u : 0u;
        s->capsense_btn1_prev_status = btn1_active ? 1u : 0u;
    }
    else
    {
        /* If I2C probe fails, gracefully fall back to GPIO alias path. */
        s->capsense_i2c_available = false;
        s->capsense_i2c_initialized = false;
        s->capsense_btn0_prev_status = 0u;
        s->capsense_btn1_prev_status = 0u;
    }

    /* Treat non-idle state as active to tolerate polarity differences. */
    bool btn0_active = (read_capsense_btn0_raw() != s->capsense_btn0_idle_raw);
    bool btn1_active = (read_capsense_btn1_raw() != s->capsense_btn1_idle_raw);

    /* Rising edge on active state = one press event (widget activation). */
    bool gpio_btn0_pressed = (btn0_active && !s->prev_vol_dn_raw);
    bool gpio_btn1_pressed = (btn1_active && !s->prev_vol_up_raw);

    *btn0_pressed = (i2c_btn0_pressed || gpio_btn0_pressed);
    *btn1_pressed = (i2c_btn1_pressed || gpio_btn1_pressed);

    s->prev_vol_dn_raw = btn0_active;
    s->prev_vol_up_raw = btn1_active;
}

static bool input_i2c_reg_read(uint8_t addr7, uint8_t reg, uint8_t *buf, uint32_t len)
{
    if (!buf || (len == 0u)) return false;

    for (uint32_t r = 0u; r < EDGEAI_I2C_RETRY_COUNT; r++)
    {
        cy_en_scb_i2c_status_t rc = Cy_SCB_I2C_MasterSendStart(CYBSP_I2C_CONTROLLER_HW, addr7,
                                                                CY_SCB_I2C_WRITE_XFER, EDGEAI_I2C_TIMEOUT_MS,
                                                                &disp_touch_i2c_controller_context);
        if (rc != CY_SCB_I2C_SUCCESS) continue;

        rc = Cy_SCB_I2C_MasterWriteByte(CYBSP_I2C_CONTROLLER_HW, reg, EDGEAI_I2C_TIMEOUT_MS,
                                        &disp_touch_i2c_controller_context);
        if (rc != CY_SCB_I2C_SUCCESS)
        {
            (void)Cy_SCB_I2C_MasterSendStop(CYBSP_I2C_CONTROLLER_HW, EDGEAI_I2C_TIMEOUT_MS,
                                            &disp_touch_i2c_controller_context);
            continue;
        }

        rc = Cy_SCB_I2C_MasterSendReStart(CYBSP_I2C_CONTROLLER_HW, addr7, CY_SCB_I2C_READ_XFER,
                                          EDGEAI_I2C_TIMEOUT_MS, &disp_touch_i2c_controller_context);
        if (rc != CY_SCB_I2C_SUCCESS)
        {
            (void)Cy_SCB_I2C_MasterSendStop(CYBSP_I2C_CONTROLLER_HW, EDGEAI_I2C_TIMEOUT_MS,
                                            &disp_touch_i2c_controller_context);
            continue;
        }

        bool ok = true;
        for (uint32_t i = 0u; i < len; i++)
        {
            cy_en_scb_i2c_command_t ack = (i + 1u < len) ? CY_SCB_I2C_ACK : CY_SCB_I2C_NAK;
            rc = Cy_SCB_I2C_MasterReadByte(CYBSP_I2C_CONTROLLER_HW, ack, &buf[i], EDGEAI_I2C_TIMEOUT_MS,
                                           &disp_touch_i2c_controller_context);
            if (rc != CY_SCB_I2C_SUCCESS)
            {
                ok = false;
                break;
            }
        }

        (void)Cy_SCB_I2C_MasterSendStop(CYBSP_I2C_CONTROLLER_HW, EDGEAI_I2C_TIMEOUT_MS,
                                        &disp_touch_i2c_controller_context);
        if (ok) return true;
    }

    return false;
}

static bool input_i2c_reg_write(uint8_t addr7, uint8_t reg, const uint8_t *data, uint32_t len)
{
    if (!data || (len == 0u)) return false;

    for (uint32_t r = 0u; r < EDGEAI_I2C_RETRY_COUNT; r++)
    {
        cy_en_scb_i2c_status_t rc = Cy_SCB_I2C_MasterSendStart(CYBSP_I2C_CONTROLLER_HW, addr7,
                                                                CY_SCB_I2C_WRITE_XFER, EDGEAI_I2C_TIMEOUT_MS,
                                                                &disp_touch_i2c_controller_context);
        if (rc != CY_SCB_I2C_SUCCESS) continue;

        rc = Cy_SCB_I2C_MasterWriteByte(CYBSP_I2C_CONTROLLER_HW, reg, EDGEAI_I2C_TIMEOUT_MS,
                                        &disp_touch_i2c_controller_context);
        if (rc != CY_SCB_I2C_SUCCESS)
        {
            (void)Cy_SCB_I2C_MasterSendStop(CYBSP_I2C_CONTROLLER_HW, EDGEAI_I2C_TIMEOUT_MS,
                                            &disp_touch_i2c_controller_context);
            continue;
        }

        bool ok = true;
        for (uint32_t i = 0u; i < len; i++)
        {
            rc = Cy_SCB_I2C_MasterWriteByte(CYBSP_I2C_CONTROLLER_HW, data[i], EDGEAI_I2C_TIMEOUT_MS,
                                            &disp_touch_i2c_controller_context);
            if (rc != CY_SCB_I2C_SUCCESS)
            {
                ok = false;
                break;
            }
        }

        (void)Cy_SCB_I2C_MasterSendStop(CYBSP_I2C_CONTROLLER_HW, EDGEAI_I2C_TIMEOUT_MS,
                                        &disp_touch_i2c_controller_context);
        if (ok) return true;
    }

    return false;
}

static BMI2_INTF_RETURN_TYPE bmi2_i2c_read_cb(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    uint8_t addr = intf_ptr ? *((uint8_t *)intf_ptr) : BMI270_ADDR0;
    return input_i2c_reg_read(addr, reg_addr, reg_data, len) ? BMI2_INTF_RET_SUCCESS : BMI2_E_COM_FAIL;
}

static BMI2_INTF_RETURN_TYPE bmi2_i2c_write_cb(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    uint8_t addr = intf_ptr ? *((uint8_t *)intf_ptr) : BMI270_ADDR0;
    return input_i2c_reg_write(addr, reg_addr, reg_data, len) ? BMI2_INTF_RET_SUCCESS : BMI2_E_COM_FAIL;
}

static void bmi2_delay_us_cb(uint32_t period, void *intf_ptr)
{
    (void)intf_ptr;
    uint32_t ms = (period + 999u) / 1000u;
    if (ms == 0u) ms = 1u;
    Cy_SysLib_Delay(ms);
}

static bool bmi270_probe_and_init(input_hal_t *s)
{
    const uint8_t addrs[] = { BMI270_ADDR0, BMI270_ADDR1 };

    for (uint32_t i = 0u; i < (sizeof(addrs) / sizeof(addrs[0])); i++)
    {
        s_bmi2_addr = addrs[i];
        memset(&s_bmi2_dev, 0, sizeof(s_bmi2_dev));
        s_bmi2_dev.read = bmi2_i2c_read_cb;
        s_bmi2_dev.write = bmi2_i2c_write_cb;
        s_bmi2_dev.delay_us = bmi2_delay_us_cb;
        s_bmi2_dev.intf = BMI2_I2C_INTF;
        s_bmi2_dev.intf_ptr = &s_bmi2_addr;
        s_bmi2_dev.read_write_len = 32;

        if (bmi270_init(&s_bmi2_dev) != BMI2_OK) continue;

        (void)bmi2_set_adv_power_save(BMI2_DISABLE, &s_bmi2_dev);

        struct bmi2_sens_config cfg;
        uint8_t sensor = BMI2_ACCEL;
        cfg.type = BMI2_ACCEL;

        if (bmi2_sensor_enable(&sensor, 1, &s_bmi2_dev) != BMI2_OK) continue;
        if (bmi2_get_sensor_config(&cfg, 1, &s_bmi2_dev) != BMI2_OK) continue;

        cfg.cfg.acc.odr = BMI2_ACC_ODR_100HZ;
        cfg.cfg.acc.range = BMI2_ACC_RANGE_4G;
        cfg.cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
        cfg.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;

        if (bmi2_set_sensor_config(&cfg, 1, &s_bmi2_dev) != BMI2_OK) continue;
        Cy_SysLib_Delay(10u);

        s->accel_addr7 = addrs[i];
        s->accel_present = true;
        return true;
    }

    s->accel_present = false;
    s->accel_addr7 = 0u;
    return false;
}

static bool bmi270_read_accel_norm(input_hal_t *s, float *ax, float *ay, float *az)
{
    struct bmi2_sens_data data = { { 0 } };

    if (!s || !ax || !ay || !az || !s->accel_present) return false;
    if (bmi2_get_sensor_data(&data, &s_bmi2_dev) != BMI2_OK) return false;

    /* +/-4g range */
    const float lsb_per_g = 8192.0f;
    float gx = (float)data.acc.x / lsb_per_g;
    float gy = (float)data.acc.y / lsb_per_g;

    float gz = (float)data.acc.z / lsb_per_g;

    *ax = gx;
    *ay = gy;
    *az = gz;
    return true;
}

bool input_hal_init(input_hal_t *s)
{
    if (!s) return false;
    memset(s, 0, sizeof(*s));
    touch_hal_init();
    (void)bmi270_probe_and_init(s);
    return true;
}

void input_hal_poll(input_hal_t *s, platform_input_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    out->p1_y = 0.5f;
    out->p1_z = 0.5f;
    out->p2_y = 0.5f;
    out->p2_z = 0.5f;
    out->accel_active = false;
    out->accel_ax = 0.0f;
    out->accel_ay = 0.0f;

    capsense_widget_scan_poll(s, &out->vol_dn_pressed, &out->vol_up_pressed);

    if (s && s->accel_present)
    {
        float ax = 0.0f;
        float ay = 0.0f;
        float az = 0.0f;
        if (bmi270_read_accel_norm(s, &ax, &ay, &az))
        {
            const float alpha = 0.18f;
            s->accel_ax_lp += alpha * (ax - s->accel_ax_lp);
            s->accel_ay_lp += alpha * (ay - s->accel_ay_lp);
            s->accel_az_lp += alpha * (az - s->accel_az_lp);

            out->accel_active = true;
            /* Output axes swapped from prior mapping:
             *   UI/game X <- sensor X
             *   UI/game Y <- sensor Y
             */
            float ax_out = s->accel_ax_lp;
            float ay_out = -s->accel_ay_lp;
            if (absf(ax_out) < 0.02f) ax_out = 0.0f;
            if (absf(ay_out) < 0.02f) ay_out = 0.0f;
            out->accel_ax = clamp1f(ax_out);
            out->accel_ay = clamp1f(ay_out);
            out->accel_bang = ((out->accel_ax > 0.9f) || (out->accel_ax < -0.9f));
        }
    }

    edgeai_touch_state_t ts;
    touch_hal_poll(&ts);
    if (ts.count == 0u)
    {
        if (s) s->prev_touch_active = false;
        return;
    }

    out->touch_active = true;
    out->touch_x = ts.points[0].x;
    out->touch_y = ts.points[0].y;
    out->touch_pressed = s ? (!s->prev_touch_active) : true;

    const float ui_y_max = (float)EDGEAI_UI_BAR_H / (float)(EDGEAI_LCD_H - 1);

    for (uint32_t i = 0u; (i < ts.count) && (i < 2u); i++)
    {
        float tx = clamp01f(ts.points[i].x);
        float ty = clamp01f(ts.points[i].y);
        if (ty < ui_y_max) continue;

        if (tx <= EDGEAI_TOUCH_STRIP_W_NORM)
        {
            out->p1_active = true;
            out->p1_y = remap_touch_y(ty, ui_y_max);
            out->p1_z = remap_touch_z_from_left_edge(tx);
        }
        else if (tx >= (1.0f - EDGEAI_TOUCH_STRIP_W_NORM))
        {
            out->p2_active = true;
            out->p2_y = remap_touch_y(ty, ui_y_max);
            out->p2_z = 1.0f - remap_touch_z_from_right_edge(tx);
        }
    }

    if (s) s->prev_touch_active = true;
}
