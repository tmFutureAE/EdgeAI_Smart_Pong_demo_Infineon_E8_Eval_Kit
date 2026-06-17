#pragma once

#include <stdint.h>

#include "game/game.h"
#include "sw_render.h"

typedef struct
{
    int32_t cx;
    int32_t cy;
    int32_t world_scale_x;
    int32_t world_scale_y;
    float persp;

    sw_point_t near_corners[4];
    sw_point_t far_corners[4];
} render_state_t;

void render_init(render_state_t *rs);
void render_draw_frame(render_state_t *rs, const pong_game_t *g);

