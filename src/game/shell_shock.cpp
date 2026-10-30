// No shell shock (the settings file's no_shell_shock, off by default). A blast near the player (the explosion's damage,
// around 0x182E86, picks a stronger shock for a stronger blast) and a script action (sub_0003FE90) shock the player:
// sub_001147D0 (stdcall (strength) on the player) starts 3 seconds of tinted screen, sound and rumble on its +0x1420
// (sub_00129000), unless the game's cheat code byte at 0x51D6C4 is set, the player is in adrenaline (+0x13D8 2) or a
// shock is under way. Here the start is skipped, as with the cheat, the byte left alone.
#include "common/options.h"
#include "xdk/hle.h"

bool g_no_shell_shock = false;

X86_EXTERN_FUNC(__imp__sub_001147D0);
X86_FUNC(sub_001147D0) {
    if (g_no_shell_shock) {
        ctx.esp += 8;  // ret 4
        return;
    }
    __imp__sub_001147D0(ctx, base);
}
