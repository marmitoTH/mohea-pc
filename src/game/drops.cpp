// How long the things a fallen enemy drops stay (the settings file's drop_seconds, 23 by default as the game has it,
// and drops_forever). An object fades out on a timer (its +0x16C, the timer at +0x84): sub_000B1AB0 (on the timer,
// stdcall (seconds, share)) starts it, the object showing until the share of the seconds has passed, then fading until
// they all have, when it goes. A soldier's death (sub_000FB3xx) drops from its drop table (sub_000F9B90: ammo, a
// medkit, grenades), or its weapon when that dropped nothing; either gets 23 seconds and a share of 0.8696, 20 shown
// and 3 fading, through sub_000FC680 on the object and its vtable's +0x188 on the object at its +4 (sub_00139540),
// both on to sub_000B1AB0. Here those four calls, by their return addresses, get the setting's seconds with the same 3
// of fading, or seconds no game reaches.
#include <algorithm>
#include <iterator>

#include "common/options.h"
#include "core/log.h"
#include "xdk/hle.h"

uint32_t g_drop_seconds = 23;
bool g_drops_forever = false;

uint32_t DropSeconds() { return std::clamp(g_drop_seconds, 23u, 600u); }

namespace {

// the guest return addresses of the drops' timers: the drop table's item and the object at its +4, the weapon's and
// its object's
constexpr uint32_t kDrops[] = {0xF9CD4, 0xF9CE9, 0xFB6CF, 0xFB6E8};

constexpr float kGame = 23.0f, kFade = 3.0f, kForever = 1e9f;

}  // namespace

X86_EXTERN_FUNC(__imp__sub_000B1AB0);
X86_FUNC(sub_000B1AB0) {
    float& seconds = *reinterpret_cast<float*>(base + ctx.esp + 4);
    float& share = *reinterpret_cast<float*>(base + ctx.esp + 8);
    uint32_t from = *reinterpret_cast<uint32_t*>(base + ctx.esp);
    if ((g_drops_forever || DropSeconds() != 23) && seconds == kGame &&
        std::find(std::begin(kDrops), std::end(kDrops), from) != std::end(kDrops)) {
        seconds = g_drops_forever ? kForever : float(DropSeconds());
        share = g_drops_forever ? 1.0f : (seconds - kFade) / seconds;
        LOG_FIRST(4, "drops: a drop's timer (from %08X) set to %.0f seconds", from, seconds);
    }
    __imp__sub_000B1AB0(ctx, base);
}
