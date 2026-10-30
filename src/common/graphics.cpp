// See graphics.h.
#include "common/graphics.h"

#include <cmath>
#include <optional>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "common/options.h"
#include "common/sharpen.h"
#include "common/smaa.h"
#include "common/ssao.h"

namespace graphics {

namespace {

// the settings a preset sets: the antialiasing (SSAA's samples and SMAA's preset), the anisotropic filtering, the
// colour, the sharpening, the ambient occlusion and the detail distance
struct Settings {
    uint32_t ssaa = 0, smaa = 0, aniso = 0;
    float gamma = 1, contrast = 1, saturation = 1;
    bool sharpen = false;
    float sharpness = 0;
    bool ssao = false;
    float ssao_strength = 0;
    uint32_t lod = 100;
};

Settings Now() {
    const app::Settings& s = app::Get();
    return {s.antialiasing, smaa::Preset(),      s.aniso,    s.gamma,          s.contrast, s.saturation,
            sharpen::g_on,  sharpen::Strength(), ssao::g_on, ssao::Strength(), LodDistance()};
}

void Set(const Settings& g) {
    app::Settings& s = app::Get();
    s.antialiasing = g.ssaa;
    smaa::g_preset = g.smaa;
    s.aniso = g.aniso;
    s.gamma = g.gamma;
    s.contrast = g.contrast;
    s.saturation = g.saturation;
    sharpen::g_on = g.sharpen;
    sharpen::SetStrength(g.sharpness);
    ssao::g_on = g.ssao;
    ssao::SetStrength(g.ssao_strength);
    g_lod_distance = g.lod;
    app::Save();
}

// the settings file keeps two decimals; a strength counts only while its effect is on
bool Near(float a, float b) { return std::fabs(a - b) < 0.006f; }
bool Same(const Settings& a, const Settings& b) {
    return a.ssaa == b.ssaa && a.smaa == b.smaa && a.aniso == b.aniso && Near(a.gamma, b.gamma) &&
           Near(a.contrast, b.contrast) && Near(a.saturation, b.saturation) && a.sharpen == b.sharpen &&
           (!a.sharpen || Near(a.sharpness, b.sharpness)) && a.ssao == b.ssao &&
           (!a.ssao || Near(a.ssao_strength, b.ssao_strength)) && a.lod == b.lod;
}

// Enhanced: the defaults, as they are before the settings file loads (the program's start, before main); Original
// keeps their strengths for when an effect is turned on again
Settings Defaults() {
    const app::Settings s;
    return {s.antialiasing, smaa::g_preset,      s.aniso,    s.gamma,          s.contrast, s.saturation,
            sharpen::g_on,  sharpen::Strength(), ssao::g_on, ssao::Strength(), g_lod_distance};
}
const Settings kEnhanced = Defaults();
const Settings kOriginal = {0, 0, 0, 1, 1, 1, false, kEnhanced.sharpness, false, kEnhanced.ssao_strength, 100};

std::optional<Settings> g_custom;

}  // namespace

Preset Current() {
    Settings now = Now();
    if (Same(now, kOriginal)) return Preset::Original;
    if (Same(now, kEnhanced)) return Preset::Enhanced;
    return Preset::Custom;
}

void Choose(Preset p) {
    if (Current() == Preset::Custom) g_custom = Now();
    if (p == Preset::Original) Set(kOriginal);
    else if (p == Preset::Enhanced) Set(kEnhanced);
    else if (g_custom) Set(*g_custom);
}

bool HasCustom() { return g_custom.has_value() || Current() == Preset::Custom; }

bool KeepCustom() {
    if (Current() != Preset::Custom) return false;
    Settings now = Now();
    if (g_custom && Same(*g_custom, now)) return false;
    g_custom = now;
    return true;
}

void Reset() { Set(kEnhanced); }

void Load(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    const toml::table* c = t["graphics_custom"].as_table();
    if (!c) return;
    Settings g = kEnhanced;
    const toml::table& v = *c;
    g.ssaa = v["antialiasing"].value_or(g.ssaa);
    g.smaa = v["smaa"].value_or(g.smaa);
    g.aniso = v["anisotropic_filtering"].value_or(g.aniso);
    g.gamma = float(v["gamma"].value_or(double(g.gamma)));
    g.contrast = float(v["contrast"].value_or(double(g.contrast)));
    g.saturation = float(v["saturation"].value_or(double(g.saturation)));
    g.sharpen = v["sharpening"].value_or(g.sharpen);
    g.sharpness = float(v["sharpness"].value_or(double(g.sharpness)));
    g.ssao = v["ssao"].value_or(g.ssao);
    g.ssao_strength = float(v["ssao_strength"].value_or(double(g.ssao_strength)));
    g.lod = v["lod_distance"].value_or(g.lod);
    g_custom = g;
}

void Save(void* table) {
    toml::table& t = *static_cast<toml::table*>(table);
    if (!g_custom) {
        t.erase("graphics_custom");
        return;
    }
    auto round = [](float f) { return std::round(double(f) * 100.0) / 100.0; };
    const Settings& g = *g_custom;
    t.insert_or_assign("graphics_custom",
                       toml::table{{"antialiasing", int64_t(g.ssaa)},
                                   {"smaa", int64_t(g.smaa)},
                                   {"anisotropic_filtering", int64_t(g.aniso)},
                                   {"gamma", round(g.gamma)},
                                   {"contrast", round(g.contrast)},
                                   {"saturation", round(g.saturation)},
                                   {"sharpening", g.sharpen},
                                   {"sharpness", round(g.sharpness)},
                                   {"ssao", g.ssao},
                                   {"ssao_strength", round(g.ssao_strength)},
                                   {"lod_distance", int64_t(g.lod)}});
}

}  // namespace graphics
