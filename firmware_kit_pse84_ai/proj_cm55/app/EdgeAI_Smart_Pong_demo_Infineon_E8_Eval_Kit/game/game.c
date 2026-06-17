#include "game/game.h"

#include <string.h>

#include "edgeai_config.h"

#include "game/ai.h"
#include "game/input.h"
#include "game/modes.h"
#include "game/physics.h"
#include "game/ui_layout.h"
#include "platform/time_hal.h"

#define EDGEAI_MATCH_TARGET_11 11u
#define EDGEAI_MATCH_TARGET_100 100u
#define EDGEAI_MATCH_TARGET_1K 999u
#define EDGEAI_MAX_SCORE 999u
#define EDGEAI_END_PROMPT_DELAY_FRAMES 120u
#define EDGEAI_P0_DEMO_RESET_US 1300000u
#define EDGEAI_MAX_SCORE_RESET_US 30000000u
#define EDGEAI_COUNTDOWN_STEP_US 1000000u
#define EDGEAI_COUNTDOWN_TOTAL_US (3u * EDGEAI_COUNTDOWN_STEP_US)

static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline float absf(float v)
{
    return (v < 0.0f) ? -v : v;
}

static inline bool hit_rect(int32_t x, int32_t y, int32_t rx, int32_t ry, int32_t rw, int32_t rh)
{
    return (x >= rx) && (x < (rx + rw)) && (y >= ry) && (y < (ry + rh));
}

static float game_deadzone(float v, float dz)
{
    if (dz <= 0.0f) return v;
    if (dz >= 1.0f) return 0.0f;
    return (absf(v) < dz) ? 0.0f : v;
}

static bool game_end_prompt_visible(const pong_game_t *g)
{
    if (!g) return false;
    if (!g->match_over) return false;
    if (g->end_prompt_dismissed) return false;

    uint32_t elapsed = g->frame - g->match_over_frame;
    return (elapsed >= EDGEAI_END_PROMPT_DELAY_FRAMES);
}

static uint16_t game_match_target_from_index(int32_t idx)
{
    if (idx <= 0) return EDGEAI_MATCH_TARGET_11;
    if (idx == 1) return EDGEAI_MATCH_TARGET_100;
    return EDGEAI_MATCH_TARGET_1K;
}

static void game_start_countdown(pong_game_t *g)
{
    if (!g) return;
    g->serve_vx = g->ball.vx;
    g->serve_vy = g->ball.vy;
    g->serve_vz = g->ball.vz;

    g->ball.vx = 0.0f;
    g->ball.vy = 0.0f;
    g->ball.vz = 0.0f;

    g->countdown_active = true;
    g->countdown_us_left = EDGEAI_COUNTDOWN_TOTAL_US;
    g->countdown_start_cycles = time_hal_cycles();
}

static void game_update_countdown(pong_game_t *g)
{
    if (!g || !g->countdown_active) return;

    uint32_t elapsed_us = time_hal_elapsed_us(g->countdown_start_cycles);
    if (elapsed_us >= EDGEAI_COUNTDOWN_TOTAL_US)
    {
        g->countdown_us_left = 0u;
        g->countdown_active = false;
        g->ball.vx = g->serve_vx;
        g->ball.vy = g->serve_vy;
        g->ball.vz = g->serve_vz;
        return;
    }

    g->countdown_us_left = EDGEAI_COUNTDOWN_TOTAL_US - elapsed_us;
}

static void game_apply_accel_ball_nudge(pong_game_t *g, const platform_input_t *in, float dt)
{
    if (!g || !in) return;
    if (dt <= 0.0f) return;
    if (g->mode != kGameModeZeroPlayer) return;
    if (!in->accel_active) return;

    /* Tilt affects only ball vertical (y) in P0 mode. Z is intentionally unused. */
    float ax = clampf(in->accel_ax, -1.0f, 1.0f);
    float ay = clampf(in->accel_ay, -1.0f, 1.0f);

    /* Input is already deadzoned/softened in accel_proc; avoid a second deadzone here. */
    const float dz = 0.0f;
    ax = game_deadzone(ax, dz);
    ay = game_deadzone(ay, dz);

    /* High gain on purpose: P0 is a "mess with the outcome" mode.
     * Use mapped Y tilt input for vertical ball nudge; this keeps left/right
     * board roll from unintentionally driving up/down motion.
     */
    const float k = 14.0f;
    (void)ax;
    g->ball.vy -= ay * k * dt;

    const float vlim = 6.0f;
    g->ball.vy = clampf(g->ball.vy, -vlim, vlim);
}

