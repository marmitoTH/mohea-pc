// See pc_input.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "common/pc_input.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <iterator>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>
#include <toml++/toml.hpp>

#include "app/app.h"
#include "common/overlay.h"

namespace pc_input {

uint32_t g_mouse_sensitivity = 100;
bool g_invert_mouse_y = false;
bool g_swap_sticks = false;

namespace {

using clock = std::chrono::steady_clock;

// what a key can drive: the controller's buttons, and each stick direction
enum Target : int {
    kA, kB, kX, kY, kBlack, kWhite, kLeftTrigger, kRightTrigger,
    kDpadUp, kDpadDown, kDpadLeft, kDpadRight, kStart, kBack, kLeftThumb, kRightThumb,
    kMoveForward, kMoveBack, kMoveLeft, kMoveRight, kLookUp, kLookDown, kLookLeft, kLookRight,
    kReload, kUse,
    kTargetCount
};

constexpr const char* kTargetNames[kTargetCount] = {
    "a", "b", "x", "y", "black", "white", "left_trigger", "right_trigger",
    "dpad_up", "dpad_down", "dpad_left", "dpad_right", "start", "back", "left_thumb", "right_thumb",
    "move_forward", "move_back", "move_left", "move_right", "look_up", "look_down", "look_left", "look_right",
    "reload", "use"};

// what each does in game, the default layout's button after it
constexpr const char* kActionNames[kTargetCount] = {
    "Use / reload (A)",          "Change weapon (B)",         "Crouch / prone (X)",      "Jump / stand (Y)",
    "Adrenaline (black)",        "Medkit (white)",            "Aim (left trigger)",      "Fire (right trigger)",
    "Grenade (d-pad up)",        "Recall squad (d-pad down)", "D-pad left",              "D-pad right",
    "Pause (start)",             "Mission map (back)",        "Position squad (L3)",     "Melee (R3)",
    "Move forward",              "Move back",                 "Move left",               "Move right",
    "Look up",                   "Look down",                 "Look left",               "Look right",
    "Reload",                    "Use"};

// the overlay's order: moving and looking, the actions, reload and use beside A
constexpr Target kListed[kTargetCount] = {
    kMoveForward, kMoveBack, kMoveLeft, kMoveRight, kLookUp, kLookDown, kLookLeft, kLookRight,
    kRightTrigger, kLeftTrigger, kA, kReload, kUse, kB, kX, kY, kRightThumb, kLeftThumb,
    kDpadUp, kDpadDown, kDpadLeft, kDpadRight, kBlack, kWhite, kStart, kBack};

// a controller's buttons, in the targets' order: the analog ones (input::Pad's analog), then the digital ones (its
// buttons' bits)
constexpr const char* kPadButtonNames[kPadButtons] = {
    "A", "B", "X", "Y", "Black", "White", "Left trigger", "Right trigger",
    "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Start", "Back", "Left stick (L3)", "Right stick (R3)"};

// an analog button the game counts as held (sub_000C4D50: over 0x7F)
constexpr uint8_t kAnalogHeld = 0x7F;

// virtual key codes (mouse buttons included), plus the wheel as two keys
constexpr int kWheelUp = 0x100, kWheelDown = 0x101;

// the game's Sharp Shooter layout (Options, Controller Settings): A action/reload (here R reload, E use: each
// presses A for the one; game/actions.cpp), B change weapon, X crouch/prone,
// Y jump/stand, black adrenaline, white medkit, left trigger aim, right trigger fire, back mission map, start pause,
// left thumb click position squad, right thumb click melee, d-pad grenade and recall squad. Enter is start: the title
// screen takes only start
std::array<std::vector<int>, kTargetCount> Defaults() {
    std::array<std::vector<int>, kTargetCount> k;
    k[kReload] = {'R'};
    k[kUse] = {'E'};
    k[kB] = {'Q', kWheelUp, kWheelDown, VK_BACK};
    k[kX] = {'C', VK_LCONTROL};
    k[kY] = {VK_SPACE};
    k[kBlack] = {'F'};
    k[kWhite] = {'H'};
    k[kLeftTrigger] = {VK_RBUTTON};
    k[kRightTrigger] = {VK_LBUTTON};
    k[kDpadUp] = {VK_UP, 'G'};
    k[kDpadDown] = {VK_DOWN, 'X'};
    k[kDpadLeft] = {VK_LEFT};
    k[kDpadRight] = {VK_RIGHT};
    k[kStart] = {VK_ESCAPE, VK_RETURN};
    k[kBack] = {VK_TAB};
    k[kLeftThumb] = {'Z'};
    k[kRightThumb] = {VK_MBUTTON, 'V'};
    k[kMoveForward] = {'W'};
    k[kMoveBack] = {'S'};
    k[kMoveLeft] = {'A'};
    k[kMoveRight] = {'D'};
    return k;
}

std::mutex g_lock;  // the bindings and the mouse's motion: the pads are polled from several threads
std::array<std::vector<int>, kTargetCount> g_keys = Defaults();
// each action a controller's button can drive (OnPad), the button on it (-1 none): at first its own, none on reload
// and use (A does both)
bool PadAction(int action) { return action < kPadButtons || action == kReload || action == kUse; }
std::array<int, kTargetCount> PadDefaults() {
    std::array<int, kTargetCount> p;
    for (int i = 0; i < kTargetCount; i++) p[i] = i < kPadButtons ? i : -1;
    return p;
}

// The presets: the game's four layouts as it defines them (game/layout.cpp; Sharp Shooter is the default, the others
// move a few of its actions) and a modern one, the triggers to aim and fire, A to jump, B to crouch, X to use and
// reload, Y to change weapon, the bumpers (black and white) the grenade and adrenaline; then the player's own
constexpr const char* kPresetNames[kPadPresets] = {"Sharp Shooter", "Commander", "Recon", "Reloader", "Modern",
                                                   "Custom"};
constexpr const char* kPresetKeys[kPadPresets] = {"sharp_shooter", "commander", "recon", "reloader", "modern",
                                                  "custom"};
std::array<int, kTargetCount> PresetMap(int preset) {
    std::array<int, kTargetCount> p = PadDefaults();
    switch (preset) {
        case 1:  // Commander
            p[kLeftTrigger] = kLeftThumb, p[kLeftThumb] = kLeftTrigger, p[kDpadDown] = kBlack, p[kWhite] = kDpadDown;
            p[kBlack] = kDpadUp, p[kDpadUp] = kWhite;
            break;
        case 2:  // Recon
            p[kLeftThumb] = kDpadUp, p[kDpadUp] = kLeftThumb;
            break;
        case 3:  // Reloader
            p[kReload] = kWhite, p[kDpadDown] = kBlack, p[kWhite] = kDpadDown, p[kBlack] = kDpadUp;
            p[kDpadUp] = kDpadLeft;
            break;
        case 4:  // Modern
            p[kA] = kX, p[kY] = kA, p[kX] = kB, p[kB] = kY, p[kDpadUp] = kBlack, p[kBlack] = kWhite;
            p[kWhite] = kDpadUp;
            break;
    }
    return p;
}

// the preset chosen, the player's own mapping, and what the controller has now (one or the other)
int g_preset = 0;
std::array<int, kTargetCount> g_custom = PadDefaults();
std::array<int, kTargetCount> g_pad = PadDefaults();
std::atomic<HWND> g_hwnd{nullptr};
std::atomic<AOnly> g_a_only{AOnly::Both};

// SetPlay: the player control ran lately (clock ticks until), and the player looks through a scope
constexpr auto kPlayHold = std::chrono::milliseconds(150);
std::atomic<int64_t> g_play_until{0};
std::atomic<bool> g_scoped{false};

// the wheel's notches are presses held for a moment, long enough for a poll at the game's frame rate to see them
constexpr auto kWheelHold = std::chrono::milliseconds(80);
std::atomic<int64_t> g_wheel_up{0}, g_wheel_down{0};  // clock ticks of the last notch

// the overlay's key capture (driven from the UI thread, the window thread records the wheel for it): waiting for
// every key to be up, then for one; the wheel's notches as they were when it began waiting
enum class Capture { Off, Up, Waiting };
std::atomic<Capture> g_capture{Capture::Off};
int64_t g_capture_up = 0, g_capture_down = 0;

// the mouse's motion, each raw report with its time: the right stick is its rate over the last moments, so however
// many threads poll, each sees the same stick and none takes the motion from another
struct Motion {
    clock::time_point t;
    int dx, dy;
};
constexpr size_t kMotionSlots = 1024;
std::array<Motion, kMotionSlots> g_motion{};
size_t g_motion_next = 0;
constexpr auto kRateWindow = std::chrono::milliseconds(25);

// the stick's full deflection is this many counts a second at 100%; the game ignores the stick near its centre, so
// any motion starts past that
constexpr float kFullStickCountsPerSecond = 800.0f;
constexpr float kStickOffset = 8000.0f;

// direct look (game/controls.cpp): the counts since the game last took them, and when it last did; while it takes
// them the mouse leaves the right stick alone
std::atomic<int> g_look_x{0}, g_look_y{0};
std::atomic<int64_t> g_direct_until{0};
constexpr auto kDirectHold = std::chrono::milliseconds(250);
// the view's turn per count at 100%, radians: 0.057 degrees, 45 degrees for an inch at 800 counts an inch
constexpr float kRadiansPerCount = 0.001f;

struct KeyName {
    const char* name;
    int key;
};
constexpr KeyName kNamedKeys[] = {
    {"space", VK_SPACE},       {"enter", VK_RETURN},     {"escape", VK_ESCAPE},      {"tab", VK_TAB},
    {"backspace", VK_BACK},    {"shift", VK_SHIFT},      {"left_shift", VK_LSHIFT},  {"right_shift", VK_RSHIFT},
    {"ctrl", VK_CONTROL},      {"left_ctrl", VK_LCONTROL}, {"right_ctrl", VK_RCONTROL}, {"alt", VK_MENU},
    {"up", VK_UP},             {"down", VK_DOWN},        {"left", VK_LEFT},          {"right", VK_RIGHT},
    {"insert", VK_INSERT},     {"delete", VK_DELETE},    {"home", VK_HOME},          {"end", VK_END},
    {"page_up", VK_PRIOR},     {"page_down", VK_NEXT},   {"caps_lock", VK_CAPITAL},
    {"mouse1", VK_LBUTTON},    {"mouse2", VK_RBUTTON},   {"mouse3", VK_MBUTTON},     {"mouse4", VK_XBUTTON1},
    {"mouse5", VK_XBUTTON2},   {"wheel_up", kWheelUp},   {"wheel_down", kWheelDown}};

std::string NameOf(int key) {
    for (const KeyName& k : kNamedKeys)
        if (k.key == key) return k.name;
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) return std::string(1, char(std::tolower(key)));
    if (key >= VK_F1 && key <= VK_F12) return "f" + std::to_string(key - VK_F1 + 1);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "vk%02x", key);
    return buf;
}

