#pragma once
// Sharpening of the game's 3D picture, not of its HUD or menus: contrast adaptive sharpening (AMD FidelityFX CAS) run
// once a frame on the back buffer, at the render scale, when the HUD's first draw follows the scene's 3D draws, so the
// HUD drawn after it stays as it is; with the HUD hidden (game/hud.cpp), as the frame is presented, in play only.
// Frames without 3D (menus, loading) are left alone, and so is the pause menu over the game.
// Settings file: sharpening (on by default) and sharpness, 0 to 1 (0.3). F8 switches it, shown in the corner.

namespace sharpen {

extern bool g_on;

// before the backend is made (GameInfo::init): the renderer extension, with the ambient occlusion (common/ssao.h)
void Install();

// the sharpness, 0 to 1, any thread
float Strength();
void SetStrength(float v);

// settings file: sharpness (sharpening is a GameInfo option)
void Load(const void* table);
void Save(void* table);

}  // namespace sharpen
