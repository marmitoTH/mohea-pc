// See overlay.h.
#include "common/overlay.h"

#include <algorithm>

#include <imgui.h>
#include <imgui_internal.h>

#include "app/app.h"
#include "common/graphics.h"
#include "common/marker.h"
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

constexpr const char* kPresets[] = {"Original", "Enhanced", "Custom"};  // graphics::Preset's order

// the defaults of the settings past the graphics presets (common/graphics.h), as they are before the settings file
// loads: at the program's start, before main
struct Defaults {
    app::Settings app;
    bool sixty_fps = g_sixty_fps;
    uint32_t fov = g_fov;
    bool hide_hud = g_hide_hud, hide_marker = marker::g_hide;
    uint32_t drop_seconds = g_drop_seconds;
    bool drops_forever = g_drops_forever, squad_immortal = g_squad_immortal;
    bool player_invincible = g_player_invincible, unlimited_ammo = g_unlimited_ammo, no_shell_shock = g_no_shell_shock;
    uint32_t mouse_sensitivity = pc_input::g_mouse_sensitivity;
    bool invert_mouse_y = pc_input::g_invert_mouse_y;
};
const Defaults kDefaults;

// what a reset sets back: a tab's settings, or all of them
enum Part : int { kDisplay = 1, kGraphics = 2, kHacks = 4, kControls = 8, kAll = 15 };

void Reset(int part) {
    const Defaults& d = kDefaults;
    app::Settings& s = app::Get();
    if (part & kDisplay) {
        bool mode = s.display_mode != d.app.display_mode;
        s.display_mode = d.app.display_mode;
        s.vsync = d.app.vsync;
        g_sixty_fps = d.sixty_fps;
        g_fov = d.fov;
        g_hide_hud = d.hide_hud;
        marker::g_hide = d.hide_marker;
        if (mode) app::ApplyDisplay();
    }
    if (part & kGraphics) graphics::Reset();
    if (part & kHacks) {
        g_drop_seconds = d.drop_seconds;
        g_drops_forever = d.drops_forever;
        g_squad_immortal = d.squad_immortal;
        g_player_invincible = d.player_invincible;
        g_unlimited_ammo = d.unlimited_ammo;
        g_no_shell_shock = d.no_shell_shock;
    }
    if (part & kControls) {
        pc_input::g_mouse_sensitivity = d.mouse_sensitivity;
        pc_input::g_invert_mouse_y = d.invert_mouse_y;
    }
    app::Save();
    LOGR("overlay: reset %s", part == kAll ? "everything" : "a tab");
}

// the reset waiting for its confirmation
int g_reset = 0;
const char* g_reset_what = "";

// a slider saves once let go, not at each step of a drag
void SaveAfter() {
    if (ImGui::IsItemDeactivatedAfterEdit()) app::Save();
}

void Heading(const char* text) {
    ImGui::Spacing();
    ImGui::SeparatorText(text);
}

// a tab's reset button, at its end
void ResetButton(int part, const char* what) {
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Reset tab")) {
        g_reset = part;
        g_reset_what = what;
    }
}

void DisplayTab() {
    app::Settings& s = app::Get();
    Heading("Screen");
    int mode = int(s.display_mode);
    if (ImGui::Combo("Display mode", &mode, kModes, 3)) {
        s.display_mode = app::DisplayMode(mode);
        app::Save();
        app::ApplyDisplay();
    }
    if (ImGui::Checkbox("VSync", &s.vsync)) app::Save();
    if (ImGui::Checkbox("60 FPS in game", &g_sixty_fps)) app::Save();
    int fov = int(g_fov);
    if (ImGui::SliderInt("Field of view", &fov, 60, 110, "%d degrees")) g_fov = uint32_t(fov);
    SaveAfter();
    ImGui::SameLine();
    if (ImGui::Button("Default##fov")) {
        g_fov = kDefaults.fov;
        app::Save();
    }

    Heading("HUD");
    if (ImGui::Checkbox("Hide HUD", &g_hide_hud)) app::Save();
    // the star over each squad member, its health too
    if (ImGui::Checkbox("Hide squad markers", &marker::g_hide)) app::Save();
    ResetButton(kDisplay, "the display settings");
}

