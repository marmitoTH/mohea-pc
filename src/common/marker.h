#pragma once
// The squad's markers hidden (the settings file's hide_squad_marker, off by default): the green star over each squad
// member, which the game tints by the member's health and drops when the member dies. It is drawn in the 3D world, not
// by the HUD (hiding the HUD leaves it), from a texture of its own: a white star in a ring, 32x32 P8. Here that
// texture, known by a hash of its decoded pixels, is tagged when the renderer decodes it, and the pixel shaders
// sampling it discard every pixel while the option is on (a draw constant, so it switches at once).
//
// Another texture's hash (dev and debug builds): with the settings file's dump_textures on, each P8 texture decoded is
// logged once with its hash; or computed from its dump (textures\dumps\WxH_<key>_P8.png) as Hash has it, the PNG's
// RGBA turned to BGRA.
#include <cstdint>
#include <string>
#include <vector>

#include "gpu/ext.h"

namespace marker {

extern bool g_hide;

// from the renderer extension (common/sharpen.cpp), on the render thread
void Decoded(const gpu::TextureDesc& d, const gpu::FormatInfo& fi, uint32_t face, uint32_t level,
             const std::vector<uint8_t>& px, uint32_t pitch, uint8_t& flags);
void Sampled(uint8_t flags, gpu::TexInfo& info);
void Constants(float* ext);

// the shader code the tag adds
const gpu::ShaderHooks* Shaders();

}  // namespace marker
