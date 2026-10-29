// The player takes no damage (the settings file's player_invincible, off by default). The player's health (+0x3B0)
// is only ever set by sub_00116770 (vtable +0x50, stdcall (health)), which clamps it to the player's maximum (vtable
// +0x4C) and to 0; a hit (sub_00114500, +0x54) adds its damage through +0x58 (sub_001144C0), which calls it with the health
// less the damage. The game's own cheat (PlayerIsInvincible, the byte at 0x37279C) only keeps it from falling under
// 5. Here a lower health than the player has is not taken; healing and the rest still are.
#include "common/options.h"
#include "core/log.h"
#include "xdk/hle.h"

bool g_player_invincible = false;

X86_EXTERN_FUNC(__imp__sub_00116770);
X86_FUNC(sub_00116770) {
    if (g_player_invincible) {
        float& health = *reinterpret_cast<float*>(base + ctx.esp + 4);
        float now = *reinterpret_cast<float*>(base + ctx.ecx + 0x3B0);
        if (health < now) {
            LOG_FIRST(4, "invincible: the player's health kept at %.1f (not %.1f)", now, health);
            health = now;
        }
    }
    __imp__sub_00116770(ctx, base);
}