static void ui_handle_press(pong_game_t *g, float touch_x, float touch_y)
{
    if (!g) return;

    int32_t px = (int32_t)(touch_x * (float)(EDGEAI_LCD_W - 1) + 0.5f);
    int32_t py = (int32_t)(touch_y * (float)(EDGEAI_LCD_H - 1) + 0.5f);
    px = clampi(px, 0, EDGEAI_LCD_W - 1);
    py = clampi(py, 0, EDGEAI_LCD_H - 1);

    if (g->match_over)
    {
        if (g->mode == kGameModeZeroPlayer) return;

        if (g->end_prompt_dismissed)
        {
            g->end_prompt_dismissed = false;
            if (g->frame > EDGEAI_END_PROMPT_DELAY_FRAMES)
            {
                g->match_over_frame = g->frame - EDGEAI_END_PROMPT_DELAY_FRAMES;
            }
            else
            {
                g->match_over_frame = 0u;
            }
            return;
        }

        if (game_end_prompt_visible(g))
        {
            if (hit_rect(px, py, EDGEAI_END_BTN_YES_X, EDGEAI_END_BTN_Y, EDGEAI_END_BTN_W, EDGEAI_END_BTN_H))
            {
                game_reset(g);
                g->match_over = false;
                g->winner_left = false;
                g->end_prompt_dismissed = false;
                g->menu_open = false;
                g->help_open = false;
                return;
            }

            if (hit_rect(px, py, EDGEAI_END_BTN_NO_X, EDGEAI_END_BTN_Y, EDGEAI_END_BTN_W, EDGEAI_END_BTN_H))
            {
                g->end_prompt_dismissed = true;
                return;
            }
        }
        return;
    }

    const int32_t pill_x = EDGEAI_UI_PILL_X;
    const int32_t pill_y = EDGEAI_UI_PILL_Y;
    const int32_t pill_w = EDGEAI_UI_PILL_W;
    const int32_t pill_h = EDGEAI_UI_PILL_H;

    const int32_t help_x = EDGEAI_UI_HELP_BTN_X;
    const int32_t help_y = EDGEAI_UI_HELP_BTN_Y;
    const int32_t help_w = EDGEAI_UI_HELP_BTN_W;
    const int32_t help_h = EDGEAI_UI_HELP_BTN_H;

    if (hit_rect(px, py, help_x, help_y, help_w, help_h))
    {
        g->help_open = !g->help_open;
        if (g->help_open) g->menu_open = false;
        return;
    }

    if (hit_rect(px, py, pill_x, pill_y, pill_w, pill_h))
    {
        g->menu_open = !g->menu_open;
        if (g->menu_open) g->help_open = false;
        return;
    }

    if (g->help_open)
    {
        const int32_t panel_x = EDGEAI_UI_HELP_PANEL_X;
        const int32_t panel_y = EDGEAI_UI_HELP_PANEL_Y;
        const int32_t panel_w = EDGEAI_UI_HELP_PANEL_W;
        const int32_t panel_h = EDGEAI_UI_HELP_PANEL_H;

        if (!hit_rect(px, py, panel_x, panel_y, panel_w, panel_h))
        {
            g->help_open = false;
            g->ui_block_touch = true;
        }
        return;
    }

    if (!g->menu_open) return;

    const int32_t panel_x = EDGEAI_UI_PANEL_X;
    const int32_t panel_y = EDGEAI_UI_PANEL_Y;
    const int32_t panel_w = EDGEAI_UI_PANEL_W;
    const int32_t panel_h = EDGEAI_UI_PANEL_H;

    if (!hit_rect(px, py, panel_x, panel_y, panel_w, panel_h))
    {
        g->menu_open = false;
        g->ui_block_touch = true;
        return;
    }

    const int32_t opt_y0 = (EDGEAI_UI_ROW_H - EDGEAI_UI_OPT_H) / 2;
    const int32_t new_y0 = (EDGEAI_UI_ROW_H - EDGEAI_UI_NEW_H) / 2;

    /* Players: 0, 1, 2 */
    for (int32_t i = 0; i < 3; i++)
    {
        int32_t bx = EDGEAI_UI_OPT_BLOCK_X + i * (EDGEAI_UI_OPT_W + EDGEAI_UI_OPT_GAP);
        int32_t by = EDGEAI_UI_ROW0_Y + opt_y0;
        if (hit_rect(px, py, bx, by, EDGEAI_UI_OPT_W, EDGEAI_UI_OPT_H))
        {
            game_mode_t new_mode = (i == 0) ? kGameModeZeroPlayer : (i == 1) ? kGameModeSinglePlayer : kGameModeTwoPlayer;
            if (g->mode != new_mode)
            {
                g->mode = new_mode;
                game_reset(g);
            }
            return;
        }
    }

    /* Difficulty: 1, 2, 3 */
    for (int32_t i = 0; i < 3; i++)
    {
        int32_t bx = EDGEAI_UI_OPT_BLOCK_X + i * (EDGEAI_UI_OPT_W + EDGEAI_UI_OPT_GAP);
        int32_t by = EDGEAI_UI_ROW1_Y + opt_y0;
        if (hit_rect(px, py, bx, by, EDGEAI_UI_OPT_W, EDGEAI_UI_OPT_H))
        {
            uint8_t new_diff = (uint8_t)(i + 1);
            if (new_diff < 1) new_diff = 1;
            if (new_diff > 3) new_diff = 3;
            if (g->difficulty != new_diff)
            {
                g->difficulty = new_diff;
                game_reset(g);
            }
            return;
        }
    }

    /* AI: ON, OFF */
    for (int32_t i = 0; i < 2; i++)
    {
        int32_t bx = EDGEAI_UI_OPT2_BLOCK_X + i * (EDGEAI_UI_OPT_W + EDGEAI_UI_OPT_GAP);
        int32_t by = EDGEAI_UI_ROW2_Y + opt_y0;
        if (hit_rect(px, py, bx, by, EDGEAI_UI_OPT_W, EDGEAI_UI_OPT_H))
        {
            bool new_ai_enabled = (i == 0);
            if (g->ai_enabled != new_ai_enabled)
            {
                g->ai_enabled = new_ai_enabled;
            }
            return;
        }
    }

    /* Skill mode: 2AI, AI/ALGO, ALGO/AI */
    for (int32_t i = 0; i < 3; i++)
    {
        int32_t bx = EDGEAI_UI_OPT_BLOCK_X + i * (EDGEAI_UI_OPT_W + EDGEAI_UI_OPT_GAP);
        int32_t by = EDGEAI_UI_ROW3_Y + opt_y0;
        if (hit_rect(px, py, bx, by, EDGEAI_UI_OPT_W, EDGEAI_UI_OPT_H))
        {
            ai_learn_mode_t mode =
                (i == 0) ? kAiLearnModeBoth : ((i == 1) ? kAiLearnModeAiAlgo : kAiLearnModeAlgoAi);
            if (g->ai_learn_mode != mode)
            {
                ai_learning_set_mode(g, mode);
            }
            return;
        }
    }

    /* Persistent learning: ON, OFF */
    for (int32_t i = 0; i < 2; i++)
    {
        int32_t bx = EDGEAI_UI_OPT2_BLOCK_X + i * (EDGEAI_UI_OPT_W + EDGEAI_UI_OPT_GAP);
        int32_t by = EDGEAI_UI_ROW4_Y + opt_y0;
        if (hit_rect(px, py, bx, by, EDGEAI_UI_OPT_W, EDGEAI_UI_OPT_H))
        {
            bool en = (i == 0);
            if (g->persistent_learning != en)
            {
                ai_learning_set_persistent(g, en);
            }
            return;
        }
    }

    /* Match target: 11, 100, 1K(999). */
    for (int32_t i = 0; i < 3; i++)
    {
        int32_t bx = EDGEAI_UI_OPT_BLOCK_X + i * (EDGEAI_UI_OPT_W + EDGEAI_UI_OPT_GAP);
        int32_t by = EDGEAI_UI_ROW5_Y + opt_y0;
        if (hit_rect(px, py, bx, by, EDGEAI_UI_OPT_W, EDGEAI_UI_OPT_H))
        {
            uint16_t tgt = game_match_target_from_index(i);
            if (g->match_target != tgt)
            {
                g->match_target = tgt;
            }
            return;
        }
    }

    /* Target guide: ON, OFF */
    for (int32_t i = 0; i < 2; i++)
    {
        int32_t bx = EDGEAI_UI_OPT2_BLOCK_X + i * (EDGEAI_UI_OPT_W + EDGEAI_UI_OPT_GAP);
        int32_t by = EDGEAI_UI_ROW6_Y + opt_y0;
        if (hit_rect(px, py, bx, by, EDGEAI_UI_OPT_W, EDGEAI_UI_OPT_H))
        {
            bool en = (i == 0);
            if (g->target_overlay_enabled != en)
            {
                g->target_overlay_enabled = en;
            }
            return;
        }
    }

    /* SPEED++: ON, OFF */
    for (int32_t i = 0; i < 2; i++)
    {
        int32_t bx = EDGEAI_UI_OPT2_BLOCK_X + i * (EDGEAI_UI_OPT_W + EDGEAI_UI_OPT_GAP);
        int32_t by = EDGEAI_UI_ROW7_Y + opt_y0;
        if (hit_rect(px, py, bx, by, EDGEAI_UI_OPT_W, EDGEAI_UI_OPT_H))
        {
            bool en = (i == 0);
            if (g->speedpp_enabled != en)
            {
                g->speedpp_enabled = en;
                g->speedpp_stage = 0u;
                g->speedpp_next_threshold = 11u;
                g->speedpp_serve_speed_target = 0.0f;
                g->speedpp_peak_speed = 0.0f;
            }
            return;
        }
    }

    /* Volume: step down/up buttons. */
    {
        const int32_t vol_x = EDGEAI_UI_PANEL_X + 12;
        const int32_t vol_left_w = 88;
        const int32_t vol_center_w = 60;
        const int32_t vol_right_w = 88;
        int32_t left_bx = vol_x;
        int32_t right_bx = vol_x + vol_left_w + vol_center_w;
        int32_t by = EDGEAI_UI_ROW8_Y + opt_y0;
        if (hit_rect(px, py, left_bx, by, vol_left_w, EDGEAI_UI_OPT_H))
        {
            uint8_t v = g->audio_volume;
            g->audio_volume = (v < 5u) ? 0u : (uint8_t)(v - 5u);
            return;
        }
        if (hit_rect(px, py, right_bx, by, vol_right_w, EDGEAI_UI_OPT_H))
        {
            uint8_t v = g->audio_volume;
            g->audio_volume = (v > 95u) ? 100u : (uint8_t)(v + 5u);
            return;
        }
    }

    /* New game. */
    {
        int32_t bx = EDGEAI_UI_NEW_X;
        int32_t by = EDGEAI_UI_ROW9_Y + new_y0;
        if (hit_rect(px, py, bx, by, EDGEAI_UI_NEW_W, EDGEAI_UI_NEW_H))
        {
            g->score_total_left = 0u;
            g->score_total_right = 0u;
            game_reset(g);
            g->menu_open = false;
            return;
        }
    }
}

