// Reload and use on their own keys. The controller's A drives two actions in game (bound to it in the bindings at
// 0x374440): 0x12 and 0xD1. The player control (sub_00120C70) asks for each (sub_000C66D0: port, action, two more, the
// action's value). 0x12 goes to sub_001171C0, which uses what is in reach (sub_00115990: a pickup, a mounted gun)
// or leaves the mounted gun the player is on (the player's +0x1C0 bit 13), else reloads; 0xD1, asked only when
// something is in reach, to sub_000EE730. With a key bound to one alone (common/pc_input.h AButton): reload's 0x12
// reloads even with something in reach and is no on a mounted gun, and 0xD1 is no; use's 0x12 is no unless something
// is in reach or the player is on a mounted gun, so it never reloads.
//
// The same check each frame tells the input the player is in play, and whether the player looks through a scope, for
// the mouse wheel (common/pc_input.h SetPlay): the scope's zoom (sub_0011B190 in, sub_0011B340 out, on the d-pad's
// up and down, stepping the field of view at the player's +0x138 down or up) works while the player's +0x1C0 has bit 25 and not bit 30 and its weapon holder (+0x1CC) is in mode 3
// (+0x1278) with its scope (+0x1280).
#include "common/pc_input.h"
#include "xdk/hle.h"

namespace {

// the guest return addresses of the player control's asks for 0x12 and 0xD1, its own check for something in reach
// (each frame, after the ask for 0x12) and 0x12's
constexpr uint32_t kAskReload = 0x121004, kAskUse = 0x12109E, kControlCheck = 0x12107F, kReloadCheck = 0x1171C8;

// the game's thread only: the player, and whether the player control's last check found something in reach
uint32_t g_player = 0;
bool g_in_reach = false;

bool Mounted(uint8_t* base) {
    return g_player && (*reinterpret_cast<uint32_t*>(base + g_player + 0x1C0) >> 13 & 1);
}

}  // namespace

// stdcall (port, action, two more) on the input manager
X86_EXTERN_FUNC(__imp__sub_000C66D0);
X86_FUNC(sub_000C66D0) {
    uint32_t from = *reinterpret_cast<uint32_t*>(base + ctx.esp);
    __imp__sub_000C66D0(ctx, base);
    pc_input::AOnly only = pc_input::AButton();
    if (from == kAskReload && only == pc_input::AOnly::Use && !g_in_reach && !Mounted(base)) ctx.eax = 0;
    if (from == kAskReload && only == pc_input::AOnly::Reload && Mounted(base)) ctx.eax = 0;
    if (from == kAskUse && only == pc_input::AOnly::Reload) ctx.eax = 0;
}

// on the player: what usable thing is in reach, if any
X86_EXTERN_FUNC(__imp__sub_00115990);
X86_FUNC(sub_00115990) {
    uint32_t from = *reinterpret_cast<uint32_t*>(base + ctx.esp), self = ctx.ecx;
    __imp__sub_00115990(ctx, base);
    if (from == kControlCheck) {
        g_player = self;
        g_in_reach = ctx.eax != 0;
        uint32_t flags = *reinterpret_cast<uint32_t*>(base + self + 0x1C0);
        uint32_t holder = *reinterpret_cast<uint32_t*>(base + self + 0x1CC);
        bool scoped = (flags >> 25 & 1) && !(flags >> 30 & 1) && holder &&
                      *reinterpret_cast<uint32_t*>(base + holder + 0x1278) == 3 && *(base + holder + 0x1280);
        pc_input::SetPlay(scoped);
    }
    if (from == kReloadCheck && pc_input::AButton() == pc_input::AOnly::Reload) ctx.eax = 0;
}
