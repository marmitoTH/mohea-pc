// The keys the menus take on their own (common/pc_input.h MenuKeys): the move keys as the d-pad and crouch's as X,
// given to the menus alone, so in play the game never sees them as the d-pad (the d-pad's up there is the grenade).
// The menus (the front end, the pause menu, the dialogs: EA's Apt movies, game/helpbar.cpp) take a controller's
// buttons as key events, sent for what changed (sub_001789F0, each frame for each port), from sub_001779D0 (stdcall
// (port, released, pressed), on the menus): the port's buttons as the menus' bits (the d-pad's up 0x1, down 0x2, left
// 0x4, right 0x8, A 0x10, X 0x20, Y 0x40, B 0x80 and so on), kept at 0x51FB88 + port * 4, and those released and
// pressed since the last time. Here port 0's have the keys' bits too.
#include "common/pc_input.h"
#include "xdk/hle.h"

namespace {

constexpr uint32_t kButtons = 0x51FB88;  // each port's bits, as of the last time
constexpr uint32_t kA = 0x10;

uint32_t& U32(uint8_t* base, uint32_t a) { return *reinterpret_cast<uint32_t*>(base + a); }

}  // namespace

X86_EXTERN_FUNC(__imp__sub_001779D0);
X86_FUNC(sub_001779D0) {
    uint32_t self = ctx.ecx, port = U32(base, ctx.esp + 4);
    uint32_t released = U32(base, ctx.esp + 8), pressed = U32(base, ctx.esp + 12);
    uint32_t before = port < 4 ? U32(base, kButtons + 4 * port) : 0;
    __imp__sub_001779D0(ctx, base);
    if (port != 0) return;
    uint32_t now = U32(base, kButtons) | pc_input::MenuKeys();
    U32(base, released) = before & ~now;
    U32(base, pressed) = now & ~before;
    U32(base, kButtons) = now;
    // A pressed makes its port the menus' (as the game does)
    if (U32(base, pressed) & kA) U32(base, self + 0x18) = port;
}