static void game_adjust_volume(pong_game_t *g, int delta)
{
    if (!g) return;
    int32_t v = (int32_t)g->audio_volume + delta;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    g->audio_volume = (uint8_t)v;
}

void game_init(pong_game_t *g)
{
    if (!g) return;
    memset(g, 0, sizeof(*g));

    g->mode = kGameModeZeroPlayer;
    g->difficulty = 2;
    g->ai_enabled = true;
    g->match_target = EDGEAI_MATCH_TARGET_100;
    g->persistent_learning = false;
    g->speedpp_enabled = true;
    g->target_overlay_enabled = false;
    g->audio_volume = 60u;
    g->ai_learn_mode = kAiLearnModeAlgoAi;
    g->menu_open = false;
    g->help_open = false;
    g->ui_block_touch = false;
    g->match_over = false;
    g->winner_left = false;
    g->end_prompt_dismissed = false;
    g->countdown_active = false;
    g->score_totals_pending = false;

    g->rng = 1u;
    g->frame = 0;
    g->match_over_frame = 0u;
    g->match_over_start_cycles = 0u;
    g->countdown_us_left = 0u;
    g->countdown_start_cycles = 0u;

    g->paddle_l.x_plane = 0.06f;
    g->paddle_r.x_plane = 0.94f;

    g->paddle_l.size_y = 0.26f;
    g->paddle_l.size_z = 0.28f;
    g->paddle_r.size_y = 0.26f;
    g->paddle_r.size_z = 0.28f;

    g->paddle_l.y = 0.5f;
    g->paddle_l.z = 0.5f;
    g->paddle_r.y = 0.5f;
    g->paddle_r.z = 0.5f;

    g->paddle_l.target_y = g->paddle_l.y;
    g->paddle_l.target_z = g->paddle_l.z;
    g->paddle_r.target_y = g->paddle_r.y;
    g->paddle_r.target_z = g->paddle_r.z;

    g->last_hit_dy = 0.0f;
    g->last_hit_dz = 0.0f;
    g->serve_vx = 0.0f;
    g->serve_vy = 0.0f;
    g->serve_vz = 0.0f;
    g->speedpp_peak_speed = 0.0f;
    g->speedpp_serve_speed_target = 0.0f;
    g->speedpp_stage = 0u;
    g->speedpp_next_threshold = 11u;

    g->accel_active = false;
    g->accel_ax = 0.0f;
    g->accel_ay = 0.0f;
    g->sfx_wall_bounce_count = 0u;
    g->sfx_paddle_hit_count = 0u;

    g->ai_telemetry_start_cycles = 0u;
    g->ai_npu_attempts_window = 0u;
    g->ai_fallback_window = 0u;
    g->ai_npu_rate_hz = 0u;
    g->ai_fallback_rate_hz = 0u;
    g->sfx_wall_bounce_count = 0u;
    g->sfx_paddle_hit_count = 0u;
    g->ai_left_active = false;
    g->ai_right_active = false;

    ai_init(g);
    game_reset(g);
}

