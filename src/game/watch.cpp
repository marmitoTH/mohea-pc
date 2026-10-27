// Tests (MOHEA_WATCH=actions): who reads the actions the right stick drives. The input manager (0x3C6600) keeps a
// controller every 0xBB4 bytes; each frame sub_000C7280 walks the bindings at [manager + 0x2ED4], (action, control)
// word pairs up to an action of 0xFFFF, and writes each action to the controller's action array (controller + 0x7C,
// 12 bytes an action: flags at +0, an analog action's value at +8, -127..127 for a stick, +4 only for buttons).
// Controls 0x12 and 0x13 are the right stick's x and y (the controller's bytes +0x14 and +0x15). The controller update
// sub_000C5330 checks the bindings about once a second: a new table is logged and data breakpoints go on the values
// of the actions bound to 0x12 or 0x13 (four at most), in every thread. Each access is counted by its call chain (the
// host frames above it, unwound from the exception), logged as module + offset: llvm-symbolizer on the dev build's
// MOHEA.dll gives the generated line, which names the guest instruction; or the DLL's X86FuncMappings table gives the
// guest function. This found the player control sub_00120C70, which passes the look on to sub_00114950
// (game/controls.cpp).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "core/log.h"
#include "xdk/hle.h"

namespace {

// a hit: the instruction after the access and the callers above it, host addresses
constexpr int kDepth = 6, kChains = 256;
struct Chain {
    std::atomic<uintptr_t> key{0};
    uintptr_t frames[kDepth] = {};
    int controller = 0;
    std::atomic<uint32_t> hits{0};
};
Chain g_chains[kChains];
std::atomic<bool> g_armed{false};

// the faulting frame and its callers, unwound from the exception's context
int Walk(const CONTEXT& from, uintptr_t* out) {
    CONTEXT c = from;
    int n = 0;
    while (n < kDepth && c.Rip) {
        out[n++] = c.Rip;
        DWORD64 image = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &image, nullptr);
        if (!fn) break;
        void* handler = nullptr;
        DWORD64 frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, c.Rip, fn, &c, &handler, &frame, nullptr);
    }
    return n;
}

LONG CALLBACK OnStep(EXCEPTION_POINTERS* e) {
    DWORD64 dr6 = e->ContextRecord->Dr6;
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(dr6 & 0xF)) return EXCEPTION_CONTINUE_SEARCH;
    int controller = 0;
    while (!(dr6 & (1u << controller))) controller++;
    uintptr_t frames[kDepth] = {};
    Walk(*e->ContextRecord, frames);
    uintptr_t key = controller + 1;
    for (uintptr_t f : frames) key = key * 1000003u ^ f;
    // the first free slot takes a new chain; its frames follow the key, the report reads them seconds later
    for (Chain& c : g_chains) {
        uintptr_t seen = 0;
        if (c.key.compare_exchange_strong(seen, key)) {
            std::memcpy(c.frames, frames, sizeof(frames));
            c.controller = controller;
        } else if (seen != key) {
            continue;
        }
        c.hits++;
        break;
    }
    e->ContextRecord->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

std::string Where(uintptr_t ip) {
    HMODULE mod = nullptr;
    char name[MAX_PATH] = "?";
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(ip), &mod))
        return "?";
    GetModuleFileNameA(mod, name, sizeof(name));
    const char* file = std::strrchr(name, '\\');
    char buf[MAX_PATH + 32];
    std::snprintf(buf, sizeof(buf), "%s+0x%llX", file ? file + 1 : name,
                  static_cast<unsigned long long>(ip - reinterpret_cast<uintptr_t>(mod)));
    return buf;
}

// new chains as they appear, then every chain's count every 30 s
void Report() {
    bool logged[kChains] = {};
    for (int tick = 1;; tick++) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        for (int i = 0; i < kChains; i++) {
            Chain& c = g_chains[i];
            if (!c.key.load() || logged[i]) continue;
            logged[i] = true;
            std::string text;
            for (uintptr_t f : c.frames)
                if (f) text += " " + Where(f);
            LOGR("watch: chain %d breakpoint %d:%s", i, c.controller, text.c_str());
        }
        if (tick % 10) continue;
        std::string counts;
        for (int i = 0; i < kChains; i++)
            if (g_chains[i].key.load()) counts += " " + std::to_string(i) + "=" + std::to_string(g_chains[i].hits.load());
        LOGR("watch: hits%s", counts.c_str());
    }
}

