// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "snake_game.h"

// Render the current game state into the active Raylib-compatible surface.
// Shared by the Host simulator and the on-device application.
void snake_view_render(const snake_game_t *game);
