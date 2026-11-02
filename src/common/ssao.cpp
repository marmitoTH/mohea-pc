// See ssao.h.
#include "common/ssao.h"

#include <algorithm>
#include <atomic>
#include <cmath>

#include <toml++/toml.hpp>

#include "core/log.h"
#include "gpu/backend.h"

namespace ssao {

bool g_on = true;

namespace {

std::atomic<float> g_strength{0.5f};

// world units (the game's look like metres: its near plane is 0.05, its far 400)
constexpr float kRadius = 1.0f;
constexpr float kFadeEnd = 60.0f;  // past it none, from 60% of it fading: distant fog keeps its own look
constexpr uint32_t kSceneDraws = 32;

constexpr DXGI_FORMAT kDepthRead = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;  // the renderer's depth, its float part
constexpr DXGI_FORMAT kAoFormat = DXGI_FORMAT_R8_UNORM;

// p: a, b (z / w = a + b / w), sx, sy (x and y's scale); q: radius, strength (0 to 1), fade end. Depth is stored reversed,
// 1 - z / w, so the view distance w = b / (1 - d - a)
const char* kHlsl = R"(
cbuffer B : register(b1) { float4 p; float4 q; };
Texture2D t0 : register(t0);
Texture2D t1 : register(t1);
Texture2D t2 : register(t2);
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 t = float2((id << 1) & 2, id & 2);
    return float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
}
float dist(float d) { return p.y / (1.0 - d - p.x); }
float3 view(Texture2D dep, int2 c, float2 size) {
    float w = dist(dep.Load(int3(c, 0)).r);
    float2 ndc = float2((c.x + 0.5) / size.x * 2 - 1, 1 - (c.y + 0.5) / size.y * 2);
    return float3(ndc.x * w / p.z, ndc.y * w / p.w, w);
}

// the occlusion, a half size target: t0 the depth
float ao(float4 pos : SV_Position) : SV_Target {
    uint2 isize;
    t0.GetDimensions(isize.x, isize.y);
    float2 size = float2(isize);
    int2 c = min(int2(pos.xy) * 2, int2(isize) - 2);
    if (t0.Load(int3(c, 0)).r <= 0) return 1;  // the far plane: the sky
    float3 P = view(t0, c, size);
    if (P.z > q.z) return 1;
    // the normal from the nearer neighbour each way, so an edge does not bend it
    float3 r = view(t0, c + int2(1, 0), size) - P, l = P - view(t0, max(c - int2(1, 0), 0), size);
    float3 d = view(t0, c + int2(0, 1), size) - P, u = P - view(t0, max(c - int2(0, 1), 0), size);
    float3 dx = abs(r.z) < abs(l.z) ? r : l, dy = abs(d.z) < abs(u.z) ? d : u;
    float3 n = normalize(cross(dx, dy));
    if (n.z > 0) n = -n;  // facing the camera
    float rpx = min(q.x * p.z / P.z * size.x * 0.5, size.x * 0.08);  // the radius in pixels
    if (rpx < 2) return 1;
    float noise = frac(52.9829189 * frac(dot(pos.xy, float2(0.06711056, 0.00583715))));
    float occ = 0;
    const int kDirs = 6, kSteps = 4;
    [unroll] for (int i = 0; i < kDirs; i++) {
        float ang = (i + noise) * 6.2831853 / kDirs;
        float2 dir = float2(cos(ang), sin(ang));
        [unroll] for (int j = 0; j < kSteps; j++) {
            float t = (j + 0.25 + 0.5 * noise) / kSteps;
            int2 s = clamp(c + int2(dir * rpx * t), int2(0, 0), int2(isize) - 1);
            float3 v = view(t0, s, size) - P;
            float l2 = dot(v, v);
            float ndv = dot(n, v) * rsqrt(l2 + 1e-6);
            occ += saturate(ndv - 0.15) * saturate(1 - l2 / (q.x * q.x));
        }
    }
    float a = 1 - occ / (kDirs * kSteps) * 1.6;
    return lerp(saturate(a), 1, saturate((P.z - q.z * 0.6) / (q.z * 0.4)));
}

// the picture times the occlusion, blurred by depth: t0 the picture, t1 the occlusion, t2 the depth
float4 comp(float4 pos : SV_Position) : SV_Target {
    int2 c = int2(pos.xy);
    float4 colour = t0.Load(int3(c, 0));
    uint2 hs;
    t1.GetDimensions(hs.x, hs.y);
    float d0 = dist(t2.Load(int3(c, 0)).r);
    int2 h = c / 2;
    float sum = 0, wsum = 0;
    [unroll] for (int y = -1; y <= 2; y++) {
        [unroll] for (int x = -1; x <= 2; x++) {
            int2 s = clamp(h + int2(x, y) - 1, int2(0, 0), int2(hs) - 1);
            float sd = dist(t2.Load(int3(s * 2, 0)).r);
            float w = exp(-abs(sd - d0) / (d0 * 0.03 + 1e-3));
            sum += t1.Load(int3(s, 0)).r * w;
            wsum += w;
        }
    }
    float a = wsum > 1e-4 ? sum / wsum : 1;
    // the strength as a power: 0 none, 0.5 squared, 1 to the fourth, the corners darker before the flat parts
    return float4(colour.rgb * pow(saturate(a), q.y * 4), colour.a);
}
)";

}  // namespace

float Strength() { return g_strength.load(); }
void SetStrength(float v) { g_strength = std::clamp(v, 0.0f, 1.0f); }

void Load(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    g_strength = std::clamp(float(t["ssao_strength"].value_or(double(g_strength.load()))), 0.0f, 1.0f);
}

