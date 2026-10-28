#include "app/app.h"
#include "common/display.h"
#include "common/options.h"
#include "common/pc_input.h"
#include "common/sharpen.h"

namespace {

// The Xbox controller's motors take 8 bits each, the high byte of XINPUT_RUMBLE's words. The game sets the left motor
// to (strength << 8) + 0xFF (0xC5AD5), so 0x00FF is its motor off, which a PC controller plays as a faint buzz that never
// stops. Only the high byte counts, spread back over the full range.
void Rumble(int, uint16_t& left, uint16_t& right) {
    left = uint16_t((left >> 8) * 0x101);
    right = uint16_t((right >> 8) * 0x101);
}

// the runtime only reads the settings file: written back once loaded, it shows every setting with its default,
// the key bindings among them, to change by hand; then the renderer's sharpening
void Init() {
    app::Save();
    sharpen::Install();
}

// the settings file's tables and values past the options
void LoadSettings(const void* table) {
    pc_input::Load(table);
    sharpen::Load(table);
}

void SaveSettings(void* table) {
    pc_input::Save(table);
    sharpen::Save(table);
}

// the full screen keys first, then the keyboard and mouse
bool Message(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    return display::Message(hwnd, msg, wp, lp, result) || pc_input::Message(hwnd, msg, wp, lp, result);
}

}  // namespace

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"Medal of Honor European Assault",  // window and splash
    .short_title = L"MOH European Assault",       // message boxes
    .name = L"MOHEA",                             // window classes, crash dumps, handover event
    .log = "mohea",                               // log file prefix
    .settings = "mohea.toml",                     // settings file beside the exe
    .splash = L"splash",                          // optional splash.png or splash.bmp beside the exe
    .missing = L"Put Medal of Honor European Assault (USA) in the assets folder: its disc image (.iso), or default.xbe "
               L"with its DATA folder and EAGLRM.O beside it.",
    .images = {{"default.xbe", "MOHEA"}},         // each recompiled xbe and its exe, for title launches
    .options = {{"mouse_sensitivity", nullptr, &pc_input::g_mouse_sensitivity},  // game settings kept in the
                {"invert_mouse_y", &pc_input::g_invert_mouse_y, nullptr},       // settings file
                {"sixty_fps", &g_sixty_fps, nullptr},
                {"sharpening", &sharpen::g_on, nullptr}},
    .load = LoadSettings,                         // reads more from the settings file's toml::table
    .save = SaveSettings,                         // writes more into it
    .init = Init,                                 // after the settings load, before the window and boot
    .loaded = nullptr,                            // image in memory, before its entry point runs
    .window = pc_input::Window,                   // the game window is up
    .message = Message,                           // window messages first; true takes one
    .ui = [](double) { display::Ui(); },          // ImGui drawing each frame
    .pad = pc_input::Pad,                         // changes a polled pad
    .rumble = Rumble,                             // changes a rumble before it is set
};
