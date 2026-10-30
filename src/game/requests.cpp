// A pending request taken out of its list twice (CRASHES.md, 2026-10-28: the pool at 0x3A65A8 corrupted). A request
// list (vtable 0x302800, made by sub_0003CD90; probably sounds) keeps its requests, 0x28-byte blocks of the pool at
// 0x3A65A8, linked at their +0x20 (next, then previous) behind a head at the list's +0x1C, their count at +0x14.
// sub_0003CD00 (vtable +4, stdcall (request)) takes a request out and frees it: when it fails to start, on event 0x08
// (likely stopped, sub_0003C860) and in its callback on event 0x02 (done, sub_0003C6F0). A request stopped and then
// done was taken out twice and freed twice, which corrupted the pool. Here one no longer in its list is left alone.
// Should its memory already hold a new request of the same list, that one is taken out early (a sound cut short).
#include <atomic>

#include "core/log.h"
#include "xdk/hle.h"

namespace {

uint32_t At(uint8_t* base, uint32_t address) {
    return *reinterpret_cast<uint32_t*>(base + address);
}

// the request's link is among the list's
bool Listed(uint8_t* base, uint32_t list, uint32_t request) {
    uint32_t head = list + 0x1C, link = request + 0x20;
    uint32_t n = At(base, head);
    for (int i = 0; n && n != head && i < 4096; i++, n = At(base, n))
        if (n == link) return true;
    return false;
}

}  // namespace

X86_EXTERN_FUNC(__imp__sub_0003CD00);
X86_FUNC(sub_0003CD00) {
    uint32_t list = ctx.ecx, request = At(base, ctx.esp + 4);
    if (request && !Listed(base, list, request)) {
        static std::atomic<uint32_t> skipped{0};
        uint32_t n = ++skipped;
        if (n <= 8 || n % 100 == 0)
            LOGR("requests: request %08X taken out of list %08X again (from %08X), left alone (%u so far)", request,
                 list, At(base, ctx.esp), n);
        ctx.esp += 8;  // ret 4
        return;
    }
    __imp__sub_0003CD00(ctx, base);
}