void Save(void* table) {
    double strength = std::round(double(g_strength.load()) * 100.0) / 100.0;
    static_cast<toml::table*>(table)->insert_or_assign("ssao_strength", strength);
}

// A perspective transform in c0..c3 (the characters' and many objects' world view projection): c2's xyz is c3's
// times a, a little over 1, and the rest is b. c3's length is the object's scale, which c0's and c1's share.
void Pass::Capture(const gpu::State& s, uint64_t frame) {
    if (seen_ == frame) return;
    const auto& c0 = s.vsc[96];  // hardware order: D3D register + 96
    const auto& c1 = s.vsc[97];
    const auto& c2 = s.vsc[98];
    const auto& c3 = s.vsc[99];
    float l3 = c3[0] * c3[0] + c3[1] * c3[1] + c3[2] * c3[2];
    if (l3 < 1e-6f) return;
    float a = (c2[0] * c3[0] + c2[1] * c3[1] + c2[2] * c3[2]) / l3;
    if (a <= 1.0f || a > 1.01f) return;
    for (int i = 0; i < 3; i++)
        if (std::fabs(c2[i] - a * c3[i]) > 1e-4f * std::sqrt(l3)) return;
    float b = c2[3] - a * c3[3];
    if (b >= 0) return;
    float scale = std::sqrt(l3);
    a_ = a;
    b_ = b;
    sx_ = std::sqrt(c0[0] * c0[0] + c0[1] * c0[1] + c0[2] * c0[2]) / scale;
    sy_ = std::sqrt(c1[0] * c1[0] + c1[1] * c1[1] + c1[2] * c1[2]) / scale;
    seen_ = frame;
    LOG_FIRST(1, "ssao: projection near %.3f far %.1f, x scale %.3f, y scale %.3f", -b / a, -b / (a - 1.0f), sx_,
              sy_);
}

void Pass::Hud(uint64_t frame) {
    if (g_on && done_ != frame) LOG_FIRST(3, "ssao: no draw without depth writes before the HUD this frame");
}

void Pass::Ensure(gpu::Context& ctx, uint32_t w, uint32_t h) {
    if (ao_ && ao_w_ == w && ao_h_ == h) return;
    if (ao_) ctx.Release(ao_);
    ao_.Reset();
    ID3D12Device* dev = ctx.Device();
    if (!rtv_) {
        D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1};
        if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv_)))) return;
    }
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = kAoFormat;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ao_state_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, ao_state_, nullptr, IID_PPV_ARGS(&ao_)))) {
        ao_.Reset();
        return;
    }
    dev->CreateRenderTargetView(ao_.Get(), nullptr, rtv_->GetCPUDescriptorHandleForHeapStart());
    ao_w_ = w;
    ao_h_ = h;
}

bool Pass::Run(gpu::Context& ctx, const gpu::State& s, gpu::Target& rt, gpu::Target* ds, uint64_t frame,
               uint32_t scene_draws) {
    // once a frame, at the first draw testing depth without writing it after the world's, with a projection seen
    if (done_ == frame || !g_on || scene_draws < kSceneDraws) return false;
    if (!s.rs[gpu::kRsZEnable] || s.simple[gpu::kMZWriteEnable / 4]) return false;
    done_ = frame;
    if (!ds || !ds->res) {
        LOG_FIRST(3, "ssao: no depth target bound");
        return false;
    }
    float strength = g_strength.load();
    if (strength <= 0) return false;
    if (seen_ == ~0ull || frame - seen_ > 2) {
        LOG_FIRST(3, "ssao: no projection this frame (last %lld frames ago)",
                  seen_ == ~0ull ? -1ll : static_cast<long long>(frame - seen_));
        return false;
    }
    Ensure(ctx, std::max(rt.w / 2, 1u), std::max(rt.h / 2, 1u));
    if (!ao_) {
        LOG_FIRST(3, "ssao: no occlusion target (%ux%u)", rt.w / 2, rt.h / 2);
        return false;
    }
    const float p[8] = {a_, b_, sx_, sy_, kRadius, strength, kFadeEnd, 0};

    // the occlusion from the depth
    ctx.Transition(ds->res.Get(), ds->state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ctx.Transition(ao_.Get(), ao_state_, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12Resource* src1[3] = {ds->res.Get(), nullptr, nullptr};
    const DXGI_FORMAT fmt1[3] = {kDepthRead, kDepthRead, kDepthRead};
    ctx.Pass(kHlsl, "ao", kAoFormat, rtv_->GetCPUDescriptorHandleForHeapStart(), ao_w_, ao_h_, src1, fmt1, p, -1.0f);

    // into the picture
    ID3D12Resource* copy = ctx.Feedback(rt);
    if (copy) {
        ctx.Transition(ao_.Get(), ao_state_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        ctx.Transition(rt.res.Get(), rt.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        ID3D12Resource* src2[3] = {copy, ao_.Get(), ds->res.Get()};
        const DXGI_FORMAT fmt2[3] = {rt.fmt, kAoFormat, kDepthRead};
        ctx.Pass(kHlsl, "comp", rt.fmt, ctx.View(rt), rt.w, rt.h, src2, fmt2, p, -1.0f);
    }
    ctx.Transition(ds->res.Get(), ds->state, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    LOG_FIRST(1, "ssao: %ux%u, strength %.2f", ao_w_, ao_h_, strength);
    return true;
}

void Precompile() {
    for (const char* entry : {"ao", "comp"}) gpu::PrecompileShader(kHlsl, entry, "ps_5_0");
    gpu::PrecompileShader(kHlsl, "vs", "vs_5_0");
}

}  // namespace ssao
