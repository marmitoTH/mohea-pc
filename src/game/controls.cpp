// Mouse look. The right stick reaches the view through dead zones, limits and curves:
// - the pad reader zeroes an axis under 0.25 (sub_001B31E2); the controller update quantizes it to 8 bits, zeroes |v|
//   up to 38 of 128 and stretches the rest (sub_000C5330); the bindings make it actions 0x9D (x) and 0x9E (y);
// - the player control (sub_00120C70) passes -action 0x9E / 127 (pitch, up positive) and -action 0x9D / 127 (yaw,
//   left positive) to sub_00114980, which keeps them on the player (+0x4B0, +0x4B4);
// - the player's look handler (sub_00114D20, vtable +0x108) clamps each to -1..1, puts |v| through a response curve
//   (sub_000EE700 on the curves at 0x3761C8 and 0x3761F0), times the mode's turn rate, halves a rate that grew by
//   more than 0.75 since the last frame, scales it by the zoom, and turns: sub_001149E0(yaw rate) and
//   sub_00114B60(pitch rate), radians a second, each adding rate * dt (+0x1C8) * 0.016683 to its angle (yaw +0x110,
//   wrapped at 2 pi or held in a mounted gun's limits; pitch within its limits).
// A slow mouse vanished in the dead zones and the curve, a fast one hit the limits and the halving.
//
// Here the mouse's motion since the last frame becomes an angle, added to the player's turn after all of that: the
// rate each turn gets is raised by exactly what makes the angle change by it this frame. A controller keeps its feel.
#include <algorithm>
#include <cmath>

#include "common/pc_input.h"
#include "xdk/hle.h"

namespace {

// the game's thread only: the player, and the mouse's angles for the look handler running now (radians)
uint32_t g_player = 0;
float g_yaw = 0.0f, g_pitch = 0.0f;

// the rate that turns by angle this frame
float RateFor(uint8_t* base, uint32_t player, float angle) {
    float seconds = *reinterpret_cast<float*>(base + player + 0x1C8) * 0.01668335f;
    return seconds > 0.0f ? angle / seconds : 0.0f;
}

}  // namespace

// the player control's look input: which object is the player
X86_EXTERN_FUNC(__imp__sub_00114980);
X86_FUNC(sub_00114980) {
    g_player = ctx.ecx;
    __imp__sub_00114980(ctx, base);
}

// the look handler: the mouse's angles for this frame, for the player only. Aiming (the player's +0x1C0 bit 25) the
// handler slows the stick by the field of view (+0x138, degrees) over 70, the view at rest; the mouse slows by that to
// the power 0.8, a little less, so a scope's zoom neither makes it race nor drag
X86_EXTERN_FUNC(__imp__sub_00114D20);
X86_FUNC(sub_00114D20) {
    if (ctx.ecx == g_player) {
        pc_input::TakeLook(g_yaw, g_pitch);
        if (*reinterpret_cast<uint32_t*>(base + ctx.ecx + 0x1C0) >> 25 & 1) {
            float fov = *reinterpret_cast<float*>(base + ctx.ecx + 0x138);
            float k = std::pow(std::clamp(fov / 70.0f, 0.02f, 1.0f), 0.8f);
            g_yaw *= k;
            g_pitch *= k;
        }
    }
    __imp__sub_00114D20(ctx, base);
    g_yaw = g_pitch = 0.0f;
}

// stdcall (rate) on the player; the return address is at esp. A positive yaw rate turns left
X86_EXTERN_FUNC(__imp__sub_001149E0);
X86_FUNC(sub_001149E0) {
    if (g_yaw != 0.0f && ctx.ecx == g_player) {
        *reinterpret_cast<float*>(base + ctx.esp + 4) -= RateFor(base, ctx.ecx, g_yaw);
        g_yaw = 0.0f;
    }
    __imp__sub_001149E0(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_00114B60);
X86_FUNC(sub_00114B60) {
    if (g_pitch != 0.0f && ctx.ecx == g_player) {
        *reinterpret_cast<float*>(base + ctx.esp + 4) += RateFor(base, ctx.ecx, g_pitch);
        g_pitch = 0.0f;
    }
    __imp__sub_00114B60(ctx, base);
}
