// See apt_rewrite.h.
#include "game/apt_rewrite.h"

#include <algorithm>

#include "core/log.h"
#include "xdk/hle.h"

namespace apt {
namespace {

uint32_t Crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & (0 - (c & 1)));
    }
    return ~c;
}

const std::vector<Rewrite>& Rewrites() {
    static const std::vector<Rewrite> rewrites = [] {
        std::vector<Rewrite> r = HelpBarRewrites();
        for (Rewrite& m : MenuRewrites()) r.push_back(std::move(m));
        return r;
    }();
    return rewrites;
}

// some code's Push and ConstantPool arrays: (capacity, offset in the .apt)
void OldArrays(uint8_t* base, uint32_t apt, Code c, std::vector<std::pair<uint32_t, uint32_t>>& arrays) {
    for (uint32_t a = apt + c.start; a < apt + c.start + c.size;) {
        uint8_t op = base[a];
        if (op < 0x80) {
            a++;
            continue;
        }
        uint32_t args = (a + 4) & ~3u;
        bool values = op == kPush || op == kConstantPool;
        if (values) arrays.emplace_back(U32(base, args), U32(base, args + 4));
        a = args + (values ? 8 : 4);
    }
}

// a block of a movie about to be linked: its code written anew, once (linked, it is no longer as it ships)
void Patch(uint8_t* base, uint32_t code, uint32_t apt, uint32_t consts_file) {
    if (std::memcmp(base + apt, "Apt Data:6", 10)) return;
    for (const Rewrite& r : Rewrites()) {
        if (std::find(r.triggers.begin(), r.triggers.end(), code - apt) == r.triggers.end()) continue;
        uint32_t first = r.code.front().first.start, last = first;
        for (auto& [c, write] : r.code) {
            first = std::min(first, c.start - (c.block ? 0 : kBody));
            last = std::max(last, c.start + c.size);
        }
        if (Crc32(base + apt + first, last - first) != r.crc) continue;
        Consts consts{base, consts_file, U32(base, consts_file + 0x1C), U32(base, consts_file + 0x18)};
        std::vector<const char*> needed;
        for (auto [from, to] : r.renamed) needed.push_back(from);
        for (auto [from, reg] : r.registers) needed.push_back(from);
        for (const char* from : needed)
            if (consts.Find(from) < 0) {
                LOGW("%s: no \"%s\" to rename, left as it is", r.name, from);
                return;
            }
        // the .const as it was, for a rewrite given up
        std::vector<std::pair<char*, std::string>> texts;
        std::vector<std::pair<uint32_t, uint64_t>> entries;
        auto undo = [&] {
            for (auto& [text, was] : texts) std::strcpy(text, was.c_str());
            for (auto [entry, was] : entries) std::memcpy(base + entry, &was, 8);
        };
        for (auto [from, to] : r.renamed) {
            uint32_t entry = consts.entries + 8 * uint32_t(consts.Find(from));
            char* text = reinterpret_cast<char*>(base + consts_file + U32(base, entry + 4));
            texts.emplace_back(text, text);
            std::strcpy(text, to);
        }
        for (auto [from, reg] : r.registers) {
            uint32_t entry = consts.entries + 8 * uint32_t(consts.Find(from));
            uint64_t was;
            std::memcpy(&was, base + entry, 8);
            entries.emplace_back(entry, was);
            U32(base, entry) = 4;
            U32(base, entry + 4) = uint32_t(reg);
        }
        std::vector<Writer> writers;
        std::vector<std::pair<uint32_t, uint32_t>> arrays;
        for (auto& [c, write] : r.code) {
            writers.push_back(Writer{consts, c, apt + c.start});
            write(writers.back());
            OldArrays(base, apt, c, arrays);
            const Writer& w = writers.back();
            if (!w.ok || w.code.size() > c.size) {
                LOGW("%s: not rewritten, the code at +%X %zu bytes of %u", r.name, c.start, w.code.size(), c.size);
                undo();
                return;
            }
        }
        // the old arrays, those side by side made one (each function's are), to the Pushes: the fullest Push first,
        // each to the start of the smallest room that holds it
        std::sort(arrays.begin(), arrays.end(), [](auto& a, auto& b) { return a.second < b.second; });
        std::vector<std::pair<uint32_t, uint32_t>> rooms;
        for (auto [n, offset] : arrays) {
            if (!n) continue;
            if (!rooms.empty() && rooms.back().second + 4 * rooms.back().first == offset) rooms.back().first += n;
            else rooms.emplace_back(n, offset);
        }
        std::vector<std::pair<Writer*, const Push*>> pushes;
        for (Writer& w : writers)
            for (const Push& p : w.pushes) pushes.emplace_back(&w, &p);
        std::stable_sort(pushes.begin(), pushes.end(),
                         [](auto& a, auto& b) { return a.second->entries.size() > b.second->entries.size(); });
        std::vector<uint32_t> array_of(pushes.size());
        for (size_t i = 0; i < pushes.size(); i++) {
            uint32_t n = uint32_t(pushes[i].second->entries.size());
            auto fit = rooms.end();
            for (auto it = rooms.begin(); it != rooms.end(); ++it)
                if (it->first >= n && (fit == rooms.end() || it->first < fit->first)) fit = it;
            if (fit == rooms.end()) {
                LOGW("%s: not rewritten, too little room for the Pushes' values", r.name);
                undo();
                return;
            }
            array_of[i] = fit->second;
            fit->first -= n;
            fit->second += 4 * n;
        }
        for (Writer& w : writers) {
            if (!w.where.block) w.code.resize(w.where.size, 0x02);
            std::memcpy(base + w.at, w.code.data(), w.code.size());
        }
        for (size_t i = 0; i < pushes.size(); i++) {
            auto [w, p] = pushes[i];
            U32(base, w->at + uint32_t(p->at)) = uint32_t(p->entries.size());
            U32(base, w->at + uint32_t(p->at) + 4) = array_of[i];
            for (size_t k = 0; k < p->entries.size(); k++)
                U32(base, apt + array_of[i] + 4 * uint32_t(k)) = p->entries[k];
        }
        LOGI("%s: rewritten (%zu Pushes)", r.name, pushes.size());
        return;
    }
}

}  // namespace
}  // namespace apt

// a block of a movie's bytecode linked
X86_EXTERN_FUNC(__imp__sub_00287830);
X86_FUNC(sub_00287830) {
    uint32_t code = apt::U32(base, ctx.esp + 4), movie = apt::U32(base, ctx.esp + 8);
    uint32_t consts = apt::U32(base, ctx.esp + 12);
    if (consts) apt::Patch(base, code, movie, consts);
    __imp__sub_00287830(ctx, base);
}
