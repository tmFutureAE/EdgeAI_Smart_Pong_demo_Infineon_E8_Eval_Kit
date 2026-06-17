#pragma once

#include <stdint.h>

/* Minimal 5x7 text renderer for direct-to-LCD drawing (via fill-rect).
 * This is used for boot titles and small status text in raster mode.
 */

int32_t edgeai_text5x7_width(int32_t scale, const char *s);
void edgeai_text5x7_draw_scaled(int32_t x, int32_t y, int32_t scale, const char *s, uint16_t rgb565);
void edgeai_text5x7_draw_scaled_no_present(int32_t x, int32_t y, int32_t scale, const char *s, uint16_t rgb565);

/* Tile-buffer variant (for software rendered frames). */
void edgeai_text5x7_draw_scaled_sw(uint16_t *dst, uint32_t w, uint32_t h,
                                   int32_t tile_x0, int32_t tile_y0,
                                   int32_t x, int32_t y, int32_t scale,
                                   const char *s, uint16_t rgb565);
