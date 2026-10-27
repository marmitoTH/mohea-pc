// The player's shots at any frame rate. Each frame the player's weapons take their aim from the camera
// (sub_000F4DA0: the bullets' direction at the weapon's +0x1F0, their start at +0x210), and the weapon's update fires
// along it (sub_00189420). A shot's recoil comes from the fire animation (sub_00114050, ecx the player): it kicks the
// view up, and the camera after it, a frame later. At 30 frames a second the kick and the bullet came in the same
// frame and the bullet left along the aim before the kick; at 60 the bullet came a frame after the kick and followed
// it, far up when aiming down the sights and far right from the hip with the Kar98k. Here a player's bullet fired
// after a kick leaves along the aim from before it, as at 30.
#include <cstring>
#include <unordered_map>

#include "xdk/hle.h"

namespace {

// the game's thread only: the player's weapons' owner (the player's +0x30, as sub_000F4DA0 has it) and the frames
// since its last kick (kNone: none waiting); each weapon's aim, as of the last frame before a kick for the player's
constexpr int kNone = -1, kFrames = 4;
uint32_t g_owner = 0;
int g_since_kick = kNone;
struct Aim {
    float dir[3], pos[3];
};
std::unordered_map<uint32_t, Aim> g_aims;

}  // namespace

// the recoil (stdcall, 4 arguments) on the player
X86_EXTERN_FUNC(__imp__sub_00114050);
X86_FUNC(sub_00114050) {
    g_owner = ctx.ecx + 0x30;
    g_since_kick = 0;
    __imp__sub_00114050(ctx, base);
}

// a weapon holder's aim each frame (stdcall, ecx the holder, its weapon at +0x1128, aiming while +0x1158 is set; the
// player has one for each weapon): every weapon in hand's, but the player's not after a kick, which is forgotten a few
// frames on if no shot came
X86_EXTERN_FUNC(__imp__sub_000F4DA0);
X86_FUNC(sub_000F4DA0) {
    uint32_t weapon = *reinterpret_cast<uint32_t*>(base + ctx.ecx + 0x1128);
    bool aiming = *(base + ctx.ecx + 0x1158) != 0;
    __imp__sub_000F4DA0(ctx, base);
    if (!aiming || !weapon) return;
    if (*reinterpret_cast<uint32_t*>(base + weapon + 0x254) == g_owner && g_since_kick != kNone) {
        if (++g_since_kick > kFrames) g_since_kick = kNone;
        return;
    }
    Aim& a = g_aims[weapon];
    std::memcpy(a.dir, base + weapon + 0x1F0, sizeof(a.dir));
    std::memcpy(a.pos, base + weapon + 0x210, sizeof(a.pos));
}

// a weapon's shot (ecx the weapon): the player's after a kick, along the aim before it
X86_EXTERN_FUNC(__imp__sub_00189420);
X86_FUNC(sub_00189420) {
    uint32_t weapon = ctx.ecx;
    if (g_since_kick != kNone && *reinterpret_cast<uint32_t*>(base + weapon + 0x254) == g_owner) {
        auto a = g_aims.find(weapon);
        if (a != g_aims.end()) {
            std::memcpy(base + weapon + 0x1F0, a->second.dir, sizeof(a->second.dir));
            std::memcpy(base + weapon + 0x210, a->second.pos, sizeof(a->second.pos));
        }
        g_since_kick = kNone;
    }
    __imp__sub_00189420(ctx, base);
}