// 0 when the name is no key
int KeyOf(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    for (const KeyName& k : kNamedKeys)
        if (name == k.name) return k.key;
    if (name.size() == 1 && std::isalnum(static_cast<unsigned char>(name[0]))) return std::toupper(name[0]);
    if (name.size() >= 2 && name[0] == 'f' && std::isdigit(static_cast<unsigned char>(name[1]))) {
        int n = std::atoi(name.c_str() + 1);
        if (n >= 1 && n <= 12) return VK_F1 + n - 1;
    }
    if (name.size() == 4 && name.compare(0, 2, "vk") == 0) return int(std::strtol(name.c_str() + 2, nullptr, 16));
    return 0;
}

int64_t Ticks(clock::time_point t) { return t.time_since_epoch().count(); }

bool Held(int key, clock::time_point now) {
    if (key == kWheelUp) return now - clock::time_point(clock::duration(g_wheel_up.load())) < kWheelHold;
    if (key == kWheelDown) return now - clock::time_point(clock::duration(g_wheel_down.load())) < kWheelHold;
    // Alt+Enter switches the full screen (common/display.cpp), it presses nothing in the game
    if (key == VK_RETURN && (GetAsyncKeyState(VK_MENU) & 0x8000)) return false;
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

bool Active() {
    HWND hwnd = g_hwnd.load();
    return hwnd && GetForegroundWindow() == hwnd && !IsIconic(hwnd);
}

// in game the cursor is hidden and held at the window's centre, so the mouse only turns the view and clicks stay in
// the game; another window taking the focus lets it go. Windowed, Esc lets it go too (the window can be resized) until
// a click in the window, which presses nothing: the mouse's buttons are not read until they are all up again. Window
// thread only: ShowCursor counts per thread
bool g_captured = false;
std::atomic<bool> g_released{false}, g_swallow{false};

bool Windowed() { return app::Get().display_mode == app::DisplayMode::Windowed; }

// the mouse turns the view and its buttons press: not while it is let go, nor while the overlay is open
bool Captured() { return Active() && !g_released.load() && !overlay::g_open.load(); }

// the overlay open from a controller, the mouse not moved since: the cursor stays hidden
bool PadOverlay() { return overlay::g_open.load() && overlay::g_by_pad.load(); }

// the overlay opened or closed (Refresh): the window takes the mouse or lets it go; ShowCursor counts per thread
constexpr UINT kRefresh = WM_APP + 0x40;

bool MouseButton(int key) {
    return key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON || key == VK_XBUTTON1 || key == VK_XBUTTON2;
}

void Capture(HWND hwnd, bool on) {
    if (on != g_captured) {
        ShowCursor(on ? FALSE : TRUE);
        g_captured = on;
    }
    if (!on) {
        ClipCursor(nullptr);
        return;
    }
    RECT r;
    GetClientRect(hwnd, &r);
    MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
    LONG x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2;
    RECT centre{x, y, x + 1, y + 1};
    RECT now;
    if (!GetClipCursor(&now) || !EqualRect(&now, &centre)) ClipCursor(&centre);
}

int16_t Axis(float v) { return int16_t(std::clamp(v, -32768.0f, 32767.0f)); }

// counts a second past the stick's dead zone, or 0
float Stick(float rate) {
    if (rate == 0.0f) return 0.0f;
    float v = std::abs(rate) / kFullStickCountsPerSecond * 32767.0f * float(g_mouse_sensitivity) / 100.0f;
    v = kStickOffset + v * (32767.0f - kStickOffset) / 32767.0f;
    return std::copysign(std::min(v, 32767.0f), rate);
}

// a key as the overlay shows it: the settings file's name, its words capitalized, the mouse's buttons spelt out
std::string ShownName(int key) {
    switch (key) {
        case VK_LBUTTON: return "Left mouse";
        case VK_RBUTTON: return "Right mouse";
        case VK_MBUTTON: return "Middle mouse";
        case VK_XBUTTON1: return "Mouse 4";
        case VK_XBUTTON2: return "Mouse 5";
    }
    std::string name = NameOf(key);
    if (name.size() == 4 && name.compare(0, 2, "vk") == 0) {
        // no name of ours: Windows' own
        char text[32];
        if (UINT scan = MapVirtualKeyA(UINT(key), MAPVK_VK_TO_VSC); scan && GetKeyNameTextA(LONG(scan << 16), text, 32))
            return text;
        return name;
    }
    bool start = true;
    for (char& c : name) {
        if (c == '_') {
            c = ' ';
            start = true;
        } else {
            if (start) c = char(std::toupper(static_cast<unsigned char>(c)));
            start = false;
        }
    }
    return name;
}

// what a controller's buttons held of A's actions: A's own (both), reload's and use's
struct PadA {
    bool a = false, reload = false, use = false;
};

// a controller's buttons put on the actions they are mapped to, and its sticks swapped; reload's and use's press A
PadA Remap(input::Pad& pad) {
    std::array<int, kTargetCount> map;
    bool swap;
    {
        std::lock_guard lock(g_lock);
        map = g_pad;
        swap = g_swap_sticks;
    }
    // each button's value, analog or held (0xFF)
    std::array<uint8_t, kPadButtons> in{};
    for (int i = 0; i < 8; i++) in[i] = pad.analog[i];
    for (int i = 0; i < 8; i++) in[8 + i] = (pad.buttons >> i & 1) ? 0xFF : 0;
    auto value = [&](int action) { return map[action] >= 0 ? in[map[action]] : uint8_t(0); };
    uint16_t buttons = pad.buttons & ~uint16_t(0xFF);
    for (int a = 0; a < kPadButtons; a++) {
        uint8_t v = value(a);
        if (a < 8) pad.analog[a] = v;
        else if (v > kAnalogHeld) buttons |= uint16_t(1u << (a - 8));
    }
    pad.buttons = buttons;
    PadA held{pad.analog[kA] > kAnalogHeld, value(kReload) > kAnalogHeld, value(kUse) > kAnalogHeld};
    if (held.reload || held.use) pad.analog[kA] = 0xFF;
    if (swap) {
        std::swap(pad.lx, pad.rx);
        std::swap(pad.ly, pad.ry);
    }
    return held;
}

}  // namespace