// every thread but this one, suspended while its debug registers change: up to four 4-byte values, any access
void Arm(std::vector<uintptr_t> at) {
    std::thread([at] {
        DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 te{sizeof(te)};
        int armed = 0;
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
            HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, te.th32ThreadID);
            if (!t) continue;
            if (SuspendThread(t) != DWORD(-1)) {
                CONTEXT c{};
                c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (GetThreadContext(t, &c)) {
                    DWORD64* dr[4] = {&c.Dr0, &c.Dr1, &c.Dr2, &c.Dr3};
                    c.Dr7 = 0;
                    for (size_t i = 0; i < 4; i++) {
                        *dr[i] = i < at.size() ? at[i] : 0;
                        // on, read or write, 4 bytes
                        if (i < at.size()) c.Dr7 |= (1ull << (i * 2)) | (3ull << (16 + i * 4)) | (3ull << (18 + i * 4));
                    }
                    if (SetThreadContext(t, &c)) armed++;
                }
                ResumeThread(t);
            }
            CloseHandle(t);
        }
        CloseHandle(snap);
        LOGR("watch: %zu data breakpoints in %d threads", at.size(), armed);
    }).join();
}

constexpr uint32_t kManager = 0x3C6600, kBindings = kManager + 0x2ED4, kActions = kManager + 0x7C;

// the bindings when they change: each (action, control) pair, and the right stick's actions watched
void CheckBindings(uint8_t* base) {
    static uint32_t last = 0;
    static uint64_t last_sum = 0;
    uint32_t table = *reinterpret_cast<uint32_t*>(base + kBindings);
    if (!table) return;
    std::string pairs;
    std::vector<uintptr_t> watch;
    uint64_t sum = table;
    for (uint32_t at = table;; at += 4) {
        uint16_t action = *reinterpret_cast<uint16_t*>(base + at), control = *reinterpret_cast<uint16_t*>(base + at + 2);
        if (action == 0xFFFF || at - table > 0x1000) break;
        sum = sum * 31 + (uint32_t(action) << 16 | control);
        char buf[32];
        std::snprintf(buf, sizeof(buf), " %X=%X", action, control);
        pairs += buf;
        if ((control == 0x12 || control == 0x13) && watch.size() < 4)
            watch.push_back(reinterpret_cast<uintptr_t>(base) + kActions + action * 12u + 8);
    }
    if (table == last && sum == last_sum) return;
    last = table;
    last_sum = sum;
    LOGR("watch: bindings at %08X (action=control):%s", table, pairs.c_str());
    if (watch.empty()) return;
    std::string list;
    for (uintptr_t w : watch) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), " action %llX", static_cast<unsigned long long>((w - reinterpret_cast<uintptr_t>(base) - kActions - 8) / 12));
        list += buf;
    }
    LOGR("watch: right stick actions:%s", list.c_str());
    Arm(watch);
}

// MOHEA_WATCH=actions
std::string Mode() {
    static const std::string mode = [] {
        const char* v = std::getenv("MOHEA_WATCH");
        return std::string(v ? v : "");
    }();
    return mode;
}

void Start() {
    if (!g_armed.exchange(true)) {
        AddVectoredExceptionHandler(1, OnStep);
        std::thread(Report).detach();
    }
}

}  // namespace

X86_EXTERN_FUNC(__imp__sub_000C5330);
X86_FUNC(sub_000C5330) {
    __imp__sub_000C5330(ctx, base);
    if (Mode() != "actions") return;
    Start();
    using clock = std::chrono::steady_clock;
    static clock::time_point next{};
    if (clock::now() < next) return;
    next = clock::now() + std::chrono::seconds(1);
    CheckBindings(base);
}
