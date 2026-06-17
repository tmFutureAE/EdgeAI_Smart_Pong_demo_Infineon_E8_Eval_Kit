#include "game/modes.h"

game_mode_t modes_next(game_mode_t m)
{
    if (m == kGameModeZeroPlayer) return kGameModeSinglePlayer;
    if (m == kGameModeSinglePlayer) return kGameModeTwoPlayer;
    return kGameModeZeroPlayer;
}
