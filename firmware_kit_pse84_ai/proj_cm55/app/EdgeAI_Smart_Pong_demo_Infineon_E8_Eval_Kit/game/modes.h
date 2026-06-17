#pragma once

typedef enum
{
    kGameModeZeroPlayer = 0,
    kGameModeSinglePlayer = 1,
    kGameModeTwoPlayer = 2,
} game_mode_t;

game_mode_t modes_next(game_mode_t m);
