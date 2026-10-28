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

// the overlay (common/overlay.h) opened or closed, any thread: the window takes the mouse or lets it go
void Refresh();

// a polled controller: the keys and the mouse merged into port 0, which is connected without a controller too
void Pad(int port, input::Pad& pad);

// the A button's two actions in game, reload and use (game/actions.cpp): which one alone the keys press it for, as of
// the last poll; Both when a controller's A or a key bound to A itself holds it, or nothing does
enum class AOnly { Both, Reload, Use };
AOnly AButton();

// direct look, once a frame from the game's look handler: the mouse's motion since the last call as the view's turn,
// radians, right and up positive; 0 when the game window is not active. While the game calls it the mouse no longer
// moves the right stick
void TakeLook(float& yaw, float& pitch);

}  // namespace pc_input
