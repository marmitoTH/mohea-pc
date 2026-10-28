// Movies the scripts play, the EA and THX logos at boot among them, skippable. The script command sub_0017AEC0 plays a
// movie (sub_00137260: name, context, a callback each frame that stops the movie by returning 0) with sub_00179100 on a
// byte the command's second argument sets: when the byte is set, start or A (actions 0x89 and 0x90, on any controller;
// the keyboard's Enter is start) stops the movie. The logos are played with it clear, so they played to the end; here
// it is set.
#include "xdk/hle.h"

X86_EXTERN_FUNC(__imp__sub_00179100);
X86_FUNC(sub_00179100) {
    uint32_t skippable = *reinterpret_cast<uint32_t*>(base + ctx.esp + 4);
    *(base + skippable) = 1;
    __imp__sub_00179100(ctx, base);
}
