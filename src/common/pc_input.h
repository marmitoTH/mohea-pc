#pragma once
// Keyboard and mouse as the controller on port 0: keys and mouse buttons bound to its buttons and sticks, the mouse's
// motion as the right stick. Bindings are the settings file's [keys] table, by controller input, and a controller's
// buttons remapped are its [pad] table; both are set in the overlay.
#include <cstdint>
#include <string>

#include "core/input.h"

namespace pc_input {

extern uint32_t g_mouse_sensitivity;  // percent
extern bool g_invert_mouse_y;

// settings file: the [keys] and [pad] tables
void Load(const void* table);
void Save(void* table);

// The bindings, remapped from the overlay. The actions are the controller's buttons as the game's default layout has
// them (the profile's own layout is left out, game/layout.cpp), then each stick direction, and reload and use on their
// own (each presses A for that action alone). In play a controller's buttons are put on the actions they are mapped
// to (several may share one) and keys press the actions they are bound to; in the menus, the pause menu among them, a
// controller is as it is and the default keys work too, so the d-pad, A, B, the arrows and Enter always work them.
constexpr int kPadButtons = 16;  // a controller's buttons, the first actions
int Actions();
bool OnPad(int action);  // a controller's button can drive it: not the stick directions
const char* ActionName(int action);
// the order the overlay lists them in
int ListedAction(int i);
std::string KeysText(int action);  // the keys bound, as the overlay shows them; "-" for none
void SetKeys(int action, int key, bool add);  // add the key, or have it alone; 0 clears
// a controller's buttons, kPadButtons of them; -1 none
const char* PadButtonName(int button);
int PadButton(int action);
void SetPadButton(int action, int button);  // the mapping custom from then on, from the preset's
// the controller's presets: the game's layouts Sharp Shooter (the default), Commander, Recon and Reloader, Modern,
// then Custom, the player's own mapping
constexpr int kPadPresets = 6;
const char* PadPresetName(int preset);
int PadPreset();
void SetPadPreset(int preset);
extern bool g_swap_sticks;  // the left stick looks and the right moves
void ResetBindings();

// a key for the overlay, UI thread: once every key is up, the next one or mouse button or wheel notch; Esc or a
// controller's B cancels. CapturedKey: 0 while waiting, -1 cancelled, else the key, which ends it
void StartCapture();
bool Capturing();
int CapturedKey();
void CancelCapture();

// window thread
void Window(void* hwnd);
bool Message(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);

// the game's thread, from the player control each frame it runs (game/actions.cpp): the player is in play, and looks
// through a scope or not. The mouse wheel works only in play: zooming in a scope (up in, down out), else as bound
void SetPlay(bool scoped);

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
