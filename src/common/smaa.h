#pragma once
// Morphological antialiasing of the game's 3D picture: SMAA 1x (Jimenez et al., the reference shader in common/smaa,
// MIT), run once a frame on the back buffer at the render scale, where the sharpening runs (common/sharpen.h) and
// before it: luma edges, blending weights from the area and search tables, then the neighbours blended into the
// picture. The cheap alternative to a resolution scale over 100% (render_percent), or on top of it. Settings file:
// smaa, SMAA's preset (0 off; 1 low, 2 medium, 3 high, 4 ultra, the default).
#include <cstdint>

#include "gpu/ext.h"

namespace smaa {

extern uint32_t g_preset;

// the preset, 0 (off) to 4
uint32_t Preset();

// render thread, from the renderer extension (common/sharpen.cpp)
class Pass {
public:
    // on the back buffer at the HUD's first draw: true when it ran, the draw binds its targets again
    bool Run(gpu::Context& ctx, gpu::Target& rt);

private:
    bool Ensure(gpu::Context& ctx, uint32_t w, uint32_t h);
    bool Tables(gpu::Context& ctx);

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_;  // the edges', then the weights'
    Microsoft::WRL::ComPtr<ID3D12Resource> edges_, weights_, area_, search_;
    D3D12_RESOURCE_STATES edges_state_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES weights_state_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
    uint32_t w_ = 0, h_ = 0;
};

}  // namespace smaa
