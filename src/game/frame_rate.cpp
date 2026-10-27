// 60 frames a second in game (the settings file's sixty_fps, on by default). The game counts vertical blanks (60 a
// second, its callback sub_00013130 at 0x38DC8C) and paces each frame in sub_000137C0: it waits until [0x3727D4] blanks
// have passed since the last frame, then returns the blanks that did as the frame's time step, which the game scales
// its movement, turning and the rest by. A level's start (sub_0007FC50) sets that wait to 2 blanks, 30 frames a
// second; the menus do not wait. With the option the wait is one blank, as the menus run; without it, back to the
// level's 2 (F9 switches it while playing).
#include "common/options.h"
#include "xdk/hle.h"

bool g_sixty_fps = true;

X86_EXTERN_FUNC(__imp__sub_000137C0);
X86_FUNC(sub_000137C0) {
    constexpr uint32_t kFrameBlanks = 0x3727D4;
    // off, the game's own wait, but the level's 2 again right after switching back from 60
    static bool set = false;
    uint32_t& blanks = *reinterpret_cast<uint32_t*>(base + kFrameBlanks);
    if (g_sixty_fps) {
        blanks = 1;
        set = true;
    } else if (set) {
        blanks = 2;
        set = false;
    }
    __imp__sub_000137C0(ctx, base);
}
