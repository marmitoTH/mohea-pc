// See display.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "common/display.h"

#include "app/app.h"
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

}  // namespace

bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    bool first = !(lp & (1 << 30));  // not a key held down repeating
    switch (msg) {
        case WM_KEYDOWN:
            if (wp == VK_F11) {
                if (first) Toggle();
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

}  // namespace display
