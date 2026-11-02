// The keys' own actions in play (common/pc_input.h TakeStance, TakeWeapon), each frame the player control
// (sub_00120C70) passes the move to the player (sub_00114950, stdcall (three floats), on the player), after it.
//
// Crouch and prone as toggles with standing: crouch stands up from crouched
// and crouches otherwise, prone stands up from prone and goes prone otherwise. The player's stance is its +0x3F8 (0
// prone, 1 crouched, 2 standing); the game steps it one at a time, up by sub_00114000 (prone to crouched to standing,
// on the player) and down by sub_00115000 (standing to crouched to prone, when the weapon allows). Here the stance
// steps toward the one pressed for: a step once the last one has shown and held a moment (so going prone or standing
// up passes through crouched as the game's own two steps would), for a few steps at most (a step the game refuses
// leaves it as it is, and is tried again a while later).
//
// The primary, the secondary and the grenades on their own keys. The player's weapons are its +0x1D0 (the primary),
// +0x1D4 (the secondary) and +0x1D8 (the grenades, their count the weapon data's (+0x1128) +0x1CE), the one in hand
// +0x1CC and the last of the first two +0x1E4. B (sub_0011B6C0, from the vtable's +0x80) cycles them and switches by a
// message to the player's +0x30 (its vtable's +0x48, (6, the weapon)); here a key sends that for its weapon, as B
// would: not on a mounted gun (+0x1C0 bit 13), with a weapon in hand, not the one the level holds the player to
// (+0x1E0 while 0x389414 is 11) or with a pickup waiting (+0x1F4), and to the grenades only with some left.
#include <chrono>

#include "common/pc_input.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {

constexpr uint32_t kStepUp = 0x114000, kStepDown = 0x115000;
constexpr int kNone = -1, kProne = 0, kCrouched = 1, kStanding = 2;
constexpr int kMaxSteps = 4;
using clock = std::chrono::steady_clock;
constexpr auto kHold = std::chrono::milliseconds(250);  // a step shown, held before the next
constexpr auto kRetry = std::chrono::milliseconds(1000);  // a step not shown, tried again

// the game's thread only: the stance pressed for, the stance before the last step, when it was taken, the steps taken
int g_target = kNone, g_before = kNone, g_steps = 0;
clock::time_point g_stepped;

uint32_t& U32(uint8_t* base, uint32_t a) { return *reinterpret_cast<uint32_t*>(base + a); }

void Stance(X86Context& ctx, uint8_t* base, uint32_t player) {
    int stance = int(*reinterpret_cast<uint32_t*>(base + player + 0x3F8));
    switch (pc_input::TakeStance()) {
        case pc_input::Stance::Crouch:
            g_target = stance == kCrouched ? kStanding : kCrouched;
            g_steps = 0;
            break;
        case pc_input::Stance::Prone:
            g_target = stance == kProne ? kStanding : kProne;
            g_steps = 0;
            break;
        case pc_input::Stance::None:
            if (g_target == kNone) return;
            break;
    }
    if (stance == g_target || g_steps >= kMaxSteps) {
        g_target = kNone;
        g_steps = 0;
        return;
    }
    // the last step not shown yet, or not held
    clock::time_point now = clock::now();
    if (g_steps > 0 && now - g_stepped < (stance == g_before ? kRetry : kHold)) return;
    g_before = stance;
    g_stepped = now;
    g_steps++;
    uint32_t ecx = ctx.ecx;
    ctx.ecx = player;
    kernel::CallGuest(ctx, stance < g_target ? kStepUp : kStepDown, {});
    ctx.ecx = ecx;
}

void Weapon(X86Context& ctx, uint8_t* base, uint32_t player) {
    int slot = pc_input::TakeWeapon();
    if (slot < 0) return;
    uint32_t holder = U32(base, player + 0x1CC), weapon = U32(base, player + 0x1D0 + 4 * slot);
    if ((U32(base, player + 0x1C0) >> 13 & 1) || !holder || U32(base, player + 0x1F4) || !weapon || weapon == holder)
        return;
    if (holder == U32(base, player + 0x1E0) && U32(base, 0x389414) == 0x0B) return;
    if (slot == 2 && *reinterpret_cast<int16_t*>(base + U32(base, weapon + 0x1128) + 0x1CE) <= 0) return;
    if (slot < 2) U32(base, player + 0x1E4) = weapon;
    uint32_t ecx = ctx.ecx, to = player + 0x30;
    ctx.ecx = to;
    kernel::CallGuest(ctx, U32(base, U32(base, to) + 0x48), {6, weapon});
    ctx.ecx = ecx;
}

}  // namespace

X86_EXTERN_FUNC(__imp__sub_00114950);
X86_FUNC(sub_00114950) {
    uint32_t player = ctx.ecx;
    __imp__sub_00114950(ctx, base);
    Stance(ctx, base, player);
    Weapon(ctx, base, player);
}
