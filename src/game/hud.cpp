// The HUD hidden (the settings file's hide_hud, off by default). The frame draws the HUD with sub_001590B0 (stdcall
// (one argument)), which draws nothing while the byte at 0x3785A5 is clear: a level's start and the pause menu's end
// copy it from bit 4 of the game's +0x12 (the bit the game's hide HUD cheat code flips, sub_0015BDF0), the pause menu
// and cutscenes clear it, and scripts set it (sub_00092FE0). Here the draw is skipped, the game's state left alone.
#include "common/options.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

bool g_hide_hud = false;

bool HudWanted() {
    constexpr uint32_t kShown = 0x3785A5;
    return kernel::g_base && kernel::g_base[kShown] != 0;
}

X86_EXTERN_FUNC(__imp__sub_001590B0);
X86_FUNC(sub_001590B0) {
    if (g_hide_hud) {
        ctx.esp += 8;  // ret 4
        return;
    }
    __imp__sub_001590B0(ctx, base);
}
