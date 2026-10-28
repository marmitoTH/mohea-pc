// See overlay.h.
#include "common/overlay.h"

#include <algorithm>

#include <imgui.h>

#include "app/app.h"
#include "common/options.h"
#include "common/pc_input.h"
#include "common/sharpen.h"
#include "common/ssao.h"
#include "core/log.h"

namespace overlay {

std::atomic<bool> g_open{false};
std::atomic<bool> g_by_pad{false};

namespace {

// the window takes the focus the frame it opens, so a controller's d-pad moves in it at once
std::atomic<bool> g_focus{false};

constexpr const char* kModes[] = {"Fullscreen", "Borderless", "Windowed"};  // app::DisplayMode's order
constexpr const char* kAa[] = {"Off", "2x", "4x", "8x", "16x"};
constexpr uint32_t kAaSamples[] = {0, 2, 4, 8, 16};

// a slider saves once let go, not at each step of a drag
void SaveAfter() {
    if (ImGui::IsItemDeactivatedAfterEdit()) app::Save();
}

void Heading(const char* text) {
    ImGui::Spacing();
    ImGui::SeparatorText(text);
}

}  // namespace

void Toggle(bool by_pad) {
    bool open = !g_open.load();
    g_by_pad = open && by_pad;
    g_open = open;
    if (open) g_focus = true;
    pc_input::Refresh();
    LOGR("overlay: %s", open ? "open" : "closed");
}

void Draw() {
    ImGuiIO& io = ImGui::GetIO();
    // a controller alone: the mouse, held at the window's centre, would hover there and fight the d-pad
    if (g_open.load() && g_by_pad.load()) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    else io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    if (!g_open.load()) return;
    app::Settings& s = app::Get();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    if (g_focus.exchange(false)) ImGui::SetNextWindowFocus();
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    if (!ImGui::Begin("Options", nullptr, flags)) {
        ImGui::End();
        return;
    }
    ImGui::SetWindowFontScale(1.4f);
    ImGui::PushItemWidth(260.0f);

    Heading("Display");
    int mode = int(s.display_mode);
    if (ImGui::Combo("Display mode", &mode, kModes, 3)) {
        s.display_mode = app::DisplayMode(mode);
        app::Save();
        app::ApplyDisplay();
    }
    int aa = 0;
    for (int i = 0; i < 5; i++)
        if (s.antialiasing == kAaSamples[i]) aa = i;
    if (ImGui::Combo("Antialiasing (SSAA)", &aa, kAa, 5)) {
        s.antialiasing = kAaSamples[aa];
        app::Save();
    }
    if (ImGui::Checkbox("VSync", &s.vsync)) app::Save();
    if (ImGui::Checkbox("60 FPS in game", &g_sixty_fps)) app::Save();

    Heading("Color");
    // the frame as shown, menus and HUD too (the runtime's present)
    if (ImGui::SliderFloat("Gamma", &s.gamma, 0.5f, 2.0f, "%.2f")) s.gamma = std::clamp(s.gamma, 0.5f, 2.0f);
    SaveAfter();
    if (ImGui::SliderFloat("Contrast", &s.contrast, 0.5f, 2.0f, "%.2f"))
        s.contrast = std::clamp(s.contrast, 0.5f, 2.0f);
    SaveAfter();
    if (ImGui::SliderFloat("Saturation", &s.saturation, 0.0f, 2.0f, "%.2f"))
        s.saturation = std::clamp(s.saturation, 0.0f, 2.0f);
    SaveAfter();
    if (ImGui::Button("Default colors")) {
        const app::Settings defaults;
        s.gamma = defaults.gamma;
        s.contrast = defaults.contrast;
        s.saturation = defaults.saturation;
        app::Save();
    }

    Heading("Image");
    if (ImGui::Checkbox("Sharpening", &sharpen::g_on)) app::Save();
    float sharpness = sharpen::Strength();
    ImGui::BeginDisabled(!sharpen::g_on);
    if (ImGui::SliderFloat("Sharpness", &sharpness, 0.0f, 1.0f, "%.2f")) sharpen::SetStrength(sharpness);
    SaveAfter();
    ImGui::EndDisabled();
    if (ImGui::Checkbox("Ambient occlusion (SSAO)", &ssao::g_on)) app::Save();
    float strength = ssao::Strength();
    ImGui::BeginDisabled(!ssao::g_on);
    if (ImGui::SliderFloat("SSAO strength", &strength, 0.0f, 1.0f, "%.2f")) ssao::SetStrength(strength);
    SaveAfter();
    ImGui::EndDisabled();

    Heading("Mouse");
    int sensitivity = int(pc_input::g_mouse_sensitivity);
    if (ImGui::SliderInt("Sensitivity", &sensitivity, 10, 400, "%d%%"))
        pc_input::g_mouse_sensitivity = uint32_t(sensitivity);
    SaveAfter();
    if (ImGui::Checkbox("Invert Y", &pc_input::g_invert_mouse_y)) app::Save();

    ImGui::PopItemWidth();
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Close")) Toggle();
    ImGui::SameLine();
    ImGui::TextDisabled("F1, Esc or L3 + R3 to close");
    ImGui::End();
}

}  // namespace overlay
