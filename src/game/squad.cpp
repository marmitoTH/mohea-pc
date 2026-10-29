// The player's squad takes no hits (the settings file's squad_immortal, off by default): the soldiers the player gives
// orders to, no other.
//
// A soldier (vtable 0x317008) takes a hit as a message, 0xD0D20092, which its handler (sub_000F9F70) passes to its
// character (the soldier's +0x2760, vtable 0x2FF2F0) and on to the character's health (its +0x500, through vtable
// +0x3C): sub_00016EA0 (on the health, stdcall (message, character)) takes the hit's damage (message +0x30) off the
// health (+0x9C of +0x98; +0x88 0 to 3, 3 dead) and plays the soldier's reaction. The squad the player orders is at
// [0x375FE0] (the player control's squad commands, sub_00124BF0): a list of its members at +0x54, as many as +0x5C,
// each a record whose +0 is a member's character. Here a hit on one of them is dropped before it lands. A script
// setting a soldier's health is another message (0x5156CE44), left as it is.
#include "common/options.h"
#include "core/log.h"
#include "xdk/hle.h"

bool g_squad_immortal = false;

namespace {

constexpr uint32_t kSquad = 0x375FE0;

uint32_t& At(uint8_t* base, uint32_t address) {
    return *reinterpret_cast<uint32_t*>(base + address);
}

// the character is in the squad the player orders
bool InSquad(uint8_t* base, uint32_t character) {
    uint32_t squad = At(base, kSquad);
    if (!squad || !character) return false;
    uint32_t members = At(base, squad + 0x54), count = At(base, squad + 0x5C);
    if (!members || count > 64) return false;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t record = At(base, members + 4 * i);
        if (record && At(base, record) == character) return true;
    }
    return false;
}

}  // namespace

// a hit (stdcall (message, character) on the character's health)
X86_EXTERN_FUNC(__imp__sub_00016EA0);
X86_FUNC(sub_00016EA0) {
    if (g_squad_immortal && InSquad(base, At(base, ctx.esp + 8))) {
        LOG_FIRST(4, "squad: a hit on a squad member (character %08X) dropped", At(base, ctx.esp + 8));
        ctx.esp += 12;  // ret 8
        return;
    }
    __imp__sub_00016EA0(ctx, base);
}
