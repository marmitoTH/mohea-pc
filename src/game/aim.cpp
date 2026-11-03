// The player's shots at any frame rate. Each frame the player's weapons take their aim from the camera
// (sub_000F4DA0: the bullets' direction at the weapon's +0x1F0, their start at +0x210), and the weapon's update fires
// along it (sub_00189420). A shot's recoil comes from the fire animation (sub_00114050, ecx the player): it kicks the
// view up, and the camera after it, a frame later. At 30 frames a second the kick and the bullet came in the same
// frame and the bullet left along the aim before the kick; at 60 the bullet came a frame after the kick and followed
// it, far up when aiming down the sights and far right from the hip with the Kar98k. Here a player's bullet fired
// after a kick leaves along the aim from before it, as at 30.
//
// A held trigger, too: event 4 to the weapon holder's event handler (sub_000F5180) each frame, 0x41 once let go. Each
// shot enters the holder's Firing state (AimFiring down the sights), whose entry (sub_000A40B0, sub_000A42E0) kicks and
// starts the fire animation, whose fire event (0x31, sub_000A3010) asks the weapon for the bullet, which sets the
// weapon's wait (+0x1D4). The animation's end leaves the state, about 4.5 blanks in for the StG 44 and the Thompson,
// and the next trigger enters it again: at 30 frames a second, a trigger each 2 blanks, 6 blanks a shot, 600 a minute
// (their interval, 100 ms); at 60, 5 blanks, 720. And in the state the trigger (sub_000A4190) kicks and starts the
// animation over when the wait has run out, and until the bullet it has: with no wait left (a fresh weapon, a reload)
// a trigger a blank after the kick, before the fire event, started the animation over, every frame: kicks and the fire
// animation, no bullets. Here, at 60, the player's trigger an odd number of blanks into a burst is let go by (not two
// running, should the frames come 2 blanks apart), the next frame's taking its place, as at 30. And one less than 4
// blanks after the kick, the soonest a shot follows another at 30 (two frames): the PPSh's animation is over a frame
// after its kick, so the next frame's trigger fired again, every 2 frames at either rate, 1800 a minute at 60.
//
// And the MP40 asks its weapon for the bullet twice, at the kick and a blank on: at 30 frames a second both came in the
// kick's frame, one bullet; at 60 a frame apart, two bullets a kick, twice the rate. Here, at 60, the player's bullet
// a blank after one, in the same 2 blanks of the kick's steps, is not fired.
#include <cstring>
#include <unordered_map>

#include "common/options.h"
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

// the blank the player's last kick came at (the blank count, frame_rate.cpp's); a burst's shots come within kBurst.
// The last trigger let go by, and the blank of the player's last bullet.
constexpr uint32_t kBlanks = 0x38DC8C, kBurst = 16, kTrigger = 4, kSoonest = 4;
uint32_t g_kick_blank = 0;
bool g_skipped = false;  // the last trigger let go by for its odd blank
uint32_t g_shot_blank = 0;

uint32_t Blanks(uint8_t* base) { return *reinterpret_cast<volatile uint32_t*>(base + kBlanks); }

// the player's weapon in the holder's hand
bool Players(uint8_t* base, uint32_t holder) {
    uint32_t weapon = holder ? *reinterpret_cast<uint32_t*>(base + holder + 0x1128) : 0;
    return weapon && *reinterpret_cast<uint32_t*>(base + weapon + 0x254) == g_owner;
}

}  // namespace

// the recoil (stdcall, 4 arguments) on the player
X86_EXTERN_FUNC(__imp__sub_00114050);
X86_FUNC(sub_00114050) {
    g_owner = ctx.ecx + 0x30;
    g_since_kick = 0;
    g_kick_blank = Blanks(base);
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

// a weapon's shot (ecx the weapon): the player's after a kick, along the aim before it; at 60, the player's a blank
// after its last, an odd number of blanks from the kick, not fired
X86_EXTERN_FUNC(__imp__sub_00189420);
X86_FUNC(sub_00189420) {
    uint32_t weapon = ctx.ecx;
    if (*reinterpret_cast<uint32_t*>(base + weapon + 0x254) == g_owner) {
        uint32_t now = Blanks(base), last = g_shot_blank;
        g_shot_blank = now;
        if (g_sixty_fps && now - last == 1 && (now - g_kick_blank) % 2) {
            ctx.esp += 4;  // ret
            return;
        }
    }
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

// the holder's event handler (stdcall (event, data), ecx the holder): the player's trigger, at 60, an odd number of
// blanks into a burst, or sooner after the kick than at 30, let go by
X86_EXTERN_FUNC(__imp__sub_000F5180);
X86_FUNC(sub_000F5180) {
    uint32_t event = *reinterpret_cast<uint32_t*>(base + ctx.esp + 4);
    if (event == kTrigger && g_sixty_fps && Players(base, ctx.ecx)) {
        uint32_t since = Blanks(base) - g_kick_blank;
        bool odd = !g_skipped && since <= kBurst && since % 2;
        g_skipped = odd;
        if (odd || since < kSoonest) {
            ctx.esp += 12;  // ret 8
            return;
        }
    }
    __imp__sub_000F5180(ctx, base);
}
