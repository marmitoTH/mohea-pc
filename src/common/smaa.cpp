// See smaa.h.
#include "common/smaa.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "common/smaa/AreaTex.h"
#include "common/smaa/SearchTex.h"
#include "core/log.h"

namespace smaa {

uint32_t g_preset = 4;

uint32_t Preset() { return std::min(g_preset, 4u); }

namespace {

constexpr DXGI_FORMAT kEdges = DXGI_FORMAT_R8G8_UNORM, kWeights = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kArea = DXGI_FORMAT_R8G8_UNORM, kSearch = DXGI_FORMAT_R8_UNORM;

// The reference shader (common/smaa/SMAA.hlsl.inc: SMAA.hlsl of github.com/iryoku/smaa at 71c806a, re-encoded as UTF-8)
// in its custom language mode on the passes' samplers (s0 linear, s1 point), at a preset defined ahead of this. The
// passes' vertex shader gives only the position, so each pixel shader works out its texture coordinate and the offsets
// SMAA's own vertex shaders would have: p is the size (1 / w, 1 / h, w, h).
const char* const kPresets[] = {"", "#define SMAA_PRESET_LOW 1\n", "#define SMAA_PRESET_MEDIUM 1\n",
                                "#define SMAA_PRESET_HIGH 1\n", "#define SMAA_PRESET_ULTRA 1\n"};

const char* const kPrelude = R"(
cbuffer B : register(b1) { float4 p; float4 q; };
#define SMAA_RT_METRICS p
#define SMAA_CUSTOM_SL 1
SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);
#define SMAATexture2D(tex) Texture2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(LinearSampler, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(PointSampler, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(LinearSampler, coord, 0, offset)
#define SMAASample(tex, coord) tex.Sample(LinearSampler, coord)
#define SMAASamplePoint(tex, coord) tex.Sample(PointSampler, coord)
#define SMAASampleOffset(tex, coord, offset) tex.Sample(LinearSampler, coord, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]
#define SMAAGather(tex, coord) tex.Gather(LinearSampler, coord, 0)
)";

const char* const kSmaa =
#include "common/smaa/SMAA.hlsl.inc"
    ;

const char* const kPasses = R"(
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 t = float2((id << 1) & 2, id & 2);
    return float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 edges(float4 pos : SV_Position) : SV_Target {
    float2 uv = pos.xy * p.xy;
    float4 o[3];
    SMAAEdgeDetectionVS(uv, o);
    return float4(SMAALumaEdgeDetectionPS(uv, o, tex0), 0, 0);
}
float4 weights(float4 pos : SV_Position) : SV_Target {
    float2 uv = pos.xy * p.xy, pix;
    float4 o[3];
    SMAABlendingWeightCalculationVS(uv, pix, o);
    return SMAABlendingWeightCalculationPS(uv, pix, o, tex0, tex1, tex2, float4(0, 0, 0, 0));
}
float4 blend(float4 pos : SV_Position) : SV_Target {
    float2 uv = pos.xy * p.xy;
    float4 o;
    SMAANeighborhoodBlendingVS(uv, o);
    return SMAANeighborhoodBlendingPS(uv, o, tex0, tex1);
}
)";

// the whole source at a preset, 1 to 4, kept for the renderer's pipeline cache
const char* Hlsl(uint32_t preset) {
    static const std::string s[5] = {
        "",
        std::string(kPresets[1]) + kPrelude + kSmaa + kPasses,
        std::string(kPresets[2]) + kPrelude + kSmaa + kPasses,
        std::string(kPresets[3]) + kPrelude + kSmaa + kPasses,
        std::string(kPresets[4]) + kPrelude + kSmaa + kPasses,
    };
    return s[preset].c_str();
}

Microsoft::WRL::ComPtr<ID3D12Resource> Texture(ID3D12Device* dev, DXGI_FORMAT fmt, uint32_t w, uint32_t h,
                                               D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = fmt;
    rd.SampleDesc.Count = 1;
    rd.Flags = flags;
    Microsoft::WRL::ComPtr<ID3D12Resource> r;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr, IID_PPV_ARGS(&r))))
        return nullptr;
    return r;
}

}  // namespace

