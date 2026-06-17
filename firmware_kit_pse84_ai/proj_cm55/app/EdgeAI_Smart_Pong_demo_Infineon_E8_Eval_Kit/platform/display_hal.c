#include "platform/display_hal.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "cy_graphics.h"
#include "lv_port_disp.h"

static uint16_t *s_fb_front = NULL;
static uint16_t *s_fb_back = NULL;
static int32_t s_x_off = 0;
static int32_t s_y_off = 0;
static uint32_t s_vw = 0;
static uint32_t s_vh = 0;

static inline bool display_hal_is_ready(void)
{
    return (s_fb_front != NULL) && (s_fb_back != NULL) && (s_vw > 0u) && (s_vh > 0u);
}

static inline void display_hal_present(void)
{
    /* Wait for frame boundary, then atomically flip to the completed back buffer. */
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(8));
    SCB_CleanDCache();
    Cy_GFXSS_Set_FrameBuffer((GFXSS_Type *)GFXSS, (uint32_t *)s_fb_back, &gfx_context);
    uint16_t *tmp = s_fb_front;
    s_fb_front = s_fb_back;
    s_fb_back = tmp;
}

bool display_hal_init(void)
{
    s_fb_front = (uint16_t *)frame_buffer1;
    s_fb_back = (uint16_t *)frame_buffer2;
    s_vw = (uint32_t)ACTUAL_DISP_HOR_RES;
    s_vh = (uint32_t)ACTUAL_DISP_VER_RES;

    if ((s_vw > EDGEAI_LCD_W) && (s_vh >= EDGEAI_LCD_H))
    {
        s_x_off = (int32_t)((s_vw - EDGEAI_LCD_W) / 2u);
    }
    else
    {
        s_x_off = 0;
    }

    if ((s_vh > EDGEAI_LCD_H) && (s_vw >= EDGEAI_LCD_W))
    {
        s_y_off = (int32_t)((s_vh - EDGEAI_LCD_H) / 2u);
    }
    else
    {
        s_y_off = 0;
    }

    if (!display_hal_is_ready()) return false;

    SCB_CleanDCache();
    Cy_GFXSS_Set_FrameBuffer((GFXSS_Type *)GFXSS, (uint32_t *)s_fb_front, &gfx_context);
    return true;
}

void display_hal_fill(uint16_t rgb565)
{
    if (!display_hal_is_ready()) return;

    for (uint32_t y = 0; y < s_vh; y++)
    {
        uint16_t *row0 = &s_fb_front[y * (uint32_t)MY_DISP_HOR_RES];
        uint16_t *row1 = &s_fb_back[y * (uint32_t)MY_DISP_HOR_RES];
        for (uint32_t x = 0; x < s_vw; x++)
        {
            row0[x] = rgb565;
            row1[x] = rgb565;
        }
    }
    display_hal_present();
}

void display_hal_fill_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t rgb565)
{
    if (!display_hal_is_ready()) return;
    if (x1 < x0 || y1 < y0) return;

    x0 += s_x_off;
    y0 += s_y_off;
    x1 += s_x_off;
    y1 += s_y_off;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= (int32_t)s_vw) x1 = (int32_t)s_vw - 1;
    if (y1 >= (int32_t)s_vh) y1 = (int32_t)s_vh - 1;
    if (x1 < x0 || y1 < y0) return;

    for (int32_t y = y0; y <= y1; y++)
    {
        uint16_t *row = &s_fb_back[(uint32_t)y * (uint32_t)MY_DISP_HOR_RES];
        for (int32_t x = x0; x <= x1; x++)
        {
            row[(uint32_t)x] = rgb565;
        }
    }
}

void display_hal_blit_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t *rgb565)
{
    if (!display_hal_is_ready() || (rgb565 == NULL)) return;
    if (x1 < x0 || y1 < y0) return;

    int32_t src_w = (x1 - x0) + 1;
    int32_t src_h = (y1 - y0) + 1;

    int32_t dst_x0 = x0 + s_x_off;
    int32_t dst_y0 = y0 + s_y_off;

    for (int32_t r = 0; r < src_h; r++)
    {
        int32_t dy = dst_y0 + r;
        if ((dy < 0) || (dy >= (int32_t)s_vh)) continue;

        int32_t sx0 = 0;
        int32_t dx0 = dst_x0;
        int32_t copy_w = src_w;

        if (dx0 < 0)
        {
            sx0 = -dx0;
            copy_w -= sx0;
            dx0 = 0;
        }
        if ((dx0 + copy_w) > (int32_t)s_vw)
        {
            copy_w = (int32_t)s_vw - dx0;
        }
        if (copy_w <= 0) continue;

        const uint16_t *src_row = &rgb565[(uint32_t)r * (uint32_t)src_w + (uint32_t)sx0];
        uint16_t *dst_row = &s_fb_back[(uint32_t)dy * (uint32_t)MY_DISP_HOR_RES + (uint32_t)dx0];
        memcpy(dst_row, src_row, (size_t)copy_w * sizeof(uint16_t));
    }
}

void display_hal_present_frame(void)
{
    if (!display_hal_is_ready()) return;
    display_hal_present();
}
