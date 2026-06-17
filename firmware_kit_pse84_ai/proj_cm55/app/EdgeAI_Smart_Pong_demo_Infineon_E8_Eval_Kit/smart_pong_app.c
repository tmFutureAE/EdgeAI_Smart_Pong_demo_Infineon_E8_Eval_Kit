#include "smart_pong_app.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "edgeai_config.h"

#include "game/game.h"
#include "game/render.h"
#include "platform/audio_hal.h"
#include "platform/display_hal.h"
#include "platform/input_hal.h"
#include "platform/time_hal.h"
#include "sw_render.h"
#include "text5x7.h"

static void smart_pong_draw_boot_banner(void)
{
    const char *title = "SMART";
    const char *subtitle = "PONG";
    int32_t title_scale = 5;
    int32_t subtitle_scale = 5;
    while ((edgeai_text5x7_width(title_scale, title) > (EDGEAI_LCD_W - 20)) && (title_scale > 1))
    {
        title_scale--;
        subtitle_scale = title_scale;
    }
    const int32_t title_w = edgeai_text5x7_width(title_scale, title);
    const int32_t subtitle_w = edgeai_text5x7_width(subtitle_scale, subtitle);
    const int32_t title_x = (EDGEAI_LCD_W - title_w) / 2;
    const int32_t subtitle_x = (EDGEAI_LCD_W - subtitle_w) / 2;
    const int32_t title_y = (EDGEAI_LCD_H / 2) - (7 * title_scale) - 6;
    const int32_t subtitle_y = title_y + (7 * title_scale) + 12;

    display_hal_fill(0x0000u);
    edgeai_text5x7_draw_scaled_no_present(title_x, title_y, title_scale, title, 0xFFFFu);
    edgeai_text5x7_draw_scaled_no_present(subtitle_x, subtitle_y, subtitle_scale, subtitle, 0xBDF7u);
    display_hal_present_frame();
    time_hal_delay_us(700000u);
}

void smart_pong_app_start(void)
{
    time_hal_init();

    if (!display_hal_init())
    {
        printf("Display HAL initialization failed\r\n");
        for (;;) {}
    }

    smart_pong_draw_boot_banner();

    input_hal_t input;
    (void)input_hal_init(&input);
    (void)audio_hal_init();

    pong_game_t game;
    game_init(&game);

    render_state_t render;
    render_init(&render);

    const uint32_t frame_us = 1000000u / (uint32_t)EDGEAI_FIXED_FPS;
    const float dt = 1.0f / (float)EDGEAI_FIXED_FPS;
    uint16_t prev_score_left = game.score.left;
    uint16_t prev_score_right = game.score.right;
    bool prev_match_over = game.match_over;
    uint8_t prev_audio_volume = game.audio_volume;
    audio_hal_set_volume(prev_audio_volume);
    uint32_t last_wall_sfx_cycles = 0u;
    uint32_t last_paddle_sfx_cycles = 0u;
    uint32_t last_point_sfx_cycles = 0u;
    const uint32_t wall_sfx_min_gap_us = 50000u;
    const uint32_t paddle_sfx_min_gap_us = 75000u;
    const uint32_t point_sfx_min_gap_us = 150000u;

    for (;;)
    {
        uint32_t start = time_hal_cycles();

        platform_input_t in;
        input_hal_poll(&input, &in);

        game_step(&game, &in, dt);
        if (game.audio_volume != prev_audio_volume)
        {
            audio_hal_set_volume(game.audio_volume);
            prev_audio_volume = game.audio_volume;
        }
        if (game.sfx_wall_bounce_count > 0u)
        {
            uint32_t now = time_hal_cycles();
            if ((last_wall_sfx_cycles == 0u) || (time_hal_elapsed_us(last_wall_sfx_cycles) >= wall_sfx_min_gap_us))
            {
                audio_hal_queue_wall_bounce(1u);
                last_wall_sfx_cycles = now;
            }
            game.sfx_wall_bounce_count = 0u;
        }
        if (game.sfx_paddle_hit_count > 0u)
        {
            uint32_t now = time_hal_cycles();
            if ((last_paddle_sfx_cycles == 0u) || (time_hal_elapsed_us(last_paddle_sfx_cycles) >= paddle_sfx_min_gap_us))
            {
                audio_hal_queue_paddle_hit(1u);
                last_paddle_sfx_cycles = now;
            }
            game.sfx_paddle_hit_count = 0u;
        }
        if ((game.score.left > prev_score_left) || (game.score.right > prev_score_right))
        {
            uint32_t now = time_hal_cycles();
            if ((last_point_sfx_cycles == 0u) || (time_hal_elapsed_us(last_point_sfx_cycles) >= point_sfx_min_gap_us))
            {
                audio_hal_queue_point_scored(1u);
                last_point_sfx_cycles = now;
            }
        }
        if (!prev_match_over && game.match_over)
        {
            audio_hal_queue_win_tune();
        }
        prev_match_over = game.match_over;
        prev_score_left = game.score.left;
        prev_score_right = game.score.right;
        audio_hal_update();
        render_draw_frame(&render, &game);

        uint32_t elapsed_us = time_hal_elapsed_us(start);
        while (elapsed_us < frame_us)
        {
            /* Keep audio FIFO serviced between frames to avoid underrun chop. */
            audio_hal_update();
            uint32_t remaining = frame_us - elapsed_us;
            if (remaining > 300u)
            {
                time_hal_delay_us(200u);
            }
            elapsed_us = time_hal_elapsed_us(start);
        }
    }
}
