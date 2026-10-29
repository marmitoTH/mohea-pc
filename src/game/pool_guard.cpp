// Tests (the settings file's pool_guard: on by default in dev and debug builds, off in release; read once, at the
// first pool): the game's fixed-size pools checked at each block handed out and taken back, so a corruption shows
// where it happens rather than when something later trips over it (CRASHES.md, the 2026-10-28 crash). It costs a
// lock, a set's update and a look up the guest stack at each.
//
// A pool (vtable 0x3082A4; set up by sub_001567B0 (blocks a chunk, block size), torn down by sub_00156800) keeps its
// blocks in chunks from the heap: +0x04 the blocks in use, +0x08 the blocks it has, +0x0C blocks a chunk, +0x10 a
// block's size, +0x14 its chunks (each links the next at +0, its blocks from +0x10), +0x18 the free list (each free
// block links the next at +0). sub_001569E0 (vtable +4) hands out the free list's head, adding a chunk when none is
// left (sub_00156840); sub_00156550 (vtable +8, stdcall (block)) puts a block back at the head and, when no block is
// in use any more, gives every chunk back to the heap. A block taken back twice makes the count reach 0 too early:
// the chunks go back to the heap under blocks still in use.
//
// Here each pool's live blocks are kept beside it. Logged as errors, a few times a pool, each with the pool's last
// hand-outs and returns (guest thread, block, the guest return addresses found on the stack):
// - a block taken back that is not live (taken back twice, or another pool's), or outside the pool's chunks;
// - a block handed out that is already live, or a free list head outside the pool's chunks (the list is corrupt);
// - the count reaching 0 with blocks still live, or the count and the live blocks disagreeing;
// and once a pool, as a report, the first time a second guest thread uses it.
#include <algorithm>
#include <array>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

#ifdef NDEBUG
bool g_pool_guard = false;
#else
bool g_pool_guard = true;
#endif

namespace {

// the setting as the game started: switched on later, the blocks handed out before would all look foreign
bool On() {
    static const bool on = [] {
        if (g_pool_guard) LOGR("pool guard: on");
        return g_pool_guard;
    }();
    return on;
}

constexpr int kHistory = 64, kChain = 4, kReports = 3;

// the game's code, where its return addresses fall (.text)
constexpr uint32_t kCodeStart = 0x11000, kCodeEnd = 0x2D826C;

struct Op {
    bool back;  // taken back, else handed out
    uint32_t block, thread, in_use;
    uint32_t chain[kChain];
};

struct Pool {
    std::unordered_set<uint32_t> live;
    std::array<Op, kHistory> ops{};
    size_t count = 0;
    uint32_t thread = 0;
    bool shared = false;
    int reports = 0;
};

std::mutex g_lock;  // pools are used from several guest threads
std::unordered_map<uint32_t, Pool> g_pools;

template <typename T>
T& At(uint8_t* base, uint32_t address) {
    return *reinterpret_cast<T*>(base + address);
}

uint32_t Thread() {
    kernel::Thread* t = kernel::Current();
    return t ? t->id : 0;
}

// a return address: in the code, after a call (E8 rel32, or FF /2 through a register or memory, 2, 3 or 6 bytes)
bool Return(uint8_t* base, uint32_t a) {
    if (a < kCodeStart || a >= kCodeEnd) return false;
    return base[a - 5] == 0xE8 || base[a - 2] == 0xFF || base[a - 3] == 0xFF || base[a - 6] == 0xFF;
}

// the hooked function's caller, then the next return addresses up the guest stack
void Chain(X86Context& ctx, uint8_t* base, uint32_t* chain) {
    int n = 0;
    chain[n++] = At<uint32_t>(base, ctx.esp);
    for (uint32_t at = ctx.esp + 4; n < kChain && at < ctx.esp + 0x400; at += 4) {
        uint32_t a = At<uint32_t>(base, at);
        if (Return(base, a)) chain[n++] = a;
    }
    while (n < kChain) chain[n++] = 0;
}

// the block lies on a block of one of the pool's chunks
bool InChunks(uint8_t* base, uint32_t pool, uint32_t block) {
    uint32_t blocks = At<uint32_t>(base, pool + 0x0C), size = At<uint32_t>(base, pool + 0x10);
    uint32_t chunk = At<uint32_t>(base, pool + 0x14);
    for (int i = 0; chunk && i < 4096; i++, chunk = At<uint32_t>(base, chunk)) {
        uint32_t first = chunk + 0x10;
        if (block >= first && block < first + blocks * size) return size && (block - first) % size == 0;
    }
    return false;
}

void Record(Pool& p, uint8_t* base, uint32_t pool, bool back, uint32_t block, const uint32_t* chain) {
    Op& op = p.ops[p.count++ % kHistory];
    op.back = back;
    op.block = block;
    op.thread = Thread();
    op.in_use = At<uint32_t>(base, pool + 0x04);
    std::copy(chain, chain + kChain, op.chain);
    if (!p.thread) p.thread = op.thread;
    if (op.thread != p.thread && !p.shared) {
        p.shared = true;
        LOGR("pool %08X: used by guest thread %X too, first by %X (from %08X %08X %08X %08X)", pool, op.thread,
             p.thread, op.chain[0], op.chain[1], op.chain[2], op.chain[3]);
    }
}

std::string Line(const Op& op) {
    char line[128];
    std::snprintf(line, sizeof(line), "%s %08X thread %X in use %u from %08X %08X %08X %08X",
                  op.back ? "back" : "out ", op.block, op.thread, op.in_use, op.chain[0], op.chain[1], op.chain[2],
                  op.chain[3]);
    return line;
}

// the problem, then the pool's last operations, oldest first; this one's last
void Report(Pool& p, uint8_t* base, uint32_t pool, const char* what, uint32_t block) {
    if (p.reports++ >= kReports) return;
    LOGE("pool %08X: %s: block %08X (blocks %u of %u in use, %zu live, %u a chunk of %u bytes, free list %08X)", pool,
         what, block, At<uint32_t>(base, pool + 0x04), At<uint32_t>(base, pool + 0x08), p.live.size(),
         At<uint32_t>(base, pool + 0x0C), At<uint32_t>(base, pool + 0x10), At<uint32_t>(base, pool + 0x18));
    size_t n = std::min<size_t>(p.count, kHistory);
    for (size_t i = p.count - n; i < p.count; i++) LOGE("pool %08X:   %s", pool, Line(p.ops[i % kHistory]).c_str());
}

}  // namespace

