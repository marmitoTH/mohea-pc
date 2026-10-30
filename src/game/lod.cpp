// The distances characters and objects keep their detail at (the settings file's lod_distance, percent of the game's,
// 100 to 400; 400 by default, the Enhanced preset, Original 100). The LOD manager
// (\moh4\code\Moh4\object\LODManager.cpp, the one at [0x3DB614], made by sub_000F21B0) keeps two sets of distances
// in metres: +0x00 to +0x10 4, 8, 60, 80 and 100000, +0x14 to +0x1C 30, 80 and 100000.
// sub_000F1AA0 turns the first into a table by whole metre to 99 (+0x148: detail 0 to 4, 4 past 80 m), sub_000F1BA0
// the second (+0x2D8). Each frame sub_000F1C90 (on the manager, stdcall (camera)) measures each object's distance
// from the camera, rounded to a metre and held at 99, and gives it both tables' entries. Full detail ended at 4 m, so
// characters changed models a few steps away. Here the distances are scaled (not the 100000s) and both tables made
// again, when the manager is first seen and each time the setting changes. Past about 125% the 80 m step is out of
// the tables' 99 m, so far characters stay drawn at the detail before it.
#include <algorithm>

#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

uint32_t g_lod_distance = 400;

uint32_t LodDistance() { return std::clamp(g_lod_distance, 100u, 400u); }

namespace {

constexpr uint32_t kBuildFirst = 0xF1AA0, kBuildSecond = 0xF1BA0;

// the manager seen, its distances as the game made them, and the percent its tables were made for
uint32_t g_manager = 0;
float g_game[8] = {};
uint32_t g_made = 0;

// the distances scaled: all but the 100000s (+0x10, +0x1C)
constexpr bool kScaled[8] = {true, true, true, true, false, true, true, false};

}  // namespace

X86_EXTERN_FUNC(__imp__sub_000F1C90);
X86_FUNC(sub_000F1C90) {
    uint32_t manager = ctx.ecx, percent = LodDistance();
    float* d = reinterpret_cast<float*>(base + manager);
    if (manager != g_manager) {
        g_manager = manager;
        std::copy(d, d + 8, g_game);
        g_made = 100;
    }
    if (percent != g_made) {
        for (int i = 0; i < 8; i++) d[i] = kScaled[i] ? g_game[i] * float(percent) / 100.0f : g_game[i];
        uint32_t ecx = ctx.ecx;
        ctx.ecx = manager;
        kernel::CallGuest(ctx, kBuildFirst, {});
        ctx.ecx = manager;
        kernel::CallGuest(ctx, kBuildSecond, {});
        ctx.ecx = ecx;
        g_made = percent;
        LOGI("lod: distances at %u%%: full detail to %.0f m, then %.0f, %.0f, %.0f", percent, d[0], d[1], d[2], d[3]);
    }
    __imp__sub_000F1C90(ctx, base);
}
