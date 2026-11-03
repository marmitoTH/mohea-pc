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
// them (the profile's own layout is left out, game/layout.cpp), then each stick direction, reload and use on their own
// (each presses A for that action alone), the scope's zoom in and out (the d-pad's up and down in a scope, which its
// buttons and keys then drive alone; the d-pad and the wheel by default), and the keys' crouch and prone (each a
// toggle with standing; the controller's X cycles crouched and prone as the game has it) and weapons (the primary,
// the secondary, the grenades; the controller's B cycles them), game/play_keys.cpp. In play a controller's buttons
// are put on the actions they are mapped to (several may share one) and keys press the actions they are bound to; in
// the menus, the pause menu among them, a controller is as it is and the default keys work too, so the d-pad, A, B,
// the arrows and Enter always work them.
enum Target : int {
    kA, kB, kX, kY, kBlack, kWhite, kLeftTrigger, kRightTrigger,
    kDpadUp, kDpadDown, kDpadLeft, kDpadRight, kStart, kBack, kLeftThumb, kRightThumb,
    kMoveForward, kMoveBack, kMoveLeft, kMoveRight, kLookUp, kLookDown, kLookLeft, kLookRight,
    kReload, kUse, kZoomIn, kZoomOut, kCrouch, kProne, kWeapon1, kWeapon2, kGrenades,
    kTargetCount
};
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

// the game's button prompts (game/prompts.cpp), any thread: the keys and the mouse were used last, rather than a
// controller; and an action's first key, as the overlay shows it ("" for none)
bool UsingKeyboard();
std::string FirstKey(int action);
// in play, where a controller is remapped (SetPlay lately), not in a menu or a tutorial's box
bool InPlay();

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
// the keys the menus take on their own (game/menus.cpp), as the menus' buttons: the move keys the d-pad's, crouch's X.
// Play never sees them, so they move through a menu and only move in play
constexpr uint32_t kMenuUp = 0x1, kMenuDown = 0x2, kMenuLeft = 0x4, kMenuRight = 0x8, kMenuX = 0x20;
uint32_t MenuKeys();

// the overlay (common/overlay.h) opened or closed, any thread: the window takes the mouse or lets it go
void Refresh();

// a polled controller: the keys and the mouse merged into port 0, which is connected without a controller too
void Pad(int port, input::Pad& pad);

// crouch or prone pressed on the keys in play since the last call (game/play_keys.cpp, the game's thread), or none;
// and a weapon, 0 the primary, 1 the secondary, 2 the grenades, -1 none
enum class Stance { None, Crouch, Prone };
Stance TakeStance();
int TakeWeapon();

// the A button's two actions in game, reload and use (game/actions.cpp): which one alone the keys press it for, as of
// the last poll; Both when a controller's A or a key bound to A itself holds it, or nothing does
enum class AOnly { Both, Reload, Use };
AOnly AButton();

// The free camera (game/free_camera.cpp), the overlay's hack, off at each start: the player's input moves the camera
// instead, and the game gets none but start and back. TakeFreeCamera, once a frame on the game's thread: the move,
// -1 to 1 each (right, forward, up), the right stick's turn, -1 to 1 (right, up), the mouse's since the last call,
// radians (right, up positive), and the speed's steps since the last call (up positive)
extern bool g_free_camera;
extern bool g_free_camera_drop;  // turned off, the player is put where the camera is
struct FreeCamera {
    float move[3] = {}, turn[2] = {}, yaw = 0, pitch = 0;
    int speed = 0;
};
FreeCamera TakeFreeCamera();

// direct look, once a frame from the game's look handler: the mouse's motion since the last call as the view's turn,
// radians, right and up positive; 0 when the game window is not active. While the game calls it the mouse no longer
// moves the right stick
void TakeLook(float& yaw, float& pitch);

}  // namespace pc_input
