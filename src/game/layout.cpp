// The game's controller layouts (Options, Controller Settings) left out: Sharp Shooter, the default, always, and the
// overlay remaps the controller and the keys on it instead (common/pc_input.h, whose presets are the other layouts).
// The bindings (0x374440, entries of input << 16 | action to 0xFFFFFFFF; 0x374540 in a vehicle) are what the player
// control asks (sub_000C66D0). Loading a profile, sub_000C6C40 (stdcall (layout, port), on the input manager 0x3C6600)
// has the layout, one of the manager's 4 (+0x2EFC, 0x178 bytes each: Sharp Shooter, Commander, Recon, Reloader), name
// a button for each of its elements (sub_000C62E0: the elements Action_Jump, Action_Aim and so on, the buttons
// Xbox_ButtonA, Xbox_TriggerL, Xbox_Up and so on, each by the CRC-32 of its name; the tables at 0x3737B0 and
// 0x373888), and sub_000793A0 writes the buttons' inputs into the actions they drive: in place in 0x374440, or in a
// split screen game the port's own copy (0x375740 + port * 0x100). The bindings as the game ships them are not Sharp
// Shooter's (it moves 7 actions), so the layout is still applied, Sharp Shooter, whichever the profile has.
#include "core/log.h"
#include "xdk/hle.h"

namespace {

constexpr uint32_t kSharpShooter = 0;

}  // namespace

X86_EXTERN_FUNC(__imp__sub_000C6C40);
X86_FUNC(sub_000C6C40) {
    uint32_t& layout = *reinterpret_cast<uint32_t*>(base + ctx.esp + 4);
    if (layout != kSharpShooter)
        LOGI("layout: the profile's controller layout %u left out, Sharp Shooter instead", layout);
    layout = kSharpShooter;
    __imp__sub_000C6C40(ctx, base);
}
