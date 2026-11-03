// The free camera (the overlay's hack, common/pc_input.h g_free_camera): the frame drawn from a camera the player
// flies, while the game, which gets none of the player's input, goes on with its own. The camera the frame is drawn
// from is the one at [0x3E8CF8], and the frame's draw (sub_00080B40) reads its world transform (+0x30: right,
// forward, up and position rows, the world z up) for the view's culling planes, and its view (+0xD0, the world
// transform's inverse, the view's y and z the camera's up and forward) for the draws. Here both are the free camera's
// for the draw and the game's again after it, so the game's camera goes on as it was, and the view is back on it once
// the hack is off. The level's streaming and the characters' detail go by the free camera too (below). It starts
// where the game's camera is.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "common/pc_input.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

extern uint32_t g_character;  // game/play_keys.cpp: the player's character

namespace {

constexpr uint32_t kActiveCamera = 0x3E8CF8;
constexpr uint32_t kPlayer = 0x3E8D08;  // the players (sub_00130D50), the first the one playing, its position +0x90
constexpr uint32_t kWorld = 0x30, kView = 0xD0;
constexpr float kPi = 3.14159265f;
constexpr float kTurnRate = 2.5f;       // radians a second, a stick pushed fully
constexpr float kSpeedStep = 1.25f;     // a wheel notch or a d-pad press
constexpr float kDefaultSpeed = 4.0f;   // metres a second
constexpr float kMinSpeed = 0.25f, kMaxSpeed = 64.0f;
constexpr float kMaxPitch = 1.55f;      // short of straight up and down

struct Vec {
    float x, y, z;
};
Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec operator*(Vec a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// the game's thread only
struct State {
    bool flying = false;
    Vec pos{};
    Vec eye{};  // the game's camera above the player as it started
    float yaw = 0, pitch = 0;  // yaw from the world's x towards y, pitch up from level
    float speed = kDefaultSpeed;
    std::chrono::steady_clock::time_point last;
} g_cam;

// the camera's axes at its yaw and pitch
void Axes(Vec& right, Vec& forward, Vec& up) {
    float cy = std::cos(g_cam.yaw), sy = std::sin(g_cam.yaw), cp = std::cos(g_cam.pitch), sp = std::sin(g_cam.pitch);
    forward = {cp * cy, cp * sy, sp};
    right = {sy, -cy, 0};
    up = {right.y * forward.z - right.z * forward.y, right.z * forward.x - right.x * forward.z,
          right.x * forward.y - right.y * forward.x};
}

// starts where the game's camera is, looking where it looks
void Start(uint8_t* base, const float* world) {
    Vec forward{world[4], world[5], world[6]};
    g_cam.pos = {world[12], world[13], world[14]};
    g_cam.eye = {};
    if (uint32_t player = *reinterpret_cast<uint32_t*>(base + kPlayer)) {
        const float* at = reinterpret_cast<const float*>(base + player + 0x90);
        g_cam.eye = {g_cam.pos.x - at[0], g_cam.pos.y - at[1], g_cam.pos.z - at[2]};
    }
    g_cam.yaw = std::atan2(forward.y, forward.x);
    g_cam.pitch = std::asin(std::clamp(forward.z, -1.0f, 1.0f));
    g_cam.speed = kDefaultSpeed;
    g_cam.last = std::chrono::steady_clock::now();
    g_cam.flying = true;
}

// the player's input since the last frame
void Fly() {
    auto now = std::chrono::steady_clock::now();
    float dt = std::min(std::chrono::duration<float>(now - g_cam.last).count(), 0.1f);
    g_cam.last = now;
    pc_input::FreeCamera in = pc_input::TakeFreeCamera();
    for (; in.speed > 0; in.speed--) g_cam.speed = std::min(g_cam.speed * kSpeedStep, kMaxSpeed);
    for (; in.speed < 0; in.speed++) g_cam.speed = std::max(g_cam.speed / kSpeedStep, kMinSpeed);
    g_cam.yaw -= in.yaw + in.turn[0] * kTurnRate * dt;
    g_cam.pitch = std::clamp(g_cam.pitch + in.pitch + in.turn[1] * kTurnRate * dt, -kMaxPitch, kMaxPitch);
    g_cam.yaw = std::remainder(g_cam.yaw, 2 * kPi);
    Vec right, forward, up;
    Axes(right, forward, up);
    Vec move = right * in.move[0] + forward * in.move[1] + Vec{0, 0, 1} * in.move[2];
    g_cam.pos = g_cam.pos + move * (g_cam.speed * dt);
}

// the free camera's world transform and view, as the game lays them out
void Matrices(float* world, float* view) {
    Vec right, forward, up;
    Axes(right, forward, up);
    const Vec rows[3] = {right, forward, up};
    std::memset(world, 0, 64);
    for (int r = 0; r < 3; r++) world[r * 4] = rows[r].x, world[r * 4 + 1] = rows[r].y, world[r * 4 + 2] = rows[r].z;
    world[12] = g_cam.pos.x, world[13] = g_cam.pos.y, world[14] = g_cam.pos.z, world[15] = 1;
    // the inverse (the axes' transpose and the position taken off), its columns right, up, forward
    const Vec cols[3] = {right, up, forward};
    std::memset(view, 0, 64);
    for (int c = 0; c < 3; c++) {
        view[c] = cols[c].x, view[4 + c] = cols[c].y, view[8 + c] = cols[c].z;
        view[12 + c] = -Dot(cols[c], g_cam.pos);
    }
    view[15] = 1;
}

// The shaders' camera (sub_0018B2E0, from the camera's own update as the game's camera moves, not at the frame's
// draw): its world transform (Camera::CameraToWorld, its last row Camera::CameraPosition, which the level's
// compartments fade by, Compartment::FadeParams) and its view (ECamera::WorldToCamera)
constexpr uint32_t kShaderWorld = 0x3789B0, kShaderView = 0x3789F0;

// The active camera the free camera's for a while (a draw, an update), and the shaders' too, the game's again after
class Swap {
public:
    Swap(uint8_t* base, uint32_t cam)
        : world_(reinterpret_cast<float*>(base + cam + kWorld)), view_(reinterpret_cast<float*>(base + cam + kView)),
          shader_world_(reinterpret_cast<float*>(base + kShaderWorld)),
          shader_view_(reinterpret_cast<float*>(base + kShaderView)) {
        std::memcpy(game_[0], world_, 64);
        std::memcpy(game_[1], view_, 64);
        std::memcpy(game_[2], shader_world_, 64);
        std::memcpy(game_[3], shader_view_, 64);
        Matrices(world_, view_);
        std::memcpy(shader_world_, world_, 64);
        std::memcpy(shader_view_, view_, 64);
    }
    ~Swap() {
        std::memcpy(world_, game_[0], 64);
        std::memcpy(view_, game_[1], 64);
        std::memcpy(shader_world_, game_[2], 64);
        std::memcpy(shader_view_, game_[3], 64);
    }

private:
    float *world_, *view_, *shader_world_, *shader_view_;
    float game_[4][16];
};

// an address in the game's memory: its image from 0x10000, its heap above 0x80000000
bool Guest(uint32_t a) { return a >= 0x10000 && a < 0xF0000000u; }

// The player put where the free camera is (pc_input::g_free_camera_drop), the camera's height above it as it started
// off. The character (game/play_keys.cpp, the same as the first of [0x3E8D08]) keeps its world transform at +0x60,
// its position the row at +0x90, and copies of the position elsewhere that put it back where it was: in itself (+0xD0,
// last positions further on) and in what it points to (its physics: [+0x34] +0x58, [+0x324] +0x1B0). Every exact copy
// (x, y and z), in the character and in what it points to in the console's memory, is moved
void Drop(uint8_t* base) {
    uint32_t c = g_character;
    if (!Guest(c)) {
        LOGW("free camera: the player not dropped, no character seen");
        return;
    }
    float old[3];
    std::memcpy(old, base + c + 0x90, sizeof(old));
    Vec at = g_cam.pos + g_cam.eye * -1.0f;
    const float to[3] = {at.x, at.y, at.z};
    int moved = 0;
    auto move = [&](uint32_t from, uint32_t size) {
        for (uint32_t o = 0; o + 12 <= size; o += 4) {
            float* f = reinterpret_cast<float*>(base + from + o);
            if (f[0] != old[0] || f[1] != old[1] || f[2] != old[2]) continue;
            std::memcpy(f, to, sizeof(to));
            moved++;
        }
    };
    move(c, 0x2000);
    for (uint32_t field = 0; field < 0x2000; field += 4) {
        uint32_t p = *reinterpret_cast<uint32_t*>(base + c + field);
        // the console's 64 MB of memory, mapped from 0x80000000
        if (p != c && (p & 3) == 0 && p >= 0x80000000u && p < 0x84000000u - 0x400) move(p, 0x400);
    }
    LOGI("free camera: the player dropped at %.1f %.1f %.1f (from %.1f %.1f %.1f, %d copies)", at.x, at.y, at.z,
         old[0], old[1], old[2], moved);
}

}  // namespace

// The LOD manager's viewers (game/lod.cpp: each 0x40 bytes from +0x30, their count +0x24, the camera +0x10, its
// position +0x20) take their cameras' positions (sub_000F1E80, on the manager, as the game updates, before the frame's
// draw): the active camera's is the free camera's while it flies, so the detail goes by where it is
X86_EXTERN_FUNC(__imp__sub_000F1E80);
X86_FUNC(sub_000F1E80) {
    uint32_t manager = ctx.ecx;
    __imp__sub_000F1E80(ctx, base);
    uint32_t cam = *reinterpret_cast<uint32_t*>(base + kActiveCamera);
    if (!pc_input::g_free_camera || !g_cam.flying || !cam) return;
    int32_t count = *reinterpret_cast<int32_t*>(base + manager + 0x24);
    for (int32_t i = 0; i < count; i++) {
        uint32_t slot = manager + 0x30 + uint32_t(i) * 0x40;
        if (*reinterpret_cast<uint32_t*>(base + slot + 0x10) != cam) continue;
        float* pos = reinterpret_cast<float*>(base + slot + 0x20);
        pos[0] = g_cam.pos.x, pos[1] = g_cam.pos.y, pos[2] = g_cam.pos.z;
    }
}

// The streamer's update (sub_0010BAA0, stdcall (one), on the streamer, as the game updates) takes the active camera's
// forward for its look ahead, and orders the cells it loads, under a memory budget, by their distance from the active
// camera and how far ahead of it they are (sub_0010C4B0 by sub_0010BFA0): the camera then the game's, at the player,
// put the free camera's cells last, and they never loaded. While it flies the update has the free camera instead
X86_EXTERN_FUNC(__imp__sub_0010BAA0);
X86_FUNC(sub_0010BAA0) {
    uint32_t cam = *reinterpret_cast<uint32_t*>(base + kActiveCamera);
    if (!pc_input::g_free_camera || !g_cam.flying || !cam) {
        __imp__sub_0010BAA0(ctx, base);
        return;
    }
    Swap swap(base, cam);
    __imp__sub_0010BAA0(ctx, base);
}

// The level's detail compartments stream in around a point (sub_0010B790, stdcall (point), on the world's streamer,
// from its update sub_0010BAA0): the followed object's position ([0x3E8D08] +0x90) and a look ahead along the active
// camera's forward. While it flies the point is the free camera's, the same look ahead along its own forward, so the
// far geometry it flies to loads
X86_EXTERN_FUNC(__imp__sub_0010B790);
X86_FUNC(sub_0010B790) {
    uint32_t followed = *reinterpret_cast<uint32_t*>(base + kPlayer);
    if (pc_input::g_free_camera && g_cam.flying && followed) {
        float* point = reinterpret_cast<float*>(base + *reinterpret_cast<uint32_t*>(base + ctx.esp + 4));
        const float* at = reinterpret_cast<const float*>(base + followed + 0x90);
        Vec ahead{point[0] - at[0], point[1] - at[1], point[2] - at[2]};
        float k = std::sqrt(Dot(ahead, ahead));
        Vec right, forward, up;
        Axes(right, forward, up);
        Vec p = g_cam.pos + forward * k;
        point[0] = p.x, point[1] = p.y, point[2] = p.z;
    }
    __imp__sub_0010B790(ctx, base);
}

// the frame's draw
X86_EXTERN_FUNC(__imp__sub_00080B40);
X86_FUNC(sub_00080B40) {
    uint32_t cam = *reinterpret_cast<uint32_t*>(base + kActiveCamera);
    if (!pc_input::g_free_camera || !cam) {
        if (g_cam.flying && pc_input::g_free_camera_drop) Drop(base);
        g_cam.flying = false;
        __imp__sub_00080B40(ctx, base);
        return;
    }
    if (!g_cam.flying) Start(base, reinterpret_cast<const float*>(base + cam + kWorld));
    Fly();
    // the camera is the game's again after: its own update goes on from it
    Swap swap(base, cam);
    __imp__sub_00080B40(ctx, base);
}