void Load(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    std::lock_guard lock(g_lock);
    // [pad]: the preset, and the custom mapping, each action its button's name or none (a file from before the
    // presets has the mapping alone: custom when it is not the default)
    if (const toml::table* pad = t["pad"].as_table()) {
        for (int a = 0; a < kTargetCount; a++)
            if (auto name = (*pad)[kTargetNames[a]].value<std::string>(); name && PadAction(a)) {
                g_custom[a] = -1;
                for (int b = 0; b < kPadButtons; b++)
                    if (*name == kTargetNames[b]) g_custom[a] = b;
            }
        g_preset = g_custom == PadDefaults() ? 0 : kPadPresets - 1;
        if (auto key = (*pad)["preset"].value<std::string>())
            for (int p = 0; p < kPadPresets; p++)
                if (*key == kPresetKeys[p]) g_preset = p;
        g_swap_sticks = (*pad)["swap_sticks"].value_or(g_swap_sticks);
    }
    g_pad = g_preset == kPadPresets - 1 ? g_custom : PresetMap(g_preset);
    const toml::table* keys = t["keys"].as_table();
    if (!keys) return;
    for (int i = 0; i < kTargetCount; i++) {
        const toml::node* n = keys->get(kTargetNames[i]);
        if (!n) continue;
        std::vector<int> bound;
        auto add = [&](const toml::node& v) {
            if (auto s = v.value<std::string>())
                if (int k = KeyOf(*s)) bound.push_back(k);
        };
        if (const toml::array* a = n->as_array()) {
            for (const toml::node& v : *a) add(v);
        } else {
            add(*n);
        }
        g_keys[i] = std::move(bound);
    }
    // a file from before reload and use had their own keys has them on A, which presses both: off it
    if (!keys->get(kTargetNames[kReload]) && !keys->get(kTargetNames[kUse])) {
        const auto defaults = Defaults();
        std::erase_if(g_keys[kA], [&](int k) {
            for (Target t : {kReload, kUse})
                if (std::find(defaults[t].begin(), defaults[t].end(), k) != defaults[t].end()) return true;
            return false;
        });
    }
}