void GraphicsTab() {
    app::Settings& s = app::Get();
    // the preset the settings make; Custom brings back the last custom settings
    int preset = int(graphics::Current());
    if (ImGui::BeginCombo("Preset", kPresets[preset])) {
        for (int i = 0; i < IM_ARRAYSIZE(kPresets); i++) {
            bool custom = i == int(graphics::Preset::Custom);
            ImGui::BeginDisabled(custom && !graphics::HasCustom());
            if (ImGui::Selectable(kPresets[i], i == preset) && i != preset) graphics::Choose(graphics::Preset(i));
            ImGui::EndDisabled();
            if (i == preset) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    Heading("Image");
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
    // off (as the game asks: one level textures sharp, as the console drew them), 2x, 4x, 8x, 16x (with mip chains
    // made for those, sdk_patches/0008)
    if (ImGui::Combo("Anisotropic filtering", &af, kAa, 5)) {
        s.aniso = kAaSamples[af];
        app::Save();
    }

    // how far characters and objects keep their detail, and are drawn at all
    int lod = int(LodDistance());
    if (ImGui::SliderInt("Detail distance", &lod, 100, 400, "%d%%")) g_lod_distance = uint32_t(lod);
    SaveAfter();

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

    Heading("Post-processing");
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

    // custom settings kept as they are made, once no slider is held
    if (!ImGui::IsAnyItemActive() && graphics::KeepCustom()) app::Save();
    ResetButton(kGraphics, "the graphics settings (the Enhanced preset)");
}

void HacksTab() {
    Heading("Drops");
    // what a fallen enemy drops: ammo, medkits, grenades, its weapon
    if (ImGui::Checkbox("Drops never disappear", &g_drops_forever)) app::Save();
    ImGui::BeginDisabled(g_drops_forever);
    int drop = int(DropSeconds());
    if (ImGui::SliderInt("Drops disappear after", &drop, 23, 600, "%d seconds")) g_drop_seconds = uint32_t(drop);
    SaveAfter();
    ImGui::SameLine();
    if (ImGui::Button("Default##drops")) {
        g_drop_seconds = kDefaults.drop_seconds;
        app::Save();
    }
    ImGui::EndDisabled();

    Heading("Cheats");
    if (ImGui::Checkbox("Squad takes no hits", &g_squad_immortal)) app::Save();
    if (ImGui::Checkbox("Player takes no damage", &g_player_invincible)) app::Save();
    if (ImGui::Checkbox("Unlimited ammo", &g_unlimited_ammo)) app::Save();
    if (ImGui::Checkbox("No shell shock", &g_no_shell_shock)) app::Save();
    ResetButton(kHacks, "the hacks");
}

void ControlsTab() {
    Heading("Mouse");
    int sensitivity = int(pc_input::g_mouse_sensitivity);
    if (ImGui::SliderInt("Sensitivity", &sensitivity, 10, 400, "%d%%"))
        pc_input::g_mouse_sensitivity = uint32_t(sensitivity);
    SaveAfter();
    if (ImGui::Checkbox("Invert Y", &pc_input::g_invert_mouse_y)) app::Save();
    ImGui::TextDisabled("Keys are bound in the settings file's [keys]");
    ResetButton(kControls, "the mouse settings");
}

// the reset asked for, once confirmed
void ConfirmReset() {
    constexpr const char* kTitle = "Reset to defaults?";
    if (g_reset && !ImGui::IsPopupOpen(kTitle)) ImGui::OpenPopup(kTitle);
    ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::SetWindowFontScale(1.4f);
    ImGui::Text("Set %s back to the defaults?", g_reset_what);
    ImGui::Spacing();
    if (ImGui::Button("Reset")) {
        Reset(g_reset);
        g_reset = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    bool back = ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight);
    if (ImGui::Button("Cancel") || back) {
        g_reset = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::EndPopup();
}

// the tabs, in order; the one shown last, and whether a popup (a dropdown, the confirmation) was open then
constexpr int kTabs = 4;
constexpr const char* kTabNames[kTabs] = {"Display", "Graphics", "Hacks", "Controls"};
void (*const kTabDraws[kTabs])() = {DisplayTab, GraphicsTab, HacksTab, ControlsTab};
int g_tab = 0;
bool g_popup = false;

// The tabs' region: each tab drawn once off the screen when the overlay first opens (a bit a tab), the biggest kept
// for all of them; then the tab shown before comes back (g_back_tab)
uint32_t g_measured = 0;
ImVec2 g_tabs{0.0f, 0.0f};
int g_back_tab = -1;

// the game's red on black, for the overlay alone (pushed around its window): a near black window, dark red frames,
// tabs and buttons, brighter red where hovered or held, the brightest for checks, grabs and the controller's focus
struct ThemeColor {
    ImGuiCol col;
    ImVec4 rgba;
};
constexpr ImVec4 Red(float r, float g, float b, float a = 1.0f) { return {r, g, b, a}; }
constexpr ThemeColor kTheme[] = {
    {ImGuiCol_WindowBg, Red(0.05f, 0.02f, 0.02f)},
    {ImGuiCol_PopupBg, Red(0.07f, 0.02f, 0.02f, 0.98f)},
    {ImGuiCol_Border, Red(0.45f, 0.07f, 0.06f, 0.80f)},
    {ImGuiCol_TitleBg, Red(0.30f, 0.04f, 0.04f)},
    {ImGuiCol_TitleBgActive, Red(0.50f, 0.06f, 0.05f)},
    {ImGuiCol_FrameBg, Red(0.20f, 0.03f, 0.03f)},
    {ImGuiCol_FrameBgHovered, Red(0.38f, 0.06f, 0.05f)},
    {ImGuiCol_FrameBgActive, Red(0.52f, 0.08f, 0.06f)},
    {ImGuiCol_CheckMark, Red(0.95f, 0.22f, 0.16f)},
    {ImGuiCol_SliderGrab, Red(0.75f, 0.12f, 0.09f)},
    {ImGuiCol_SliderGrabActive, Red(0.95f, 0.22f, 0.16f)},
    {ImGuiCol_Button, Red(0.42f, 0.06f, 0.05f)},
    {ImGuiCol_ButtonHovered, Red(0.68f, 0.10f, 0.08f)},
    {ImGuiCol_ButtonActive, Red(0.85f, 0.15f, 0.11f)},
    {ImGuiCol_Header, Red(0.42f, 0.06f, 0.05f)},
    {ImGuiCol_HeaderHovered, Red(0.68f, 0.10f, 0.08f)},
    {ImGuiCol_HeaderActive, Red(0.85f, 0.15f, 0.11f)},
    {ImGuiCol_Separator, Red(0.45f, 0.07f, 0.06f)},
    {ImGuiCol_SeparatorHovered, Red(0.68f, 0.10f, 0.08f)},
    {ImGuiCol_SeparatorActive, Red(0.85f, 0.15f, 0.11f)},
    {ImGuiCol_Tab, Red(0.28f, 0.04f, 0.04f)},
    {ImGuiCol_TabHovered, Red(0.72f, 0.10f, 0.08f)},
    {ImGuiCol_TabSelected, Red(0.55f, 0.06f, 0.05f)},
    {ImGuiCol_TabSelectedOverline, Red(0.95f, 0.18f, 0.12f)},
    {ImGuiCol_TabDimmed, Red(0.18f, 0.03f, 0.03f)},
    {ImGuiCol_TabDimmedSelected, Red(0.40f, 0.05f, 0.04f)},
    {ImGuiCol_TabDimmedSelectedOverline, Red(0.70f, 0.12f, 0.10f)},
    {ImGuiCol_ScrollbarGrab, Red(0.42f, 0.06f, 0.05f)},
    {ImGuiCol_ScrollbarGrabHovered, Red(0.68f, 0.10f, 0.08f)},
    {ImGuiCol_ScrollbarGrabActive, Red(0.85f, 0.15f, 0.11f)},
    {ImGuiCol_TextSelectedBg, Red(0.60f, 0.08f, 0.06f, 0.50f)},
    {ImGuiCol_NavHighlight, Red(0.95f, 0.22f, 0.16f)},
    {ImGuiCol_ModalWindowDimBg, Red(0.0f, 0.0f, 0.0f, 0.55f)},
};

}  // namespace

void Toggle(bool by_pad) {
    bool open = !g_open.load();
    g_by_pad = open && by_pad;
    g_open = open;
    if (open) g_focus = true;
    else g_reset = 0;  // closed (Esc) with a reset unconfirmed: none
    g_popup = false;
    pc_input::Refresh();
    LOGR("overlay: %s", open ? "open" : "closed");
}

void Draw() {
    ImGuiIO& io = ImGui::GetIO();
    // a controller alone: the mouse, held at the window's centre, would hover there and fight the d-pad
    if (g_open.load() && g_by_pad.load()) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    else io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    if (!g_open.load()) return;
    // a controller's B closes it and LB and RB step through the tabs, unless a dropdown or the confirmation was open
    // (B closes that, ImGui's own; a frame late, as ImGui has acted on B before this)
    int select = -1;
    bool focus_tab = false;
    bool measuring = g_measured != (1u << kTabs) - 1;
    if (measuring) {
        // each tab not seen yet shown once off the screen, then the one that was
        for (int i = 0; i < kTabs; i++)
            if (!(g_measured & (1u << i)) && i != g_tab) select = i;
    } else if (g_back_tab >= 0) {
        select = g_back_tab;
        g_back_tab = -1;
    } else if (!g_popup && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
            Toggle();
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) select = (g_tab + 1) % kTabs;
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) select = (g_tab + kTabs - 1) % kTabs;
        focus_tab = select >= 0;
    }
    if (measuring) {
        if (!g_measured) g_back_tab = g_tab;
        ImGui::SetNextWindowPos(ImVec2(-10000.0f, -10000.0f), ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
    }
    ImGui::SetNextWindowBgAlpha(0.92f);
    if (g_focus.exchange(false)) ImGui::SetNextWindowFocus();
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    for (const ThemeColor& c : kTheme) ImGui::PushStyleColor(c.col, c.rgba);
    if (!ImGui::Begin("Options", nullptr, flags)) {
        ImGui::End();
        ImGui::PopStyleColor(IM_ARRAYSIZE(kTheme));
        return;
    }
    ImGui::SetWindowFontScale(1.4f);

    // the tabs in a region as big as the biggest tab, so the tabs and the footer under it stay where they are; drawn
    // as they come while measuring
    ImVec2 tabs_top = ImGui::GetCursorPos();
    if (!measuring) {
        ImGui::BeginChild("tab", g_tabs, ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                              ImGuiWindowFlags_NoBackground);  // the window's font scale, a child's times its own
    }
    ImGui::PushItemWidth(260.0f);
    if (ImGui::BeginTabBar("tabs")) {
        for (int i = 0; i < kTabs; i++) {
            bool shown = ImGui::BeginTabItem(kTabNames[i], nullptr, i == select ? ImGuiTabItemFlags_SetSelected : 0);
            // stepped to with LB or RB: the controller's focus on the tab
            if (i == select && focus_tab) {
                ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
                GImGui->NavDisableHighlight = false;
            }
            if (shown) {
                g_tab = i;
                kTabDraws[i]();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::PopItemWidth();
    if (measuring) {
        // what the tab shown this frame takes
        const ImGuiWindow* w = ImGui::GetCurrentWindow();
        g_tabs.x = std::max(g_tabs.x, w->DC.CursorMaxPos.x - w->Pos.x - tabs_top.x);
        g_tabs.y = std::max(g_tabs.y, ImGui::GetCursorPosY() - tabs_top.y);
        g_measured |= 1u << g_tab;
    } else {
        ImGui::EndChild();
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Close")) Toggle();
    ImGui::SameLine();
    if (ImGui::Button("Reset all")) {
        g_reset = kAll;
        g_reset_what = "every setting here";
    }
    ImGui::SameLine();
    ImGui::TextDisabled("F1, Esc, B or L3 + R3 to close; LB and RB switch tabs");
    ConfirmReset();
    g_popup = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    ImGui::End();
    ImGui::PopStyleColor(IM_ARRAYSIZE(kTheme));
}

}  // namespace overlay
