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
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>
#include <toml++/toml.hpp>

#include "app/app.h"

namespace pc_input {

uint32_t g_mouse_sensitivity = 100;
bool g_invert_mouse_y = false;

namespace {

using clock = std::chrono::steady_clock;

// what a key can drive: the controller's buttons, and each stick direction
enum Target : int {
    kA, kB, kX, kY, kBlack, kWhite, kLeftTrigger, kRightTrigger,
    kDpadUp, kDpadDown, kDpadLeft, kDpadRight, kStart, kBack, kLeftThumb, kRightThumb,
    kMoveForward, kMoveBack, kMoveLeft, kMoveRight, kLookUp, kLookDown, kLookLeft, kLookRight,
    kTargetCount
};

constexpr const char* kTargetNames[kTargetCount] = {
    "a", "b", "x", "y", "black", "white", "left_trigger", "right_trigger",
    "dpad_up", "dpad_down", "dpad_left", "dpad_right", "start", "back", "left_thumb", "right_thumb",
    "move_forward", "move_back", "move_left", "move_right", "look_up", "look_down", "look_left", "look_right"};

// virtual key codes (mouse buttons included), plus the wheel as two keys
constexpr int kWheelUp = 0x100, kWheelDown = 0x101;

// the game's Sharp Shooter layout (Options, Controller Settings): A action/reload, B change weapon, X crouch/prone,
// Y jump/stand, black adrenaline, white medkit, left trigger aim, right trigger fire, back mission map, start pause,
// left thumb click position squad, right thumb click melee, d-pad grenade and recall squad. Enter is start: the title
// screen takes only start
std::array<std::vector<int>, kTargetCount> Defaults() {
    std::array<std::vector<int>, kTargetCount> k;
    k[kA] = {'E', 'R'};
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
std::atomic<HWND> g_hwnd{nullptr};

// the wheel's notches are presses held for a moment, long enough for a poll at the game's frame rate to see them
constexpr auto kWheelHold = std::chrono::milliseconds(80);
std::atomic<int64_t> g_wheel_up{0}, g_wheel_down{0};  // clock ticks of the last notch

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
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

bool Active() {
    HWND hwnd = g_hwnd.load();
    return hwnd && GetForegroundWindow() == hwnd && !IsIconic(hwnd);
}

// in game the cursor is hidden and held at the window's centre, so the mouse only turns the view and clicks stay in
// the game; another window taking the focus lets it go. Window thread only: ShowCursor counts per thread
bool g_captured = false;

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

}  // namespace

void Load(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    const toml::table* keys = t["keys"].as_table();
    if (!keys) return;
    std::lock_guard lock(g_lock);
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
            if (!Active()) break;
            Capture(hwnd, true);
            std::lock_guard lock(g_lock);
            g_motion[g_motion_next++ % kMotionSlots] = {clock::now(), raw.data.mouse.lLastX, raw.data.mouse.lLastY};
            break;
        }
        case WM_MOUSEWHEEL:
            (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? g_wheel_up : g_wheel_down) = Ticks(clock::now());
            break;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && Active()) {
                SetCursor(nullptr);
                *result = TRUE;
                return true;
            }
            break;
        case WM_ACTIVATE:
            Capture(hwnd, LOWORD(wp) != WA_INACTIVE);
            break;
        case WM_SIZE:
        case WM_MOVE:
            if (Active()) Capture(hwnd, true);
            break;
    }
    return false;
}

void Pad(int port, input::Pad& pad) {
    if (port != 0) return;
    pad.connected = true;
    if (!Active()) return;
    clock::time_point now = clock::now();
    std::array<bool, kTargetCount> held{};
    float rate_x = 0.0f, rate_y = 0.0f;
    {
        std::lock_guard lock(g_lock);
        for (int i = 0; i < kTargetCount; i++)
            for (int k : g_keys[i]) held[i] = held[i] || Held(k, now);
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

    // keys move the left stick fully over what the controller has; the mouse adds to the right stick
    auto keys_axis = [&](Target neg, Target pos) { return float(held[pos]) - float(held[neg]); };
    if (float x = keys_axis(kMoveLeft, kMoveRight)) pad.lx = Axis(x * 32767.0f);
    if (float y = keys_axis(kMoveBack, kMoveForward)) pad.ly = Axis(y * 32767.0f);
    if (float x = keys_axis(kLookLeft, kLookRight)) pad.rx = Axis(x * 32767.0f);
    if (float y = keys_axis(kLookDown, kLookUp)) pad.ry = Axis(y * 32767.0f);
    // the mouse's y grows downwards, the stick's upwards
    pad.rx = Axis(float(pad.rx) + Stick(rate_x));
    pad.ry = Axis(float(pad.ry) + Stick(g_invert_mouse_y ? rate_y : -rate_y));
}

}  // namespace pc_input
