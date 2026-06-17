#pragma once

#include <stdint.h>

typedef struct
{
    int32_t x;
    int32_t y;
} sw_point_t;

static inline uint16_t sw_pack_rgb565_u8(uint32_t r8, uint32_t g8, uint32_t b8)
{
    if (r8 > 255u) r8 = 255u;
    if (g8 > 255u) g8 = 255u;
    if (b8 > 255u) b8 = 255u;
    uint16_t r = (uint16_t)(r8 >> 3);
    uint16_t g = (uint16_t)(g8 >> 2);
    uint16_t b = (uint16_t)(b8 >> 3);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void sw_render_clear(uint16_t *dst, uint32_t w, uint32_t h, uint16_t rgb565);

void sw_render_fill_rect(uint16_t *dst, uint32_t w, uint32_t h,
                         int32_t tile_x0, int32_t tile_y0,
                         int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                         uint16_t rgb565);

void sw_render_line(uint16_t *dst, uint32_t w, uint32_t h,
                    int32_t tile_x0, int32_t tile_y0,
                    int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                    uint16_t rgb565);

void sw_render_filled_circle(uint16_t *dst, uint32_t w, uint32_t h,
                             int32_t tile_x0, int32_t tile_y0,
                             int32_t cx, int32_t cy, int32_t r, uint16_t rgb565);

void sw_render_fill_triangle(uint16_t *dst, uint32_t w, uint32_t h,
                             int32_t tile_x0, int32_t tile_y0,
                             sw_point_t p0, sw_point_t p1, sw_point_t p2,
                             uint16_t rgb565);

static inline void sw_render_fill_quad(uint16_t *dst, uint32_t w, uint32_t h,
                                       int32_t tile_x0, int32_t tile_y0,
                                       sw_point_t p0, sw_point_t p1, sw_point_t p2, sw_point_t p3,
                                       uint16_t rgb565)
{
    sw_render_fill_triangle(dst, w, h, tile_x0, tile_y0, p0, p1, p2, rgb565);
    sw_render_fill_triangle(dst, w, h, tile_x0, tile_y0, p0, p2, p3, rgb565);
}

