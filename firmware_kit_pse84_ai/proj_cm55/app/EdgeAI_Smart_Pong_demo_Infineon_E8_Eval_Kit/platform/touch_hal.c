#include "platform/touch_hal.h"

#include <string.h>

#include "edgeai_config.h"
#include "lv_port_disp.h"
#include "mtb_ctp_ft5406.h"

static bool s_touch_ok = true;

void touch_hal_init(void)
{
    s_touch_ok = true;
}

void touch_hal_poll(edgeai_touch_state_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    int touch_x = 0;
    int touch_y = 0;

    mtb_ctp_touch_event_t touch_event;
    cy_rslt_t rslt = (cy_rslt_t)mtb_ctp_ft5406_get_single_touch(&touch_event, &touch_x, &touch_y);
    if (CY_RSLT_SUCCESS != rslt)
    {
        s_touch_ok = false;
        return;
    }

    if ((touch_event != MTB_CTP_TOUCH_DOWN) && (touch_event != MTB_CTP_TOUCH_CONTACT))
    {
        s_touch_ok = true;
        return;
    }

    s_touch_ok = true;

    {
        int32_t x = (int32_t)ACTUAL_DISP_HOR_RES - (int32_t)touch_x;
        int32_t y = (int32_t)ACTUAL_DISP_VER_RES - (int32_t)touch_y;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= (int32_t)ACTUAL_DISP_HOR_RES) x = (int32_t)ACTUAL_DISP_HOR_RES - 1;
        if (y >= (int32_t)ACTUAL_DISP_VER_RES) y = (int32_t)ACTUAL_DISP_VER_RES - 1;

        int32_t x_off = 0;
        int32_t y_off = 0;
        if ((int32_t)ACTUAL_DISP_HOR_RES > EDGEAI_LCD_W)
        {
            x_off = ((int32_t)ACTUAL_DISP_HOR_RES - EDGEAI_LCD_W) / 2;
        }
        if ((int32_t)ACTUAL_DISP_VER_RES > EDGEAI_LCD_H)
        {
            y_off = ((int32_t)ACTUAL_DISP_VER_RES - EDGEAI_LCD_H) / 2;
        }

        int32_t vx = x - x_off;
        int32_t vy = y - y_off;
        if ((vx < 0) || (vy < 0) || (vx >= EDGEAI_LCD_W) || (vy >= EDGEAI_LCD_H))
        {
            return;
        }

        out->count = 1u;
        out->points[0].active = true;
        out->points[0].id = 0u;
        out->points[0].x = (float)vx / (float)(EDGEAI_LCD_W - 1);
        out->points[0].y = (float)vy / (float)(EDGEAI_LCD_H - 1);
    }
}

bool touch_hal_is_ok(void)
{
    return s_touch_ok;
}
