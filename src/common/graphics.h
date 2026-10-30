#pragma once
// The graphics presets the overlay offers: Original (the game as the console drew it: no antialiasing, filtering as
// the game asks, the colour as drawn, no sharpening or ambient occlusion), Enhanced (this port's defaults) and Custom,
// whatever else the settings are. The last custom settings are kept, in the settings file's [graphics_custom], so
// choosing Custom after a preset brings them back.
#include <cstdint>

namespace graphics {

enum class Preset { Original, Enhanced, Custom };

// what the settings are now: a preset's, or Custom
Preset Current();
// sets a preset's settings; Custom sets the kept custom ones, none kept leaves them
void Choose(Preset p);
// custom settings kept to choose again
bool HasCustom();
// the settings are custom: kept as the custom ones, when they changed (true then)
bool KeepCustom();
// Enhanced's settings, the defaults
void Reset();

// settings file: [graphics_custom]
void Load(const void* table);
void Save(void* table);

}  // namespace graphics