void game_reset(pong_game_t *g)
{
    if (!g) return;

    if (g->score_totals_pending)
    {
        if (g->score.left > g->score.right)
        {
            if (g->score_total_left < 999u) g->score_total_left++;
        }
        else if (g->score.right > g->score.left)
        {
            if (g->score_total_right < 999u) g->score_total_right++;
        }
    }
    g->score_totals_pending = false;

    ai_learning_sync_store(g);

    g->score.left = 0;
    g->score.right = 0;

    g->paddle_l.y = 0.5f;
    g->paddle_l.z = 0.5f;
    g->paddle_r.y = 0.5f;
    g->paddle_r.z = 0.5f;

    g->paddle_l.vy = 0.0f;
    g->paddle_l.vz = 0.0f;
    g->paddle_r.vy = 0.0f;
    g->paddle_r.vz = 0.0f;

    g->paddle_l.target_y = g->paddle_l.y;
    g->paddle_l.target_z = g->paddle_l.z;
    g->paddle_r.target_y = g->paddle_r.y;
    g->paddle_r.target_z = g->paddle_r.z;

    g->last_hit_dy = 0.0f;
    g->last_hit_dz = 0.0f;
    g->match_over = false;
    g->winner_left = false;
    g->end_prompt_dismissed = false;
    g->match_over_frame = g->frame;
    g->match_over_start_cycles = 0u;
    g->ai_telemetry_start_cycles = 0u;
    g->ai_npu_attempts_window = 0u;
    g->ai_fallback_window = 0u;
    g->ai_npu_rate_hz = 0u;
    g->ai_fallback_rate_hz = 0u;

    if (g->persistent_learning)
    {
        ai_learning_set_persistent(g, true);
    }

    g->rng = g->rng * 1664525u + 1013904223u;
    int serve_dir = (g->rng & 1u) ? +1 : -1;
    physics_reset_ball(g, serve_dir);
    if (g->mode == kGameModeZeroPlayer)
    {
        g->countdown_active = false;
        g->countdown_us_left = 0u;
    }
    else
    {
        game_start_countdown(g);
    }
}

