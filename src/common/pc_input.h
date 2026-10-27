#pragma once
// Keyboard and mouse as the controller on port 0: keys and mouse buttons bound to its buttons and sticks, the mouse's
// motion as the right stick. Bindings are the settings file's [keys] table, by controller input.
#include <cstdint>

#include "core/input.h"

namespace pc_input {

extern uint32_t g_mouse_sensitivity;  // percent
extern bool g_invert_mouse_y;

// settings file: the [keys] table
void Load(const void* table);
void Save(void* table);

// window thread
void Window(void* hwnd);
bool Message(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);

// a polled controller: the keys and the mouse merged into port 0, which is connected without a controller too
void Pad(int port, input::Pad& pad);

}  // namespace pc_input
