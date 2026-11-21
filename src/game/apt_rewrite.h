#pragma once
// The menus' Apt movies (EA's Flash: DATA/SHARED/UI/FE/mohfe.viv) have some of their bytecode written anew before it
// is linked (sub_00287830, cdecl (code, the .apt, the .const, counter), once for each block of actions: a frame's or a
// sprite's init actions, the functions defined in it inline). Each movie's rewrite is checked against the code as it
// ships first, and left out when it differs. The rewrites: the help bar's and the dialogs' buttons on the keys
// (game/helpbar.cpp) and the menus' mouse (game/menus.cpp).
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace apt {

inline uint32_t& U32(uint8_t* base, uint32_t a) { return *reinterpret_cast<uint32_t*>(base + a); }

// Code written anew, at its offset in the .apt: a function's body (after its DefineFunction, kBody long), its rest
// made 1 byte actions never run (the linker stops at an End, 0), or a block of actions, an End after it
struct Code {
    uint32_t start, size;
    bool block;
};
constexpr uint32_t kBody = 0x1C;

// The .const: entries (type, value) at +0x1C (a pointer by now), their count at +0x18; type 1 a string (its offset in
// the .const), 3 undefined, 4 a register's value (its number), 5 a boolean, 6 a number (0 its only one here), 7 an
// integer
struct Consts {
    uint8_t* base;
    uint32_t file, entries, count;
    int Find(uint32_t type, uint32_t value) const {
        for (uint32_t i = 0; i < count; i++)
            if (U32(base, entries + 8 * i) == type && U32(base, entries + 8 * i + 4) == value) return int(i);
        return -1;
    }
    int Find(const char* s) const {
        for (uint32_t i = 0; i < count; i++)
            if (U32(base, entries + 8 * i) == 1 &&
                !std::strcmp(reinterpret_cast<const char*>(base + file + U32(base, entries + 8 * i + 4)), s))
                return int(i);
        return -1;
    }
};

// Apt bytecode, Flash's actions: a Push's values are its count and an array of .const entries, which the linker makes
// the values themselves, so each Push has an array of its own: room in the old code's, reused. A branch's offset is
// from its end. With merge a Push right after a Push adds its values to it, the code smaller, unless a branch lands
// between them (Land, or Mark for a place to land on later)
struct Undefined {};
struct Bool {
    bool value;
};
struct Reg {  // a register's value (StoreRegister sets it); 0 to 3
    int n;
};
using Value = std::variant<const char*, int, Bool, Undefined, Reg>;
struct Push {
    size_t at;  // in the code
    std::vector<int> entries;
};
enum Op : uint8_t {
    kEnd = 0x00, kSub = 0x0B, kMultiply = 0x0C, kDivide = 0x0D, kAnd = 0x10, kOr = 0x11, kNot = 0x12, kPop = 0x17,
    kGetVar = 0x1C, kSetVar = 0x1D, kDefineLocal = 0x3C, kReturn = 0x3E, kNew = 0x40, kDefineLocal2 = 0x41,
    kAdd2 = 0x47, kLess2 = 0x48, kEquals2 = 0x49, kToNumber = 0x4A, kToString = 0x4B, kPushDuplicate = 0x4C,
    kGetMember = 0x4E, kSetMember = 0x4F, kIncrement = 0x50, kDecrement = 0x51, kCallMethod = 0x52,
    kStrictEquals = 0x66, kGreater = 0x67, kStoreRegister = 0x87, kConstantPool = 0x88, kPush = 0x96, kJump = 0x99,
    kDefineFunction = 0x9B, kIf = 0x9D
};

struct Writer {
    const Consts& consts;
    Code where;
    uint32_t at;  // the code's guest address, for the alignment
    std::vector<uint8_t> code;
    std::vector<Push> pushes;
    bool ok = true;
    bool merge = false;
    size_t push_end = SIZE_MAX;  // where the last Push ends
    void Op(uint8_t op) { code.push_back(op); }
    size_t Mark() {
        push_end = SIZE_MAX;
        return code.size();
    }
    void Align() {
        while ((at + code.size()) % 4) code.push_back(0);
    }
    void Values(std::initializer_list<Value> values) {
        bool join = merge && push_end == code.size();
        if (!join) {
            code.push_back(kPush);
            Align();
            pushes.push_back(Push{code.size(), {}});
            code.insert(code.end(), 8, 0);
            push_end = code.size();
        }
        Push& p = pushes.back();
        for (const Value& v : values) {
            int e = -1;
            if (auto s = std::get_if<const char*>(&v)) e = consts.Find(*s);
            else if (auto i = std::get_if<int>(&v)) e = *i ? consts.Find(7, uint32_t(*i)) : consts.Find(6, 0);
            else if (auto b = std::get_if<Bool>(&v)) e = consts.Find(5, b->value);
            else if (auto r = std::get_if<Reg>(&v)) e = consts.Find(4, uint32_t(r->n));
            else e = consts.Find(3, 0);
            ok = ok && e >= 0;
            p.entries.push_back(e);
        }
    }
    // the value on the stack kept in a register too
    void Store(int reg) {
        code.push_back(kStoreRegister);
        Align();
        code.insert(code.end(), {uint8_t(reg), 0, 0, 0});
    }
    // a branch, its offset set by Land
    size_t Branch(uint8_t op) {
        code.push_back(op);
        Align();
        code.insert(code.end(), 4, 0);
        return code.size() - 4;
    }
    void Land(size_t branch, size_t target) {
        int32_t offset = int32_t(target) - int32_t(branch + 4);
        std::memcpy(&code[branch], &offset, 4);
    }
    void Land(size_t branch) { Land(branch, Mark()); }
};

// a movie's code to write anew: the block that sets it off (the first the linker meets), the code (checked as it
// ships, CRC-32 from the first's start, its DefineFunction's for a body, to the last's end), the names it needs that
// the movie has not (written over text no longer used, as long or longer), and the registers' values it pushes, which
// the movie has not either (entries for text no longer used made them)
struct Rewrite {
    const char* name;
    std::vector<uint32_t> triggers;
    uint32_t crc;
    std::vector<std::pair<const char*, const char*>> renamed;
    std::vector<std::pair<Code, std::function<void(Writer&)>>> code;
    std::vector<std::pair<const char*, int>> registers;
};

std::vector<Rewrite> HelpBarRewrites();  // game/helpbar.cpp
std::vector<Rewrite> MenuRewrites();     // game/menus.cpp

// extern's names for the menus' mouse (game/menus.cpp), the game's thread: true with the value for one of them
bool MenuExtern(const char* name, std::string& value);

}  // namespace apt
