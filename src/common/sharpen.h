#pragma once
// Sharpening of the game's 3D picture, not of its HUD or menus: contrast adaptive sharpening (AMD FidelityFX CAS) run
// once a frame on the back buffer, at the render scale, when the HUD's first draw follows the scene's 3D draws, so the
// HUD drawn after it stays as it is. Frames without 3D (menus, loading) are left alone.
// Settings file: sharpening (on by default) and sharpness, 0 to 1 (0.5). F8 switches it, shown in the corner.

namespace sharpen {

extern bool g_on;

// before the backend is made (GameInfo::init): the renderer extension, with the ambient occlusion (common/ssao.h)
void Install();

// settings file: sharpness (sharpening is a GameInfo option)
void Load(const void* table);
void Save(void* table);

}  // namespace sharpen
