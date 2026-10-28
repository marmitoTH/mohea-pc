#pragma once
// F11 and Alt+Enter switch between a window and the full screen: the full screen mode last used (borderless, or
// fullscreen when the settings file asks for it), kept in the settings file. F9 switches the game between 30 and 60
// frames a second (sixty_fps), F8 the sharpening (common/sharpen.h), F7 the ambient occlusion (common/ssao.h), F6
// the supersampling's samples a pixel (antialiasing), each shown in the corner for a moment.
#include <cstdint>

namespace display {

// window thread, before the input's: true takes the message
bool Message(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);

// each frame, inside ImGui's: what F9, F8, F7 or F6 switched to, for two seconds
void Ui();

}  // namespace display
