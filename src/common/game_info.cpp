#include "app/app.h"

namespace {

// The Xbox controller's motors take 8 bits each, the high byte of XINPUT_RUMBLE's words. The game sets the left motor
// to (strength << 8) + 0xFF (0xC5AD5), so 0x00FF is its motor off, which a PC controller plays as a faint buzz that never
// stops. Only the high byte counts, spread back over the full range.
void Rumble(int, uint16_t& left, uint16_t& right) {
    left = uint16_t((left >> 8) * 0x101);
    right = uint16_t((right >> 8) * 0x101);
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
    .options = {},                                // game settings kept in the settings file
    .load = nullptr,                              // reads more from the settings file's toml::table
    .save = nullptr,                              // writes more into it
    .init = nullptr,                              // after the settings load, before the window and boot
    .loaded = nullptr,                            // image in memory, before its entry point runs
    .window = nullptr,                            // the game window is up
    .message = nullptr,                           // window messages first; true takes one
    .ui = nullptr,                                // ImGui drawing each frame
    .pad = nullptr,                               // changes a polled pad
    .rumble = Rumble,                             // changes a rumble before it is set
};