void Save(void* table) {
    toml::table& t = *static_cast<toml::table*>(table);
    toml::table keys;
    std::lock_guard lock(g_lock);
    for (int i = 0; i < kTargetCount; i++) {
        toml::array a;
        for (int k : g_keys[i]) a.push_back(NameOf(k));
        keys.insert(kTargetNames[i], std::move(a));
    }
    t.insert_or_assign("keys", std::move(keys));
    toml::table pad;
    pad.insert("preset", kPresetKeys[g_preset]);
    for (int a = 0; a < kTargetCount; a++)
        if (PadAction(a)) pad.insert(kTargetNames[a], g_custom[a] >= 0 ? kTargetNames[g_custom[a]] : "none");
    pad.insert("swap_sticks", g_swap_sticks);
    t.insert_or_assign("pad", std::move(pad));
}

int Actions() { return kTargetCount; }

bool OnPad(int action) { return PadAction(action); }

const char* ActionName(int action) { return kActionNames[action]; }

int ListedAction(int i) { return kListed[i]; }

std::string KeysText(int action) {
    std::lock_guard lock(g_lock);
    std::string text;
    for (int k : g_keys[action]) text += (text.empty() ? "" : ", ") + ShownName(k);
    return text.empty() ? "-" : text;
}

void SetKeys(int action, int key, bool add) {
    {
        std::lock_guard lock(g_lock);
        std::vector<int>& keys = g_keys[action];
        if (!add) keys.clear();
        if (key && std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
    }
    app::Save();
}

const char* PadButtonName(int button) { return button >= 0 ? kPadButtonNames[button] : "None"; }

int PadButton(int action) {
    std::lock_guard lock(g_lock);
    return g_pad[action];
}

void SetPadButton(int action, int button) {
    {
        std::lock_guard lock(g_lock);
        // a preset's mapping changed: the player's own from then on
        g_custom = g_pad;
        g_preset = kPadPresets - 1;
        g_custom[action] = button;
        g_pad = g_custom;
    }
    app::Save();
}

const char* PadPresetName(int preset) { return kPresetNames[preset]; }

int PadPreset() {
    std::lock_guard lock(g_lock);
    return g_preset;
}

void SetPadPreset(int preset) {
    {
        std::lock_guard lock(g_lock);
        g_preset = preset;
        g_pad = preset == kPadPresets - 1 ? g_custom : PresetMap(preset);
    }
    app::Save();
}

void ResetBindings() {
    std::lock_guard lock(g_lock);
    g_keys = Defaults();
    g_preset = 0;
    g_custom = PadDefaults();
    g_pad = PadDefaults();
    g_swap_sticks = false;
}

void StartCapture() { g_capture = Capture::Up; }

bool Capturing() { return g_capture.load() != Capture::Off; }

void CancelCapture() { g_capture = Capture::Off; }

int CapturedKey() {
    // the generic shift, ctrl and alt are their left and right ones
    auto down = [](int k) {
        return k != VK_SHIFT && k != VK_CONTROL && k != VK_MENU && (GetAsyncKeyState(k) & 0x8000);
    };
    if (g_capture.load() == Capture::Up) {
        for (int k = 1; k < 0xFF; k++)
            if (down(k)) return 0;
        g_capture_up = g_wheel_up.load();
        g_capture_down = g_wheel_down.load();
        g_capture = Capture::Waiting;
        return 0;
    }
    if (g_capture.load() != Capture::Waiting) return -1;
    int key = 0;
    if (down(VK_ESCAPE)) {
        g_capture = Capture::Off;
        return -1;
    }
    for (int k = 1; k < 0xFF && !key; k++)
        if (down(k)) key = k;
    if (!key && g_wheel_up.load() != g_capture_up) key = kWheelUp;
    if (!key && g_wheel_down.load() != g_capture_down) key = kWheelDown;
    if (key) g_capture = Capture::Off;
    return key;
}

void Window(void* hwnd) {
    g_hwnd = static_cast<HWND>(hwnd);
    RAWINPUTDEVICE mouse{0x01, 0x02, 0, static_cast<HWND>(hwnd)};  // generic desktop, mouse
    RegisterRawInputDevices(&mouse, 1, sizeof(mouse));
    {
        // ImGui sets the arrow cursor each frame and takes WM_SETCURSOR, which showed the cursor over the game
        std::lock_guard lock(app::UiLock());
        if (ImGui::GetCurrentContext()) ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    }
    if (Active()) Capture(static_cast<HWND>(hwnd), true);
}

bool Message(void* hwnd_ptr, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    HWND hwnd = static_cast<HWND>(hwnd_ptr);
    switch (msg) {
        case WM_INPUT: {
            RAWINPUT raw;
            UINT size = sizeof(raw);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) ==
                    UINT(-1) ||
                raw.header.dwType != RIM_TYPEMOUSE || (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
                break;
            // the overlay opened from a controller: the mouse moved, so it is the mouse's
            if (PadOverlay() && (raw.data.mouse.lLastX || raw.data.mouse.lLastY)) {
                overlay::g_by_pad = false;
                Capture(hwnd, false);
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            }
            if (!Captured()) break;
            Capture(hwnd, true);
            std::lock_guard lock(g_lock);
            g_motion[g_motion_next++ % kMotionSlots] = {clock::now(), raw.data.mouse.lLastX, raw.data.mouse.lLastY};
            g_look_x += raw.data.mouse.lLastX;
            g_look_y += raw.data.mouse.lLastY;
            break;
        }
        case WM_MOUSEWHEEL:
            if (Captured() || g_capture.load() == Capture::Waiting)
                (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? g_wheel_up : g_wheel_down) = Ticks(clock::now());
            break;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE && Windowed() && !(lp & (1 << 30))) {
                g_released = true;
                Capture(hwnd, false);
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            }
            break;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            if (g_released.load() && Active() && !overlay::g_open.load()) {
                g_released = false;
                g_swallow = true;
                Capture(hwnd, true);
            }
            break;
        case WM_SETCURSOR:
            // the window's class has no cursor: over the game the arrow while the mouse is let go
            if (LOWORD(lp) == HTCLIENT && Active()) {
                SetCursor(Captured() || PadOverlay() ? nullptr : LoadCursorW(nullptr, IDC_ARROW));
                *result = TRUE;
                return true;
            }
            break;
        case WM_ACTIVATE:
            Capture(hwnd, LOWORD(wp) != WA_INACTIVE && !g_released.load() && !overlay::g_open.load());
            break;
        case kRefresh:
            // held and hidden while the overlay is a controller's
            Capture(hwnd, Captured() || (PadOverlay() && Active()));
            if (!Captured() && !PadOverlay()) SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            // the click that closed the overlay presses nothing in the game
            g_swallow = true;
            *result = 0;
            return true;
        case WM_SIZE:
        case WM_MOVE:
            if (!Windowed()) g_released = false;
            if (Captured()) Capture(hwnd, true);
            break;
    }
    return false;
}