void game_step(pong_game_t *g, const platform_input_t *in, float dt)
{
    if (!g) return;

    /* Keep latest accel values in game state for UI/debug even when not used for control. */
    g->accel_active = (in && in->accel_active);
    g->accel_ax = in ? in->accel_ax : 0.0f;
    g->accel_ay = in ? in->accel_ay : 0.0f;

    if (in && in->touch_pressed)
    {
        ui_handle_press(g, in->touch_x, in->touch_y);
    }

    if (in && in->vol_dn_pressed)
    {
        game_adjust_volume(g, -5);
    }
    if (in && in->vol_up_pressed)
    {
        game_adjust_volume(g, +5);
    }

    if (g->ui_block_touch)
    {
        if (in && in->touch_active)
        {
            return;
        }
        g->ui_block_touch = false;
    }

    if (g->match_over)
    {
        uint32_t elapsed_us = time_hal_elapsed_us(g->match_over_start_cycles);
        bool reached_max_score = (g->score.left >= EDGEAI_MAX_SCORE || g->score.right >= EDGEAI_MAX_SCORE);

        if (reached_max_score)
        {
            if (elapsed_us >= EDGEAI_MAX_SCORE_RESET_US)
            {
                game_reset(g);
            }
        }
        else if (g->mode == kGameModeZeroPlayer && elapsed_us >= EDGEAI_P0_DEMO_RESET_US)
        {
            game_reset(g);
        }
        g->frame++;
        return;
    }

    if (g->menu_open || g->help_open)
    {
        /* Pause simulation while an overlay UI panel is open. */
        return;
    }

    if (in && in->mode_toggle)
    {
        g->mode = modes_next(g->mode);
        game_reset(g);
    }

    input_apply(g, in, dt);

    bool manual_r = (g->mode == kGameModeTwoPlayer) && in && in->p2_active;
    bool ai_left = (g->mode == kGameModeZeroPlayer);
    bool ai_right = (g->mode == kGameModeSinglePlayer) || (g->mode == kGameModeZeroPlayer);
    if (g->mode == kGameModeTwoPlayer || manual_r) ai_right = false;
    g->ai_left_active = ai_left;
    g->ai_right_active = ai_right;
    ai_step(g, dt, ai_left, ai_right);

    if (!g->countdown_active)
    {
        physics_step(g, dt);
    }
    else
    {
        game_update_countdown(g);
    }

    bool reached_match_score = (g->score.left >= g->match_target || g->score.right >= g->match_target);
    bool reached_max_score = (g->score.left >= EDGEAI_MAX_SCORE || g->score.right >= EDGEAI_MAX_SCORE);
    if (reached_match_score || reached_max_score)
    {
        g->match_over = true;
        g->score_totals_pending = true;
        g->winner_left = (g->score.left >= g->score.right);
        g->end_prompt_dismissed = reached_max_score || (g->mode == kGameModeZeroPlayer);
        g->match_over_frame = g->frame;
        g->match_over_start_cycles = time_hal_cycles();

        g->ball.vx = 0.0f;
        g->ball.vy = 0.0f;
        g->ball.vz = 0.0f;
        g->countdown_active = false;
        g->countdown_us_left = 0u;
    }

    /* Apply after physics so paddle hits do not overwrite the external nudge. */
    if (!g->match_over && !g->countdown_active)
    {
        game_apply_accel_ball_nudge(g, in, dt);
    }
    g->frame++;
}
