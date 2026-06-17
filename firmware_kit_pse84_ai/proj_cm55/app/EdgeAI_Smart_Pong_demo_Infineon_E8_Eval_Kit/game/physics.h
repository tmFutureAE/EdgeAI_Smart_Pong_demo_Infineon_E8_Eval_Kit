#pragma once

#include "game/game.h"

void physics_reset_ball(pong_game_t *g, int serve_dir);
void physics_step(pong_game_t *g, float dt);

