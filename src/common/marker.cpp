// See marker.h.
#include "common/marker.h"

#include <algorithm>
#include <iterator>
#include <mutex>
#include <unordered_set>

#include "app/app.h"
#include "core/log.h"

namespace marker {

bool g_hide = false;

namespace {

// the tag, TexInfo::flags bit 0 (bits 0 and 1 are an extension's)
constexpr uint8_t kTag = 1;

// the marker's texture by Hash: a white star in a ring, 32x32 P8 (its dump 32x32_b4067f1ed0f98b72_P8.png), drawn
// tinted (green, and the member's health)
constexpr uint32_t kFmtP8 = 0x0B;
constexpr uint64_t kMarkers[] = {0x4E7BD70919DB6E4Eull};

// FNV-1a over the format, the size and level 0's decoded pixels, rows of width * 4 bytes (BGRA)
uint64_t Hash(const gpu::TextureDesc& d, const std::vector<uint8_t>& px, uint32_t pitch) {
    uint64_t h = 0xCBF29CE484222325ull;
    auto mix = [&](const uint8_t* p, size_t n) {
        for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    };
    const uint32_t shape[] = {d.format, d.width, d.height};
    mix(reinterpret_cast<const uint8_t*>(shape), sizeof(shape));
    size_t row = size_t(d.width) * 4;
    for (uint32_t y = 0; y < d.height && size_t(y) * pitch + row <= px.size(); y++) mix(px.data() + size_t(y) * pitch, row);
    return h;
}

class Hooks : public gpu::ShaderHooks {
public:
    std::string Fetched(const gpu::TexInfo& t, int) const override {
        return t.flags & kTag ? "    if (ext.x > 0.5) discard;\n" : "";
    }
};

}  // namespace

void Decoded(const gpu::TextureDesc& d, const gpu::FormatInfo&, uint32_t face, uint32_t level,
             const std::vector<uint8_t>& px, uint32_t pitch, uint8_t& flags) {
    // a 4 byte texel decode (the marker's P8 is BGRA by now)
    if (face || level || d.cube || d.depth > 1 || d.linear || d.format != kFmtP8) return;
    if (pitch < d.width * 4) return;
    uint64_t h = Hash(d, px, pitch);
    if (std::find(std::begin(kMarkers), std::end(kMarkers), h) != std::end(kMarkers)) {
        flags |= kTag;
        LOG_FIRST(2, "marker: the squad marker's texture (%ux%u) tagged", d.width, d.height);
    }
#ifndef NDEBUG
    // with the dumps made: each P8 texture's hash, to find another marker by its dump
    if (app::Get().dump_textures) {
        static std::mutex lock;
        static std::unordered_set<uint64_t> seen;
        std::lock_guard l(lock);
        if (seen.insert(h).second)
            LOGI("marker: P8 texture %ux%u at %08X, hash %016llx", d.width, d.height, d.data,
                 static_cast<unsigned long long>(h));
    }
#endif
}

void Sampled(uint8_t flags, gpu::TexInfo& info) { info.flags |= flags & kTag; }

void Constants(float* ext) { ext[0] = g_hide ? 1.0f : 0.0f; }

const gpu::ShaderHooks* Shaders() {
    static const Hooks hooks;
    return &hooks;
}

}  // namespace marker
