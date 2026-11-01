// Move while aiming (the settings file's aim_move, off by default). Aiming down the sights (sub_001150B0, the left
// trigger past 0x20 on the player) sets the player's aiming bit (+0x1C0 bit 25) and its lean bit (bit 18). Each frame
// the player's move (sub_0011AC60, stdcall (strafe, forward, up) on the player, from its vtable's +0x104) takes the
// left stick as a lean while the lean bit is set: the strafe as the lean amount (+0x430, +0x434, the vtable's +0x150),
// and the strafe and forward zeroed, so the player stands still. Here the lean bit is off for the move, which walks and
// strafes instead, at half the stick (the game's own factor for the lean, 0x376188), as aiming in modern games slows.
#include "common/options.h"
#include "xdk/hle.h"

bool g_aim_move = false;

namespace {

constexpr uint32_t kLean = 1u << 18, kAiming = 1u << 25;
constexpr float kAimSpeed = 0.5f;

}  // namespace

X86_EXTERN_FUNC(__imp__sub_0011AC60);
X86_FUNC(sub_0011AC60) {
    uint32_t& flags = *reinterpret_cast<uint32_t*>(base + ctx.ecx + 0x1C0);
    if (!g_aim_move || !(flags & kLean)) {
        __imp__sub_0011AC60(ctx, base);
        return;
    }
    float* stick = reinterpret_cast<float*>(base + ctx.esp + 4);
    stick[0] *= kAimSpeed;
    stick[1] *= kAimSpeed;
    uint32_t player = ctx.ecx;
    flags &= ~kLean;
    __imp__sub_0011AC60(ctx, base);
    // the player's flags as the move left them, the lean bit back while it still aims
    uint32_t& after = *reinterpret_cast<uint32_t*>(base + player + 0x1C0);
    if (after & kAiming) after |= kLean;
}