void SetPlay(bool scoped) {
    g_scoped = scoped;
    g_play_until = Ticks(clock::now() + kPlayHold);
}

void Refresh() {
    if (HWND hwnd = g_hwnd.load()) PostMessageW(hwnd, kRefresh, 0, 0);
}

void Pad(int port, input::Pad& pad) {
    // a controller's both sticks clicked open or close the overlay, once until let go: each port on its own, as the
    // ports are polled in turn; while the overlay is open the game gets no input
    constexpr uint16_t kBothThumbs = 0xC0;  // left and right thumb, input::Pad's buttons
    static std::array<std::atomic<bool>, 4> thumbs_held{};
    if (port >= 0 && port < int(thumbs_held.size())) {
        bool thumbs = (pad.buttons & kBothThumbs) == kBothThumbs;
        if (thumbs && !thumbs_held[port].exchange(true)) overlay::Toggle(true);
        if (!thumbs) thumbs_held[port] = false;
    }
    // once the overlay closes a controller's buttons press nothing until all are let go: B or both sticks closed it
    static std::array<std::atomic<bool>, 4> after_overlay{};
    bool ported = port >= 0 && port < int(after_overlay.size());
    if (overlay::g_open.load()) {
        bool connected = port == 0 || pad.connected;
        pad = input::Pad{};
        pad.connected = connected;
        g_a_only = AOnly::Both;
        if (ported) after_overlay[port] = true;
        return;
    }
    if (ported && after_overlay[port].load()) {
        bool down = pad.buttons != 0;
        for (uint8_t a : pad.analog) down = down || a > kAnalogHeld;
        if (down) {
            pad.buttons = 0;
            std::fill(std::begin(pad.analog), std::end(pad.analog), uint8_t(0));
        } else {
            after_overlay[port] = false;
        }
    }
    // in play a controller's buttons on their actions (the keys below are bound to actions already); in the menus, the
    // pause menu among them, its buttons as they are, the d-pad, A and B working them
    bool play = Ticks(clock::now()) < g_play_until.load();
    PadA from_pad{pad.analog[kA] > kAnalogHeld};
    if (pad.connected && play) from_pad = Remap(pad);
    if (port != 0) return;
    pad.connected = true;
    // which of A's actions alone is held: by the controller's reload or use button, or a key's (below)
    auto a_only = [](bool both, bool reload, bool use) {
        return both || (reload && use) ? AOnly::Both : reload ? AOnly::Reload : use ? AOnly::Use : AOnly::Both;
    };
    if (!Active()) {
        g_a_only = a_only(from_pad.a, from_pad.reload, from_pad.use);
        return;
    }
    clock::time_point now = clock::now();
    bool scoped = g_scoped.load();
    std::array<bool, kTargetCount> held{};
    float rate_x = 0.0f, rate_y = 0.0f;
    {
        std::lock_guard lock(g_lock);
        // the mouse's buttons while it is captured and not swallowing the click that captured it
        bool mouse = Captured();
        if (g_swallow.load()) {
            bool down = false;
            for (int k : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON}) down = down || (GetAsyncKeyState(k) & 0x8000);
            if (down) mouse = false;
            else g_swallow = false;
        }
        // the keys bound, and in the menus the default ones too, so the arrows, Enter, Esc and Backspace work them
        // however the keys are bound
        static const std::array<std::vector<int>, kTargetCount> kDefaults = Defaults();
        auto press = [&](int i, int k) {
            // the wheel only in play, and not as bound in a scope (below)
            if (k == kWheelUp || k == kWheelDown) {
                if (play && !scoped) held[i] = held[i] || Held(k, now);
                return;
            }
            held[i] = held[i] || ((mouse || !MouseButton(k)) && Held(k, now));
        };
        for (int i = 0; i < kTargetCount; i++) {
            for (int k : g_keys[i]) press(i, k);
            if (!play)
                for (int k : kDefaults[i]) press(i, k);
        }
        // in a scope the wheel zooms: the d-pad's up narrows the field of view (in), its down widens it (out)
        if (play && scoped) {
            held[kDpadUp] = held[kDpadUp] || Held(kWheelUp, now);
            held[kDpadDown] = held[kDpadDown] || Held(kWheelDown, now);
        }
        int sum_x = 0, sum_y = 0;
        for (const Motion& m : g_motion)
            if (now - m.t < kRateWindow) {
                sum_x += m.dx;
                sum_y += m.dy;
            }
        float seconds = std::chrono::duration<float>(kRateWindow).count();
        rate_x = float(sum_x) / seconds;
        rate_y = float(sum_y) / seconds;
    }

    // digital buttons: d-pad up/down/left/right, start, back, left thumb, right thumb
    constexpr Target kDigital[] = {kDpadUp, kDpadDown, kDpadLeft, kDpadRight, kStart, kBack, kLeftThumb, kRightThumb};
    for (int i = 0; i < 8; i++)
        if (held[kDigital[i]]) pad.buttons |= uint16_t(1u << i);
    // analog buttons: A B X Y black white, left and right trigger
    for (int i = 0; i < 8; i++)
        if (held[kA + i]) pad.analog[i] = 0xFF;
    // reload and use, the keys' or the controller's, press A, and say which of its actions it is for when nothing else
    // holds it
    if (held[kReload] || held[kUse]) pad.analog[0] = 0xFF;
    g_a_only = a_only(from_pad.a || held[kA], held[kReload] || from_pad.reload, held[kUse] || from_pad.use);

    // keys push a stick fully over what the controller has, a diagonal as far as a controller's stick goes there: an
    // Xbox 360 pad's left stick pushed into a diagonal reads about 25900 an axis, 1.10 to 1.14 times a straight push
    // (both axes at full, 1.41, ran faster than straight; 1.0 ran slower: the game's stick shaping and its walk and
    // run thresholds go by each axis); the mouse adds to the right stick
    constexpr float kDiagonal = 1.12f;
    auto keys_axis = [&](Target neg, Target pos) { return float(held[pos]) - float(held[neg]); };
    auto keys_stick = [&](Target left, Target right, Target down, Target up, int16_t& x, int16_t& y) {
        float kx = keys_axis(left, right), ky = keys_axis(down, up);
        if (kx == 0.0f && ky == 0.0f) return;
        float length = kx != 0.0f && ky != 0.0f ? kDiagonal : 1.0f;
        float scale = 32767.0f * length / std::sqrt(kx * kx + ky * ky);
        x = Axis(kx * scale);
        y = Axis(ky * scale);
    };
    keys_stick(kMoveLeft, kMoveRight, kMoveBack, kMoveForward, pad.lx, pad.ly);
    keys_stick(kLookLeft, kLookRight, kLookDown, kLookUp, pad.rx, pad.ry);
    // the mouse's y grows downwards, the stick's upwards; not while the game takes the mouse as direct look
    if (Ticks(now) < g_direct_until.load()) return;
    pad.rx = Axis(float(pad.rx) + Stick(rate_x));
    pad.ry = Axis(float(pad.ry) + Stick(g_invert_mouse_y ? rate_y : -rate_y));
}

AOnly AButton() {
    return g_a_only.load();
}

void TakeLook(float& yaw, float& pitch) {
    g_direct_until = Ticks(clock::now() + kDirectHold);
    int x = g_look_x.exchange(0), y = g_look_y.exchange(0);
    if (!Captured()) return;
    float k = kRadiansPerCount * float(g_mouse_sensitivity) / 100.0f;
    yaw = float(x) * k;
    pitch = float(g_invert_mouse_y ? y : -y) * k;
}

}  // namespace pc_input
