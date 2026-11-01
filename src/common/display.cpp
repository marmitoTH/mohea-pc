// See display.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>

#include <imgui.h>

#include "common/display.h"

#include "app/app.h"
#include "common/options.h"
#include "common/overlay.h"
#include "common/pc_input.h"
#include "common/sharpen.h"
#include "common/ssao.h"
#include "core/log.h"

namespace display {

namespace {

// the full screen mode to go back to from a window: the one the game started in, else borderless
app::DisplayMode g_full = app::DisplayMode::Borderless;
bool g_full_known = false;

void Toggle() {
    app::Settings& s = app::Get();
    if (!g_full_known) {
        g_full_known = true;
        if (s.display_mode != app::DisplayMode::Windowed) g_full = s.display_mode;
    }
    if (s.display_mode == app::DisplayMode::Windowed) {
        s.display_mode = g_full;
    } else {
        g_full = s.display_mode;
        s.display_mode = app::DisplayMode::Windowed;
    }
    LOGI("display: %s", s.display_mode == app::DisplayMode::Windowed ? "windowed"
                        : s.display_mode == app::DisplayMode::Borderless ? "borderless" : "fullscreen");
    app::Save();
    app::ApplyDisplay();
}

// a notice in the corner for a moment after F9, F8 or F7: what it switched to
using clock = std::chrono::steady_clock;
clock::time_point g_notice_until{};
const char* g_notice = "";

void Notice(const char* text) {
    g_notice = text;
    g_notice_until = clock::now() + std::chrono::seconds(2);
}

// F9: 30 or 60 frames a second in game (game/frame_rate.cpp), to compare the two
void ToggleFrameRate() {
    g_sixty_fps = !g_sixty_fps;
    Notice(g_sixty_fps ? "60 FPS" : "30 FPS");
    LOGR("frame rate: %d in game", g_sixty_fps ? 60 : 30);
    app::Save();
}

// F8: the sharpening of the 3D picture (common/sharpen.h)
void ToggleSharpening() {
    sharpen::g_on = !sharpen::g_on;
    Notice(sharpen::g_on ? "Sharpening ON" : "Sharpening OFF");
    LOGR("sharpening: %s", sharpen::g_on ? "on" : "off");
    app::Save();
}

// F7: the ambient occlusion of the 3D picture (common/ssao.h)
void ToggleSsao() {
    ssao::g_on = !ssao::g_on;
    Notice(ssao::g_on ? "SSAO ON" : "SSAO OFF");
    LOGR("ssao: %s", ssao::g_on ? "on" : "off");
    app::Save();
}

}  // namespace

bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    bool first = !(lp & (1 << 30));  // not a key held down repeating
    // a key being bound in the overlay: the key is the binding's, Esc cancels it (common/pc_input.h)
    if (pc_input::Capturing()) return false;
    switch (msg) {
        case WM_KEYDOWN:
            // F1 opens and closes the overlay, Esc closes it
            if (wp == VK_F1 || (wp == VK_ESCAPE && overlay::g_open.load())) {
                if (first) overlay::Toggle();
                *result = 0;
                return true;
            }
            if (wp == VK_F11) {
                if (first) Toggle();
                *result = 0;
                return true;
            }
            if (wp == VK_F9) {
                if (first) ToggleFrameRate();
                *result = 0;
                return true;
            }
            if (wp == VK_F8) {
                if (first) ToggleSharpening();
                *result = 0;
                return true;
            }
            if (wp == VK_F7) {
                if (first) ToggleSsao();
                *result = 0;
                return true;
            }
            break;
        case WM_SYSKEYDOWN:
            // Alt+Enter; the alt bit is 29
            if (wp == VK_RETURN && (lp & (1 << 29))) {
                if (first) Toggle();
                *result = 0;
                return true;
            }
            break;
        case WM_SYSCHAR:
            // its character: without this the default handling beeps
            if (wp == '\r' && (lp & (1 << 29))) {
                *result = 0;
                return true;
            }
            break;
    }
    return false;
}

void Ui() {
    overlay::Draw();
    if (clock::now() >= g_notice_until) return;
    const char* text = g_notice;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    float size = ImGui::GetFontSize() * 2.0f;
    ImVec2 at(24.0f, 24.0f);
    draw->AddText(ImGui::GetFont(), size, ImVec2(at.x + 2, at.y + 2), IM_COL32(0, 0, 0, 200), text);
    draw->AddText(ImGui::GetFont(), size, at, IM_COL32(255, 255, 255, 255), text);
}

}  // namespace display
