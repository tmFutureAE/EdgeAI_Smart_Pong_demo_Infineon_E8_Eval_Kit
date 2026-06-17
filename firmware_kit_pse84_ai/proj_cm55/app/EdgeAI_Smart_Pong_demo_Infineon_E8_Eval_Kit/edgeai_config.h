#pragma once

#include <stdint.h>

/* Project is locked to 4.3-inch display profile (800x480). */
#define EDGEAI_LCD_W 800
#define EDGEAI_LCD_H 480

#ifndef EDGEAI_APP_NAME
#define EDGEAI_APP_NAME "EdgeAI Smart Pong"
#endif

#ifndef EDGEAI_APP_VERSION
#define EDGEAI_APP_VERSION "1.0.0"
#endif

/* Accelerometer normalization.
 * FXLS8974 configuration yields roughly ~512 counts per 1g in the current mode.
 */
#define EDGEAI_ACCEL_MAP_DENOM 512

/* Impact ("bang") detection tuning.
 * Uses a high-pass term: hp = raw - low-pass(raw), in raw sensor counts.
 */
#ifndef EDGEAI_BANG_THRESHOLD
#define EDGEAI_BANG_THRESHOLD 220
#endif

/* Render tile limits (single-blit path). */
#define EDGEAI_TILE_MAX_W 200
#define EDGEAI_TILE_MAX_H EDGEAI_LCD_H

/* Fixed-timestep target. */
#ifndef EDGEAI_FIXED_FPS
#define EDGEAI_FIXED_FPS 60
#endif

/* UI: reserve a top bar region for the settings pill. */
#ifndef EDGEAI_UI_BAR_H
#define EDGEAI_UI_BAR_H 64
#endif

/* Touch control strips at the left/right screen edges (normalized width). */
#ifndef EDGEAI_TOUCH_STRIP_W_NORM
#define EDGEAI_TOUCH_STRIP_W_NORM 0.18f
#endif
