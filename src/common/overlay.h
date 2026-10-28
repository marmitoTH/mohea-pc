#pragma once
// The options overlay: F1 (or both sticks clicked on a controller) opens it over the game, F1, Esc or its Close
// button closes it. While it is open the game gets no input (common/pc_input.cpp clears the pad and lets the mouse
// go) and the overlay is worked with the mouse, the keyboard or a controller (ImGui's own XInput navigation: d-pad,
// A, B). Opened from a controller the mouse stays hidden and unused until it moves. Each change applies at once and
// is saved in the settings file.
#include <atomic>

namespace overlay {

extern std::atomic<bool> g_open;
extern std::atomic<bool> g_by_pad;  // opened from a controller and the mouse not moved since

// any thread: open or close it, from a controller or not
void Toggle(bool by_pad = false);

// each frame, inside ImGui's
void Draw();

}  // namespace overlay