// set up (stdcall (blocks a chunk, block size) on the pool): a pool new at this address
X86_EXTERN_FUNC(__imp__sub_001567B0);
X86_FUNC(sub_001567B0) {
    if (!On()) return __imp__sub_001567B0(ctx, base);
    {
        std::lock_guard lock(g_lock);
        g_pools.erase(ctx.ecx);
    }
    __imp__sub_001567B0(ctx, base);
}

// torn down (on the pool): its chunks go back to the heap
X86_EXTERN_FUNC(__imp__sub_00156800);
X86_FUNC(sub_00156800) {
    if (!On()) return __imp__sub_00156800(ctx, base);
    {
        std::lock_guard lock(g_lock);
        g_pools.erase(ctx.ecx);
    }
    __imp__sub_00156800(ctx, base);
}

// The lock is not held while the game's code runs: a new chunk or a chunk given back goes through the heap, which may
// wait on another guest thread.

// a block handed out (on the pool, the block in eax)
X86_EXTERN_FUNC(__imp__sub_001569E0);
X86_FUNC(sub_001569E0) {
    if (!On()) return __imp__sub_001569E0(ctx, base);
    uint32_t pool = ctx.ecx;
    uint32_t chain[kChain];
    Chain(ctx, base, chain);
    {
        // the head the original takes next, checked before it would fault on a corrupt one
        std::lock_guard lock(g_lock);
        uint32_t head = At<uint32_t>(base, pool + 0x18);
        if (head && !InChunks(base, pool, head)) {
            Pool& p = g_pools[pool];
            Record(p, base, pool, false, head, chain);
            Report(p, base, pool, "free list head outside the pool", head);
        }
    }
    __imp__sub_001569E0(ctx, base);
    std::lock_guard lock(g_lock);
    Pool& p = g_pools[pool];
    uint32_t block = ctx.eax;
    Record(p, base, pool, false, block, chain);
    if (!p.live.insert(block).second) Report(p, base, pool, "handed out while live", block);
}

// a block taken back (stdcall (block) on the pool)
X86_EXTERN_FUNC(__imp__sub_00156550);
X86_FUNC(sub_00156550) {
    if (!On()) return __imp__sub_00156550(ctx, base);
    uint32_t pool = ctx.ecx, block = At<uint32_t>(base, ctx.esp + 4);
    uint32_t chain[kChain];
    Chain(ctx, base, chain);
    uint32_t in_use = At<uint32_t>(base, pool + 0x04);
    {
        std::lock_guard lock(g_lock);
        Pool& p = g_pools[pool];
        Record(p, base, pool, true, block, chain);
        if (!p.live.erase(block))
            Report(p, base, pool,
                   InChunks(base, pool, block) ? "taken back but not live (twice?)" : "taken back from outside the pool",
                   block);
        else if (in_use != p.live.size() + 1)
            Report(p, base, pool, "the count of blocks in use and the live blocks disagree", block);
        if (in_use == 1 && !p.live.empty())
            Report(p, base, pool, "no block in use by the count: the chunks go back to the heap under live blocks",
                   block);
        if (in_use == 1) p.live.clear();  // the pool is empty after this, as the game has it
    }
    __imp__sub_00156550(ctx, base);
}

