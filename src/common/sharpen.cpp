// See sharpen.h.
#include "common/sharpen.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

#include <toml++/toml.hpp>

#include "core/log.h"
#include "gpu/ext.h"

namespace sharpen {

bool g_on = true;

namespace {

std::atomic<float> g_sharpness{0.5f};

// CAS without scaling: a 3x3 neighbourhood, the cross's weight from the local contrast so edges already sharp take
// little and soft detail takes more; the colour is the console's, unconverted. The neighbours are a console pixel
// apart (q.x render pixels, the render scale): the picture is upscaled, and its detail is as wide as that. Sharpness
// (p.x) 0 to 1 is CAS's lobe from -1/9 to -1/4, past CAS's own -1/5 at 0.8.
const char* kHlsl = R"(
cbuffer B : register(b1) { float4 p; float4 q; };
Texture2D img : register(t0);
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 t = float2((id << 1) & 2, id & 2);
    return float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
}
float3 at(int2 c, int2 d, int2 size) {
    return img.Load(int3(clamp(c + d * int(q.x), int2(0, 0), size - 1), 0)).rgb;
}
float4 cas(float4 pos : SV_Position) : SV_Target {
    int2 size;
    img.GetDimensions(size.x, size.y);
    int2 c = int2(pos.xy);
    float3 a = at(c, int2(-1, -1), size), b = at(c, int2(0, -1), size), cc = at(c, int2(1, -1), size);
    float3 d = at(c, int2(-1, 0), size), e = at(c, int2(0, 0), size), f = at(c, int2(1, 0), size);
    float3 g = at(c, int2(-1, 1), size), h = at(c, int2(0, 1), size), i = at(c, int2(1, 1), size);
    float3 mn = min(min(min(d, e), min(f, b)), h);
    mn += min(mn, min(min(a, cc), min(g, i)));
    float3 mx = max(max(max(d, e), max(f, b)), h);
    mx += max(mx, max(max(a, cc), max(g, i)));
    float3 amp = sqrt(saturate(min(mn, 2.0 - mx) / max(mx, 1e-5)));
    float3 w = amp * (-1.0 / lerp(9.0, 4.0, p.x));
    float3 rgb = saturate(((b + d + f + h) * w + e) / (1.0 + 4.0 * w));
    return float4(rgb, img.Load(int3(c, 0)).a);
}
)";

// The game draws everything with vertex shaders, its HUD and menus too: no draw is pretransformed, and the shaders'
// constants keep stale values a draw does not use, so they tell nothing. Its render states do: traced in game, each
// frame draws the world, the characters, a glow (a full screen draw, an offscreen chain, the glow drawn back), the
// weapon in hand, and last the HUD, whose first draw is the frame's only one with the depth test always passing and
// depth written (the glow's full screen draws pass always without writing). That first draw, after enough others, is
// where the 3D picture is done.
constexpr uint32_t kSceneDraws = 32;
constexpr uint32_t kZFuncAlways = 0x207;  // always passing: the console takes GL values

bool HudStart(const gpu::State& s) {
    return s.rs[gpu::kRsZEnable] && s.simple[gpu::kMZWriteEnable / 4] && s.simple[gpu::kMZFunc / 4] == kZFuncAlways;
}

class Sharpen : public gpu::Extension {
public:
    bool Bound(gpu::Context& ctx, const gpu::State& s, gpu::Target* rt, gpu::Target*, bool) override {
        uint64_t frame = ctx.Presents();
        if (frame != frame_) {
            frame_ = frame;
            scene_ = 0;
            done_ = false;
        }
        uint32_t w = 0, h = 0;
        ctx.GuestSize(w, h);
        // the back buffer: the title's size
        if (!rt || rt->depth || done_ || !w || !h || rt->gw != w || rt->gh != h) return false;
        if (!HudStart(s)) {
            scene_++;
            return false;
        }
        if (scene_ < kSceneDraws) return false;
        done_ = true;
        float sharpness = g_sharpness.load();
        if (!g_on || sharpness <= 0.0f) return false;
        ID3D12Resource* copy = ctx.Feedback(*rt);
        if (!copy) return false;
        ctx.Transition(rt->res.Get(), rt->state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        ID3D12Resource* src[3] = {copy, nullptr, nullptr};
        const DXGI_FORMAT fmt[3] = {rt->fmt, rt->fmt, rt->fmt};
        // the neighbours a console pixel apart, at least one
        float step = std::max(1.0f, std::round(float(rt->w) / float(rt->gw)));
        const float p[8] = {sharpness, 0, 0, 0, step};
        ctx.Pass(kHlsl, "cas", rt->fmt, ctx.View(*rt), rt->w, rt->h, src, fmt, p, -1.0f);
        LOG_FIRST(1, "sharpening: %ux%u, sharpness %.2f, step %.0f", rt->w, rt->h, sharpness, step);
        return true;  // the draw binds its targets again
    }

private:
    uint64_t frame_ = ~0ull;
    uint32_t scene_ = 0;
    bool done_ = false;
};

}  // namespace

void Install() { gpu::SetExtension(std::make_unique<Sharpen>()); }

void Load(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    g_sharpness = std::clamp(float(t["sharpness"].value_or(double(g_sharpness.load()))), 0.0f, 1.0f);
}

void Save(void* table) {
    toml::table& t = *static_cast<toml::table*>(table);
    t.insert_or_assign("sharpness", double(g_sharpness.load()));
}

}  // namespace sharpen
