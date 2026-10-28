// The field of view at rest (the settings file's fov, degrees across a 4:3 view, 70 the game's). Cameras set their
// view in sub_0018B780 (thiscall, stdcall (angle)): half the view across, in radians, a 4:3 one, which it narrows by
// 0.75 for the height. The player's camera passes its field of view (sub_001142F0: the player's +0x138, 35 at rest,
// less aiming and less again in a scope). Here an angle at the game's rest becomes half the setting, the angles of a
// scope stay as they are, and those between (aiming down the sights, easing back from it) move by a share of it,
// so the view widens at rest and still zooms and eases as the game has it.
#include <algorithm>
#include <cmath>

#include "common/options.h"
#include "xdk/hle.h"

uint32_t g_fov = 70;

float RestFov() { return float(std::clamp(g_fov, 60u, 110u)); }

namespace {

constexpr float kRad = 0.0174532925f;
constexpr float kRest = 35.0f * kRad;  // the game's half view at rest
constexpr float kLow = 26.0f * kRad;   // below it the setting takes no share: a scope's view

}  // namespace

X86_EXTERN_FUNC(__imp__sub_0018B780);
X86_FUNC(sub_0018B780) {
    float& angle = *reinterpret_cast<float*>(base + ctx.esp + 4);
    if (g_fov != 70 && angle <= kRest + 1e-4f && angle > kLow) {
        float share = std::clamp((angle - kLow) / (kRest - kLow), 0.0f, 1.0f);
        angle += share * (RestFov() * 0.5f * kRad - kRest);
    }
    __imp__sub_0018B780(ctx, base);
}
