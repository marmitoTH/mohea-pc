#pragma once
// F11 and Alt+Enter switch between a window and the full screen: the full screen mode last used (borderless, or
// fullscreen when the settings file asks for it), kept in the settings file. F9 switches the game between 30 and 60
// frames a second (sixty_fps).
#include <cstdint>

namespace display {

// window thread, before the input's: true takes the message
bool Message(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);

}  // namespace display
