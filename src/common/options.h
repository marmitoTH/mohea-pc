#pragma once
// Game options kept in the settings file (game_info.cpp's options), defined by the code that uses them.

#include <cstdint>

// game/frame_rate.cpp: 60 frames a second in game instead of 30
extern bool g_sixty_fps;

// game/fov.cpp: the field of view at rest, degrees across a 4:3 view (60 to 110, 70 the game's)
extern uint32_t g_fov;
float RestFov();

// game/pool_guard.cpp: the game's pools checked as they are used, for tracing crashes (on in dev and debug builds)
extern bool g_pool_guard;
