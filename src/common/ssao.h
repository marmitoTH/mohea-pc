#pragma once
// Ambient occlusion of the game's 3D picture: a normal oriented, horizon style screen space AO from the depth buffer
// alone, at half the render resolution, then blurred by depth and multiplied into the picture. It runs once a frame
// where the solid world is done and before what is drawn over it (see-through things, the weapon in hand, the HUD):
// on the back buffer's first draw that tests depth without writing it, after the world's. The projection comes from
// the game's own constants (Capture). Settings file: ssao (on by default) and ssao_strength, 0 to 1 (0.5). F7
// switches it, shown in the corner.
#include "gpu/ext.h"

namespace ssao {

extern bool g_on;

// settings file: ssao_strength (ssao is a GameInfo option)
void Load(const void* table);
void Save(void* table);

// render thread, from the renderer extension (common/sharpen.cpp)
class Pass {
public:
    // each draw: its constants, for the camera's projection
    void Capture(const gpu::State& s, uint64_t frame);
    // each back buffer draw before the HUD: true when it ran, the draw binds its targets again
    bool Run(gpu::Context& ctx, const gpu::State& s, gpu::Target& rt, gpu::Target* ds, uint64_t frame,
             uint32_t scene_draws);

    // the HUD's first draw: logged once when no draw before it was the occlusion's
    void Hud(uint64_t frame);

private:
    void Ensure(gpu::Context& ctx, uint32_t w, uint32_t h);

    // the projection: z / w = a + b / w (a, b), and x and y's scale (sx, sy), with the frame they came from
    float a_ = 0, b_ = 0, sx_ = 0, sy_ = 0;
    uint64_t seen_ = ~0ull, done_ = ~0ull;
    // the occlusion, half the render size
    Microsoft::WRL::ComPtr<ID3D12Resource> ao_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_;
    D3D12_RESOURCE_STATES ao_state_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
    uint32_t ao_w_ = 0, ao_h_ = 0;
};

}  // namespace ssao
