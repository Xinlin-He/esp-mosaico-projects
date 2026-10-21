// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "bball_game.h"

// Load rendering assets (the basketball texture atlas) from the given asset
// root. Safe to call once at start-up; falls back to vector drawing if the
// atlas is unavailable.
void bball_view_init_assets(const char *asset_root);

// Render the current game state into the active Raylib-compatible surface.
// Shared by the Host simulator and the on-device application.
void bball_view_render(const bball_game_t *game);
