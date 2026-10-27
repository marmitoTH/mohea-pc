#include "app/app.h"

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
    .rumble = nullptr,                            // changes a rumble before it is set
};