// the area and search tables, uploaded once
bool Pass::Tables(gpu::Context& ctx) {
    if (area_ && search_) return true;
    ID3D12Device* dev = ctx.Device();
    struct Table {
        Microsoft::WRL::ComPtr<ID3D12Resource>& tex;
        DXGI_FORMAT fmt;
        uint32_t w, h, pitch;
        const unsigned char* bytes;
    } tables[2] = {{area_, kArea, AREATEX_WIDTH, AREATEX_HEIGHT, AREATEX_PITCH, areaTexBytes},
                   {search_, kSearch, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, SEARCHTEX_PITCH, searchTexBytes}};
    for (Table& t : tables) {
        t.tex = Texture(dev, t.fmt, t.w, t.h, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        if (!t.tex) return false;
        uint32_t rows = (t.pitch + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = uint64_t(rows) * t.h;
        bd.Height = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Microsoft::WRL::ComPtr<ID3D12Resource> up;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr, IID_PPV_ARGS(&up))))
            return false;
        uint8_t* dst = nullptr;
        if (FAILED(up->Map(0, nullptr, reinterpret_cast<void**>(&dst)))) return false;
        for (uint32_t y = 0; y < t.h; y++) std::memcpy(dst + size_t(y) * rows, t.bytes + size_t(y) * t.pitch, t.pitch);
        up->Unmap(0, nullptr);
        D3D12_TEXTURE_COPY_LOCATION to{t.tex.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        to.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION from{up.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        from.PlacedFootprint.Footprint = {t.fmt, t.w, t.h, 1, rows};
        ctx.List()->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
        ctx.Transition(t.tex.Get(), state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        ctx.Release(up);
    }
    return true;
}

// the edges and the weights at the picture's size
bool Pass::Ensure(gpu::Context& ctx, uint32_t w, uint32_t h) {
    if (edges_ && weights_ && w_ == w && h_ == h) return true;
    if (edges_) ctx.Release(edges_);
    if (weights_) ctx.Release(weights_);
    edges_.Reset();
    weights_.Reset();
    ID3D12Device* dev = ctx.Device();
    if (!rtv_) {
        D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2};
        if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv_)))) return false;
    }
    edges_state_ = weights_state_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
    edges_ = Texture(dev, kEdges, w, h, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, edges_state_);
    weights_ = Texture(dev, kWeights, w, h, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, weights_state_);
    if (!edges_ || !weights_) {
        edges_.Reset();
        weights_.Reset();
        return false;
    }
    UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE h0 = rtv_->GetCPUDescriptorHandleForHeapStart(), h1{h0.ptr + inc};
    dev->CreateRenderTargetView(edges_.Get(), nullptr, h0);
    dev->CreateRenderTargetView(weights_.Get(), nullptr, h1);
    w_ = w;
    h_ = h;
    return true;
}

bool Pass::Run(gpu::Context& ctx, gpu::Target& rt) {
    uint32_t preset = Preset();
    if (!preset || !rt.res) return false;
    if (!Tables(ctx) || !Ensure(ctx, rt.w, rt.h)) {
        LOG_FIRST(3, "smaa: no tables or targets (%ux%u)", rt.w, rt.h);
        return false;
    }
    ID3D12Resource* color = ctx.Feedback(rt);
    if (!color) return false;
    ID3D12Device* dev = ctx.Device();
    UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE edges_rtv = rtv_->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE weights_rtv{edges_rtv.ptr + inc};
    const float p[8] = {1.0f / float(rt.w), 1.0f / float(rt.h), float(rt.w), float(rt.h)};
    const char* hlsl = Hlsl(preset);

    // the edges, where the pass writes only edges
    ctx.Transition(edges_.Get(), edges_state_, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const float none[4] = {0, 0, 0, 0};
    ctx.List()->ClearRenderTargetView(edges_rtv, none, 0, nullptr);
    ID3D12Resource* src1[3] = {color, nullptr, nullptr};
    const DXGI_FORMAT fmt1[3] = {rt.fmt, rt.fmt, rt.fmt};
    ctx.Pass(hlsl, "edges", kEdges, edges_rtv, rt.w, rt.h, src1, fmt1, p, -1.0f);

    // the blending weights
    ctx.Transition(edges_.Get(), edges_state_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ctx.Transition(weights_.Get(), weights_state_, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12Resource* src2[3] = {edges_.Get(), area_.Get(), search_.Get()};
    const DXGI_FORMAT fmt2[3] = {kEdges, kArea, kSearch};
    ctx.Pass(hlsl, "weights", kWeights, weights_rtv, rt.w, rt.h, src2, fmt2, p, -1.0f);

    // the neighbours blended into the picture
    ctx.Transition(weights_.Get(), weights_state_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ctx.Transition(rt.res.Get(), rt.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12Resource* src3[3] = {color, weights_.Get(), nullptr};
    const DXGI_FORMAT fmt3[3] = {rt.fmt, kWeights, kWeights};
    ctx.Pass(hlsl, "blend", rt.fmt, ctx.View(rt), rt.w, rt.h, src3, fmt3, p, -1.0f);
    LOG_FIRST(1, "smaa: %ux%u, preset %u", rt.w, rt.h, preset);
    return true;
}

}  // namespace smaa
