// 60 frames a second in game (the settings file's sixty_fps, on by default). The game counts vertical blanks (60 a
// second, its callback sub_00013130 at 0x38DC8C) and paces each frame in sub_000137C0: it waits until [0x3727D4] blanks
// have passed since the last frame, then returns the blanks that did as the frame's time step, which the game scales
// its movement, turning and the rest by. A level's start (sub_0007FC50) sets that wait to 2 blanks, 30 frames a
// second; the menus do not wait. With the option the wait is one blank, as the menus run; without it, back to the
// level's 2 (F9 switches it while playing).
//
// Frozen (g_freeze), the frame's time step is 0 blanks: the world, its animations and its scripts stand still, and
// the game still draws each frame, so the free camera flies about the moment and a graphics setting changed shows.
// The player control, which marks each frame as play (pc_input::SetPlay), stands still too: frozen in play, play is
// kept here, so the free camera's input and the in-play effects go on. The sound and the music are held where they
// are (audio::g_hold) until it goes on.
#include <chrono>
#include <cmath>
#include <thread>

#include "common/options.h"
#include "audio/audio.h"
#include "common/pc_input.h"
#include "xdk/hle.h"

bool g_sixty_fps = true;
bool g_freeze = false;

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
    static bool frozen = false, in_play = false;
    if (g_freeze && !frozen) in_play = pc_input::InPlay();
    frozen = g_freeze;
    audio::g_hold = g_freeze;
    if (!g_freeze) return;
    if (in_play) pc_input::KeepPlay();
    ctx.eax = 0;
}

// The wait itself (0x137E0) reads the blank count until enough blanks have passed, with nothing between the reads:
// on the console's one core that cost nothing, on a PC it held the game's thread at a core's full load the whole
// wait (64% of a core at 60 frames a second, 88% at 30). The blanks come from the runtime's vertical blank thread,
// every 16.7 ms (xdk/d3d8.cpp). Here the call just before the wait (sub_00013500, the frame's screen fade) is
// followed by 1 ms sleeps until the wait is due (the runtime keeps the timer at 1 ms), so the game's own loop
// finds it done at its first read.
X86_EXTERN_FUNC(__imp__sub_00013500);
X86_FUNC(sub_00013500) {
    constexpr uint32_t kFromWait = 0x137D1, kCount = 0x38DC8C, kLast = 0x38DC6C, kFrameBlanks = 0x3727D4;
    uint32_t from = *reinterpret_cast<uint32_t*>(base + ctx.esp);
    __imp__sub_00013500(ctx, base);
    if (from != kFromWait) return;
    auto read = [&](uint32_t address) { return *reinterpret_cast<volatile uint32_t*>(base + address); };
    for (int ms = 0; read(kCount) - read(kLast) < read(kFrameBlanks) && ms < 250; ms++)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

// The script timers (sub_0014CD90, cdecl (step), on the list at 0x4F7298; the frame update passes the frame's step in
// blanks, sub_00036410) fire when their delay has run out, in blanks too, but the scripts set some in seconds: a
// demolition charge's charging ticks every 0.033 (sub_0008EAB0, 90 ticks to plant). At 30 frames a second such a
// timer fired every frame, 2 blanks; at 60 every frame, 1 blank, and planting took half as long. Here the timers run
// as at 30 at any frame rate: in whole steps of 2 blanks, a longer frame's in one, the rest kept for the next.
X86_EXTERN_FUNC(__imp__sub_0014CD90);
X86_FUNC(sub_0014CD90) {
    constexpr float kStep = 2.0f;
    static float waiting = 0.0f;
    float& step = *reinterpret_cast<float*>(base + ctx.esp + 4);
    waiting += step;
    if (waiting < kStep) {
        ctx.esp += 4;  // ret
        return;
    }
    step = std::floor(waiting / kStep) * kStep;
    waiting -= step;
    __imp__sub_0014CD90(ctx, base);
}
