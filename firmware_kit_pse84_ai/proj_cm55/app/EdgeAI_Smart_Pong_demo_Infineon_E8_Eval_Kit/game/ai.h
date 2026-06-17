#pragma once

#include <stdbool.h>

#include "game/game.h"

void ai_init(pong_game_t *g);
void ai_step(pong_game_t *g, float dt, bool ai_left, bool ai_right);
void ai_learning_reset_session(pong_game_t *g);
void ai_learning_set_mode(pong_game_t *g, ai_learn_mode_t mode);
void ai_learning_set_persistent(pong_game_t *g, bool enabled);
void ai_learning_sync_store(pong_game_t *g);
void ai_learning_on_paddle_hit(pong_game_t *g, bool left_side);
void ai_learning_on_miss(pong_game_t *g, bool left_side);
