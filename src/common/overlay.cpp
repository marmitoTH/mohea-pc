// See overlay.h.
#include "common/overlay.h"

#include <algorithm>

#include <imgui.h>

#include "app/app.h"
#include "common/options.h"
#include "common/pc_input.h"
#include "common/sharpen.h"
#include "common/smaa.h"
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

// the antialiasing, one of: SMAA's presets (smaa::g_preset), the supersampling's samples (app::Settings's), or both
constexpr const char* kAntialiasing[] = {"Off",      "SMAA Low", "SMAA Medium", "SMAA High", "SMAA Ultra",
                                         "SSAA 2x",  "SSAA 4x",  "SSAA 8x",     "SSAA 16x", "SSAA 2x + SMAA High"};
constexpr int kSmaaFirst = 1, kSsaaFirst = 5, kBoth = 9;
constexpr uint32_t kBothSamples = 2, kBothPreset = 3;

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
    // the supersampling's entry, SMAA's, or the two together (any pair set by hand shows as that)
    int aa = 0;
    for (int i = 1; i < 5; i++)
        if (s.antialiasing == kAaSamples[i]) aa = kSsaaFirst + i - 1;
    if (smaa::Preset()) aa = aa ? kBoth : kSmaaFirst + int(smaa::Preset()) - 1;
    if (ImGui::Combo("Antialiasing", &aa, kAntialiasing, IM_ARRAYSIZE(kAntialiasing))) {
        if (aa == kBoth) {
            s.antialiasing = kBothSamples;
            smaa::g_preset = kBothPreset;
        } else {
            s.antialiasing = aa >= kSsaaFirst ? kAaSamples[aa - kSsaaFirst + 1] : 0;
            smaa::g_preset = aa >= kSmaaFirst && aa < kSsaaFirst ? uint32_t(aa - kSmaaFirst + 1) : 0;
        }
        app::Save();
    }
    int af = 0;
    for (int i = 0; i < 5; i++)
        if (s.aniso == kAaSamples[i]) af = i;
    // the same steps as the antialiasing's: off (as the game asks), 2x, 4x, 8x, 16x
    if (ImGui::Combo("Anisotropic filtering", &af, kAa, 5)) {
        s.aniso = kAaSamples[af];
        app::Save();
    }
    if (ImGui::Checkbox("VSync", &s.vsync)) app::Save();
    if (ImGui::Checkbox("60 FPS in game", &g_sixty_fps)) app::Save();
    int fov = int(g_fov);
    if (ImGui::SliderInt("Field of view", &fov, 60, 110, "%d degrees")) g_fov = uint32_t(fov);
    SaveAfter();
    ImGui::SameLine();
    if (ImGui::Button("Default##fov")) {
        g_fov = 70;  // the game's
        app::Save();
    }
    if (ImGui::Checkbox("Hide HUD", &g_hide_hud)) app::Save();

    Heading("Gameplay");
    // what a fallen enemy drops: ammo, medkits, grenades, its weapon
    if (ImGui::Checkbox("Drops never disappear", &g_drops_forever)) app::Save();
    ImGui::BeginDisabled(g_drops_forever);
    int drop = int(DropSeconds());
    if (ImGui::SliderInt("Drops disappear after", &drop, 23, 600, "%d seconds")) g_drop_seconds = uint32_t(drop);
    SaveAfter();
    ImGui::SameLine();
    if (ImGui::Button("Default##drops")) {
        g_drop_seconds = 60;
        app::Save();
    }
    ImGui::EndDisabled();
    if (ImGui::Checkbox("Squad takes no hits", &g_squad_immortal)) app::Save();
    if (ImGui::Checkbox("Player takes no damage", &g_player_invincible)) app::Save();
    if (ImGui::Checkbox("Unlimited ammo", &g_unlimited_ammo)) app::Save();
    if (ImGui::Checkbox("No shell shock", &g_no_shell_shock)) app::Save();

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
