#include "sw_render.h"

#include <stdbool.h>
#include <string.h>

#include "edgeai_util.h"

static inline void sw_put(uint16_t *dst, uint32_t w, uint32_t h, int32_t x, int32_t y, uint16_t c)
{
    if ((uint32_t)x >= w || (uint32_t)y >= h) return;
    dst[(uint32_t)y * w + (uint32_t)x] = c;
}

void sw_render_clear(uint16_t *dst, uint32_t w, uint32_t h, uint16_t rgb565)
{
    if (!dst || w == 0 || h == 0) return;
    if (rgb565 == 0)
    {
        memset(dst, 0, (size_t)w * (size_t)h * sizeof(dst[0]));
        return;
    }
    for (uint32_t i = 0; i < w * h; i++) dst[i] = rgb565;
}

void sw_render_fill_rect(uint16_t *dst, uint32_t w, uint32_t h,
                         int32_t tile_x0, int32_t tile_y0,
                         int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                         uint16_t rgb565)
{
    if (!dst || w == 0 || h == 0) return;

    int32_t lx0 = x0 - tile_x0;
    int32_t ly0 = y0 - tile_y0;
    int32_t lx1 = x1 - tile_x0;
    int32_t ly1 = y1 - tile_y0;
    if (lx1 < lx0 || ly1 < ly0) return;

    if (lx0 < 0) lx0 = 0;
    if (ly0 < 0) ly0 = 0;
    if (lx1 >= (int32_t)w) lx1 = (int32_t)w - 1;
    if (ly1 >= (int32_t)h) ly1 = (int32_t)h - 1;
    if (lx1 < lx0 || ly1 < ly0) return;

    for (int32_t y = ly0; y <= ly1; y++)
    {
        uint16_t *row = &dst[(uint32_t)y * w];
        for (int32_t x = lx0; x <= lx1; x++) row[(uint32_t)x] = rgb565;
    }
}

void sw_render_line(uint16_t *dst, uint32_t w, uint32_t h,
                    int32_t tile_x0, int32_t tile_y0,
                    int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                    uint16_t rgb565)
{
    if (!dst) return;

    int32_t x0l = x0 - tile_x0;
    int32_t y0l = y0 - tile_y0;
    int32_t x1l = x1 - tile_x0;
    int32_t y1l = y1 - tile_y0;

    int32_t dx = edgeai_abs_i32(x1l - x0l);
    int32_t sx = (x0l < x1l) ? 1 : -1;
    int32_t dy = -edgeai_abs_i32(y1l - y0l);
    int32_t sy = (y0l < y1l) ? 1 : -1;
    int32_t err = dx + dy;

    int32_t x = x0l;
    int32_t y = y0l;
    for (;;)
    {
        sw_put(dst, w, h, x, y, rgb565);
        if (x == x1l && y == y1l) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y += sy;
        }
    }
}

void sw_render_filled_circle(uint16_t *dst, uint32_t w, uint32_t h,
                             int32_t tile_x0, int32_t tile_y0,
                             int32_t cx, int32_t cy, int32_t r, uint16_t rgb565)
{
    if (!dst || r <= 0) return;

    int32_t lc_x = cx - tile_x0;
    int32_t lc_y = cy - tile_y0;

    int32_t y_min = lc_y - r;
    int32_t y_max = lc_y + r;
    if (y_min < 0) y_min = 0;
    if (y_max >= (int32_t)h) y_max = (int32_t)h - 1;

    const int32_t r2 = r * r;
    for (int32_t y = y_min; y <= y_max; y++)
    {
        int32_t dy = y - lc_y;
        int32_t dy2 = dy * dy;
        if (dy2 > r2) continue;
        int32_t dx_max = (int32_t)edgeai_isqrt_u32((uint32_t)(r2 - dy2));
        int32_t x_min = lc_x - dx_max;
        int32_t x_max = lc_x + dx_max;
        if (x_min < 0) x_min = 0;
        if (x_max >= (int32_t)w) x_max = (int32_t)w - 1;
        uint16_t *row = &dst[(uint32_t)y * w];
        for (int32_t x = x_min; x <= x_max; x++) row[(uint32_t)x] = rgb565;
    }
}

static inline void sw_swap_pt(sw_point_t *a, sw_point_t *b)
{
    sw_point_t t = *a;
    *a = *b;
    *b = t;
}

static inline int32_t sw_div_fp16(int32_t num, int32_t den)
{
    if (den == 0) return 0;
    return (int32_t)(((int64_t)num << 16) / (int64_t)den);
}

void sw_render_fill_triangle(uint16_t *dst, uint32_t w, uint32_t h,
                             int32_t tile_x0, int32_t tile_y0,
                             sw_point_t p0, sw_point_t p1, sw_point_t p2,
                             uint16_t rgb565)
{
    if (!dst || w == 0 || h == 0) return;

    /* Convert to tile-local coords for rasterization. */
    p0.x -= tile_x0; p0.y -= tile_y0;
    p1.x -= tile_x0; p1.y -= tile_y0;
    p2.x -= tile_x0; p2.y -= tile_y0;

    /* Sort by y. */
    if (p1.y < p0.y) sw_swap_pt(&p0, &p1);
    if (p2.y < p0.y) sw_swap_pt(&p0, &p2);
    if (p2.y < p1.y) sw_swap_pt(&p1, &p2);

    if (p0.y == p2.y) return;

    int32_t y0 = p0.y;
    int32_t y1 = p1.y;
    int32_t y2 = p2.y;

    /* Clip y range to tile. */
    int32_t y_start = y0;
    int32_t y_end = y2;
    if (y_start < 0) y_start = 0;
    if (y_end >= (int32_t)h) y_end = (int32_t)h - 1;
    if (y_end < y_start) return;

    int32_t dxdy02 = sw_div_fp16(p2.x - p0.x, y2 - y0);
    int32_t dxdy01 = (y1 != y0) ? sw_div_fp16(p1.x - p0.x, y1 - y0) : 0;
    int32_t dxdy12 = (y2 != y1) ? sw_div_fp16(p2.x - p1.x, y2 - y1) : 0;

    int32_t x02_fp = p0.x << 16;
    int32_t x01_fp = p0.x << 16;
    int32_t x12_fp = p1.x << 16;

    /* Start at y_start. */
    x02_fp += dxdy02 * (y_start - y0);
    if (y_start < y1)
    {
        x01_fp += dxdy01 * (y_start - y0);
    }
    else
    {
        x12_fp += dxdy12 * (y_start - y1);
    }

    for (int32_t y = y_start; y <= y_end; y++)
    {
        bool upper = (y < y1);

        int32_t xa_fp = x02_fp;
        int32_t xb_fp = upper ? x01_fp : x12_fp;

        int32_t xa = xa_fp >> 16;
        int32_t xb = xb_fp >> 16;
        if (xa > xb)
        {
            int32_t t = xa;
            xa = xb;
            xb = t;
        }

        if (xa < 0) xa = 0;
        if (xb >= (int32_t)w) xb = (int32_t)w - 1;
        if (xb >= xa)
        {
            uint16_t *row = &dst[(uint32_t)y * w];
            for (int32_t x = xa; x <= xb; x++) row[(uint32_t)x] = rgb565;
        }

        x02_fp += dxdy02;
        if (upper)
        {
            x01_fp += dxdy01;
            if (y + 1 == y1)
            {
                /* Switch to lower edge at p1. */
                x12_fp = p1.x << 16;
            }
        }
        else
        {
            x12_fp += dxdy12;
        }
    }
}
