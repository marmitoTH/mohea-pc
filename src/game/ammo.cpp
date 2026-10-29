// Unlimited ammo for the player (the settings file's unlimited_ammo, off by default), as the game's own cheat has it
// (Unlimited Ammo, the byte at 0x37279E): a weapon's update (sub_0018A440, ecx the weapon) puts a shot back in the
// player's empty clip (+0x1CC), and its shot (sub_00189420, called only from there) takes none out of it, the player's
// being the weapons whose owner (+0x254) says so (vtable +0x84). Nothing else reads the byte, which the profile's cheats
// and a cheat code set. Here it is set for each weapon's update, and put back after.
#include "common/options.h"
#include "xdk/hle.h"

bool g_unlimited_ammo = false;

X86_EXTERN_FUNC(__imp__sub_0018A440);
X86_FUNC(sub_0018A440) {
    constexpr uint32_t kCheat = 0x37279E;
    uint8_t& cheat = base[kCheat];
    if (!g_unlimited_ammo || cheat) {
        __imp__sub_0018A440(ctx, base);
        return;
    }
    cheat = 1;
    __imp__sub_0018A440(ctx, base);
    cheat = 0;
}
