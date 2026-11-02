// Crouch and prone on the keys as toggles with standing (common/pc_input.h TakeStance): crouch stands up from crouched
// and crouches otherwise, prone stands up from prone and goes prone otherwise. The player's stance is its +0x3F8 (0
// prone, 1 crouched, 2 standing); the game steps it one at a time, up by sub_00114000 (prone to crouched to standing,
// on the player) and down by sub_00115000 (standing to crouched to prone, when the weapon allows). Each
// frame the player control (sub_00120C70) passes the move to the player (sub_00114950, stdcall (three floats), on the
// player), and after it here the stance steps toward the one pressed for: a step once the last one has shown and held
// a moment (so going prone or standing up passes through crouched as the game's own two steps would), for a few steps
// at most (a step the game refuses leaves it as it is, and is tried again a while later).
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

}  // namespace

X86_EXTERN_FUNC(__imp__sub_00114950);
X86_FUNC(sub_00114950) {
    uint32_t player = ctx.ecx;
    __imp__sub_00114950(ctx, base);
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
