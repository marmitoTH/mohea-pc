// Movies, skippable. Each plays through sub_00137260 (name, context, a callback each frame that stops the movie by
// returning 0), whose callbacks stop it on start or A (actions 0x89 and 0x90, on any controller; the keyboard's Enter is
// start), but only when the byte the context points at is set:
// - the script command sub_0017AEC0, with sub_00179100 on a byte its second argument sets: the EA and THX logos at
//   boot are played with it clear;
// - a mission's intro (sub_0007F2F0), with sub_0007F260 on the byte at 0x36EE80, which nothing sets.
// Here the byte is set, so they all play to the end only when not skipped.
#include "xdk/hle.h"

namespace {

void Skippable(uint8_t* base, uint32_t esp) {
    uint32_t flag = *reinterpret_cast<uint32_t*>(base + esp + 4);
    *(base + flag) = 1;
}

}  // namespace

X86_EXTERN_FUNC(__imp__sub_00179100);
X86_FUNC(sub_00179100) {
    Skippable(base, ctx.esp);
    __imp__sub_00179100(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_0007F260);
X86_FUNC(sub_0007F260) {
    Skippable(base, ctx.esp);
    __imp__sub_0007F260(ctx, base);
}
