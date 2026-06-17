#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "game/modes.h"
#include "platform/input_hal.h"
#include "platform/npu_hal.h"

typedef struct
{
    float x;
    float y;
    float z;
    float vx;
    float vy;
    float vz;
    float r;
} pong_ball_t;

typedef struct
{
    float x_plane;
    float y;
    float z;
    float vy;
    float vz;
    float size_y;
    float size_z;
    float target_y;
    float target_z;
} pong_paddle_t;

typedef struct
{
    uint16_t left;
    uint16_t right;
} pong_score_t;

typedef enum
{
    kAiLearnModeBoth = 0,
    kAiLearnModeAiAlgo = 1,
    kAiLearnModeAlgoAi = 2,
} ai_learn_mode_t;

typedef struct
{
    float speed_scale;
    float noise_scale;
    float lead_scale;
    float center_bias;
    float corner_bias;
    uint16_t hits;
    uint16_t misses;
    uint16_t style_trials[4];
    int16_t style_value_q8[4];
    uint8_t last_style;
} ai_learn_profile_t;

typedef struct
{
    game_mode_t mode;
    uint8_t difficulty; /* 1..3 */
    bool ai_enabled;
    uint16_t match_target;
    bool persistent_learning;
    bool speedpp_enabled;
    bool target_overlay_enabled;
    uint8_t audio_volume; /* 0..100 (%), 0 = mute */
    bool menu_open;
    bool help_open;
    bool ui_block_touch;
    bool match_over;
    bool winner_left;
    bool end_prompt_dismissed;
    bool countdown_active;

    pong_ball_t ball;
    pong_paddle_t paddle_l;
    pong_paddle_t paddle_r;
    pong_score_t score;
    uint32_t score_total_left;
    uint32_t score_total_right;
    bool score_totals_pending;

    uint32_t rng;
    uint32_t frame;
    uint32_t match_over_frame;
    uint32_t match_over_start_cycles;
    uint32_t countdown_us_left;
    uint32_t countdown_start_cycles;

    float last_hit_dy;
    float last_hit_dz;

    float serve_vx;
    float serve_vy;
    float serve_vz;
    float speedpp_peak_speed;
    float speedpp_serve_speed_target;
    uint8_t speedpp_stage;
    uint16_t speedpp_next_threshold;

    /* Latest accel sample (for UI/debug + optional game effects). */
    bool accel_active;
    float accel_ax;
    float accel_ay;
    uint8_t sfx_wall_bounce_count;
    uint8_t sfx_paddle_hit_count;

    /* Runtime AI/NPU telemetry. */
    uint32_t ai_telemetry_start_cycles;
    uint32_t ai_npu_attempts_window;
    uint32_t ai_fallback_window;
    uint16_t ai_npu_rate_hz;
    uint16_t ai_fallback_rate_hz;

    ai_learn_mode_t ai_learn_mode;
    bool ai_left_active;
    bool ai_right_active;
    ai_learn_profile_t ai_profile_left;
    ai_learn_profile_t ai_profile_right;

    npu_hal_t npu;
} pong_game_t;

void game_init(pong_game_t *g);
void game_reset(pong_game_t *g);
void game_step(pong_game_t *g, const platform_input_t *in, float dt);
