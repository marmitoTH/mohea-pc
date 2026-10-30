#pragma once
// Game options kept in the settings file (game_info.cpp's options), defined by the code that uses them.

#include <cstdint>

// game/frame_rate.cpp: 60 frames a second in game instead of 30
extern bool g_sixty_fps;

// game/fov.cpp: the field of view at rest, degrees across a 4:3 view (60 to 110, 70 the game's)
extern uint32_t g_fov;
float RestFov();

// game/drops.cpp: how long a fallen enemy's drops stay, seconds (23 to 600, 23 the game's, 60 by default), or for good
extern uint32_t g_drop_seconds;
extern bool g_drops_forever;
uint32_t DropSeconds();

// game/squad.cpp: the squad the player orders takes no hits
extern bool g_squad_immortal;

// game/invincible.cpp: the player takes no damage
extern bool g_player_invincible;

// game/ammo.cpp: the player never runs out of ammo nor reloads, as the game's cheat
extern bool g_unlimited_ammo;

// game/hud.cpp: the HUD not drawn
extern bool g_hide_hud;
// the game would draw its HUD now: in play, not in the pause menu or a cutscene (any thread)
bool HudWanted();

// game/shell_shock.cpp: no shell shock from blasts and scripts
extern bool g_no_shell_shock;

// game/pool_guard.cpp: the game's pools checked as they are used, for tracing crashes (on in dev and debug builds)
extern bool g_pool_guard;
