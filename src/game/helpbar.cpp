// The menus' help bar on the keys: the buttons along the bottom ("A Select  B Back", the pause menu's "Back Map") as
// the keys that press them ("[E] Select  [Q] Back"). The menus are EA's Apt movies (Flash: DATA/SHARED/UI/FE/mohfe.viv),
// the help bar scrns/helpBar.big, which each screen asks for its buttons (displayIcons, a list of button types and of
// labels, "$HB_Select", "$HB_Back" and so on, string table names). For each one show(iconType, iconLable) makes an icon
// (mcButtonIconSymbol: a button glyph, platformIcon, and its label, iconText) at the frame for the button (type 1 A,
// 2 B, 3 X, 4 Y) and gives the label its text, which the text field makes the string's when it starts with $ or #
// (sub_00171270, by sub_001710A0, cdecl (text, wide text out)).
//
// Here show(), displayIcons() and findStartX() (which centres the bar, measuring the labels) are written anew before
// their bytecode is linked (sub_00287830, cdecl (code, the .apt, the .const, counter), the block the main timeline's,
// at +0x1810, holding them): show() asks extern (the game's variables: sub_00170770, cdecl (name), a string allocated
// for the movie) for "_" and the type, which is "" on a controller and the key marked out ($, \x01, the key, \x02) on
// the keys; the label is that before the string's name, and the glyph is hidden unless it is "". The marked key is
// made "[KEY] " as the text is made. findStartX() measures the labels so, and it and displayIcons() space them as
// extern says, closer without the glyph. The bar is made as a screen opens, so a device switched to shows from the
// next screen on.
#include <algorithm>
#include <cstring>
#include <string>
#include <variant>
#include <vector>

#include "common/pc_input.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {

using namespace pc_input;

// The help bar's functions in scrns/helpBar.apt, as it ships: each one's DefineFunction, its body 0x1C after it
struct Function {
    uint32_t def, size;
};
constexpr uint32_t kMainBlock = 0x1810, kBody = 0x1C;
constexpr Function kDisplayIcons{0x1AB4, 0x254}, kShow{0x1E74, 0x1C2}, kFindStartX{0x23A8, 0x132};
constexpr uint32_t kFunctionsCrc = 0x8A149A46;  // the three, displayIcons' DefineFunction to findStartX's end
constexpr uint32_t kMovieAlloc = 0x534410;      // the movies' allocator, cdecl (size)
constexpr char kMark = '\x01', kMarkEnd = '\x02';

uint32_t& U32(uint8_t* base, uint32_t a) { return *reinterpret_cast<uint32_t*>(base + a); }

uint32_t Crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & (0 - (c & 1)));
    }
    return ~c;
}

// The .const: entries (type, value) at +0x1C (a pointer by now), their count at +0x18; type 1 a string (its offset in
// the .const), 6 a number (0 its only one here), 7 an integer
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
// the values themselves, so each Push has an array of its own: the old functions', reused. A branch's offset is from
// its end
using Value = std::variant<const char*, int>;
struct Push {
    size_t at;  // in the code
    std::vector<int> entries;
};
enum Op : uint8_t {
    kSub = 0x0B, kDivide = 0x0D, kNot = 0x12, kPop = 0x17, kGetVar = 0x1C, kSetVar = 0x1D, kDefineLocal = 0x3C,
    kReturn = 0x3E, kDefineLocal2 = 0x41, kAdd2 = 0x47, kLess2 = 0x48, kToNumber = 0x4A, kToString = 0x4B,
    kGetMember = 0x4E, kSetMember = 0x4F, kIncrement = 0x50, kCallMethod = 0x52, kStrictEquals = 0x66, kPush = 0x96,
    kJump = 0x99, kDefineFunction = 0x9B, kIf = 0x9D
};

struct Writer {
    const Consts& consts;
    Function function;
    uint32_t at;  // the body's guest address, for the alignment
    std::vector<uint8_t> code;
    std::vector<Push> pushes;
    bool ok = true;
    void Op(uint8_t op) { code.push_back(op); }
    void Align() {
        while ((at + code.size()) % 4) code.push_back(0);
    }
    void Values(std::initializer_list<Value> values) {
        code.push_back(kPush);
        Align();
        Push p{code.size(), {}};
        for (const Value& v : values) {
            int i = std::holds_alternative<int>(v) ? std::get<int>(v) : 0;
            int e = std::holds_alternative<const char*>(v) ? consts.Find(std::get<const char*>(v))
                    : i                                    ? consts.Find(7, uint32_t(i))
                                                           : consts.Find(6, 0);
            ok = ok && e >= 0;
            p.entries.push_back(e);
        }
        pushes.push_back(p);
        code.insert(code.end(), 8, 0);
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
};

// displayIcons(iconN_LST, iconLable_LST): the old one, its labels left as they are (its localization is off), the gap
// after a label extern's _gap
void WriteDisplayIcons(Writer& w) {
    auto P = [&](std::initializer_list<Value> v) { w.Values(v); };
    // this.bar._alpha = 50;
    P({"this"}), w.Op(kGetVar), P({"bar"}), w.Op(kGetMember), P({"_alpha", 50}), w.Op(kSetMember);
    // var len = iconN_LST.length; var iconType;
    P({"len", "iconN_LST"}), w.Op(kGetVar), P({"length"}), w.Op(kGetMember), w.Op(kDefineLocal);
    P({"iconType"}), w.Op(kDefineLocal2);
    // var startX = this.findStartX(iconN_LST, iconLable_LST);
    P({"startX", "iconLable_LST"}), w.Op(kGetVar), P({"iconN_LST"}), w.Op(kGetVar), P({2, "this"}), w.Op(kGetVar);
    P({"findStartX"}), w.Op(kCallMethod), w.Op(kDefineLocal);
    // var mc; var strLengthInPixels = 0; var i = 0;
    P({"mc"}), w.Op(kDefineLocal2), P({"strLengthInPixels", 0}), w.Op(kDefineLocal), P({"i", 0}), w.Op(kDefineLocal);
    // while (i < len) {
    size_t loop = w.code.size();
    P({"i"}), w.Op(kGetVar), P({"len"}), w.Op(kGetVar), w.Op(kLess2), w.Op(kNot);
    size_t done = w.Branch(kIf);
    // iconType = iconN_LST[i]; iconLable = iconLable_LST[i];
    P({"iconType", "iconN_LST"}), w.Op(kGetVar), P({"i"}), w.Op(kGetVar), w.Op(kGetMember), w.Op(kSetVar);
    P({"iconLable", "iconLable_LST"}), w.Op(kGetVar), P({"i"}), w.Op(kGetVar), w.Op(kGetMember), w.Op(kSetVar);
    // mc = this.show(iconType, iconLable);
    P({"mc", "iconLable"}), w.Op(kGetVar), P({"iconType"}), w.Op(kGetVar), P({2, "this"}), w.Op(kGetVar);
    P({"show"}), w.Op(kCallMethod), w.Op(kSetVar);
    // mc._x = startX;
    P({"mc"}), w.Op(kGetVar), P({"_x", "startX"}), w.Op(kGetVar), w.Op(kSetMember);
    // strLengthInPixels = mc.iconText.textWidth + Number(extern._gap);
    P({"strLengthInPixels", "mc"}), w.Op(kGetVar), P({"iconText"}), w.Op(kGetMember), P({"textWidth"});
    w.Op(kGetMember), P({"extern"}), w.Op(kGetVar), P({"_gap"}), w.Op(kGetMember), w.Op(kToNumber), w.Op(kAdd2);
    w.Op(kSetVar);
    // startX = mc._x + strLengthInPixels;
    P({"startX", "mc"}), w.Op(kGetVar), P({"_x"}), w.Op(kGetMember), P({"strLengthInPixels"}), w.Op(kGetVar);
    w.Op(kAdd2), w.Op(kSetVar);
    // i++; }
    P({"i", "i"}), w.Op(kGetVar), w.Op(kIncrement), w.Op(kSetVar);
    w.Land(w.Branch(kJump), loop);
    w.Land(done, w.function.size);
}

// the new show(iconType, iconLable): the old one, its traces left out, and the key
void WriteShow(Writer& w) {
    auto P = [&](std::initializer_list<Value> v) { w.Values(v); };
    // var iCount = this.iconLST.length;
    P({"iCount", "this"}), w.Op(kGetVar), P({"iconLST"}), w.Op(kGetMember), P({"length"}), w.Op(kGetMember);
    w.Op(kDefineLocal);
    // placeHolderIcon.duplicateMovieClip("icon" + iCount, iCount + 1);
    P({"iCount"}), w.Op(kGetVar), P({1}), w.Op(kAdd2), P({"icon", "iCount"}), w.Op(kGetVar), w.Op(kToString);
    w.Op(kAdd2), P({2, "placeHolderIcon"}), w.Op(kGetVar), P({"duplicateMovieClip"}), w.Op(kCallMethod), w.Op(kPop);
    // mc = eval("icon" + iCount);
    P({"mc", "icon", "iCount"}), w.Op(kGetVar), w.Op(kToString), w.Op(kAdd2), w.Op(kGetVar), w.Op(kSetVar);
    // this.iconLST[iCount] = mc;
    P({"this"}), w.Op(kGetVar), P({"iconLST"}), w.Op(kGetMember), P({"iCount"}), w.Op(kGetVar), P({"mc"});
    w.Op(kGetVar), w.Op(kSetMember);
    // mc._alpha = 100;
    P({"mc"}), w.Op(kGetVar), P({"_alpha", 100}), w.Op(kSetMember);
    // mc.gotoAndStop(this.getIconFrameLabel(iconType));
    P({"iconType"}), w.Op(kGetVar), P({1, "this"}), w.Op(kGetVar), P({"getIconFrameLabel"}), w.Op(kCallMethod);
    P({1, "mc"}), w.Op(kGetVar), P({"gotoAndStop"}), w.Op(kCallMethod), w.Op(kPop);
    // mc._y = 430;
    P({"mc"}), w.Op(kGetVar), P({"_y", 430}), w.Op(kSetMember);
    // var fLabel = extern["_" + iconType];
    P({"fLabel", "extern"}), w.Op(kGetVar), P({"_", "iconType"}), w.Op(kGetVar), w.Op(kAdd2), w.Op(kGetMember);
    w.Op(kDefineLocal);
    // mc.platformIcon._visible = "" === fLabel;
    P({"mc"}), w.Op(kGetVar), P({"platformIcon"}), w.Op(kGetMember), P({"_visible", "", "fLabel"}), w.Op(kGetVar);
    w.Op(kStrictEquals), w.Op(kSetMember);
    // mc.iconText.text = fLabel + iconLable;
    P({"mc"}), w.Op(kGetVar), P({"iconText"}), w.Op(kGetMember), P({"text", "fLabel"}), w.Op(kGetVar);
    P({"iconLable"}), w.Op(kGetVar), w.Op(kAdd2), w.Op(kSetMember);
    // this.bar._y = 430 - 10;
    P({"this"}), w.Op(kGetVar), P({"bar"}), w.Op(kGetMember), P({"_y", 430, 10}), w.Op(kSub), w.Op(kSetMember);
    // return mc;
    P({"mc"}), w.Op(kGetVar), w.Op(kReturn);
}

// findStartX(iconN_LST, iconLable_LST), where the bar starts to be centred: the labels measured as show() makes them,
// the key before them, each with extern's _gap after it, and extern's _start (-2 times what is added to the start)
// counted in; the old one measured the labels alone
void WriteFindStartX(Writer& w) {
    auto P = [&](std::initializer_list<Value> v) { w.Values(v); };
    // var len = iconN_LST.length; var strLengthInPixels = Number(extern._start); var i = 0;
    P({"len", "iconN_LST"}), w.Op(kGetVar), P({"length"}), w.Op(kGetMember), w.Op(kDefineLocal);
    P({"strLengthInPixels", "extern"}), w.Op(kGetVar), P({"_start"}), w.Op(kGetMember), w.Op(kToNumber);
    w.Op(kDefineLocal), P({"i", 0}), w.Op(kDefineLocal);
    // while (i < len) {
    size_t loop = w.code.size();
    P({"i"}), w.Op(kGetVar), P({"len"}), w.Op(kGetVar), w.Op(kLess2), w.Op(kNot);
    size_t done = w.Branch(kIf);
    // placeHolderIcon.iconText.text = extern["_" + iconN_LST[i]] + iconLable_LST[i];
    P({"placeHolderIcon"}), w.Op(kGetVar), P({"iconText"}), w.Op(kGetMember), P({"text", "extern"}), w.Op(kGetVar);
    P({"_", "iconN_LST"}), w.Op(kGetVar), P({"i"}), w.Op(kGetVar), w.Op(kGetMember), w.Op(kAdd2), w.Op(kGetMember);
    P({"iconLable_LST"}), w.Op(kGetVar), P({"i"}), w.Op(kGetVar), w.Op(kGetMember), w.Op(kAdd2), w.Op(kSetMember);
    // strLengthInPixels = strLengthInPixels + placeHolderIcon.iconText.textWidth + Number(extern._gap);
    P({"strLengthInPixels", "strLengthInPixels"}), w.Op(kGetVar), P({"placeHolderIcon"}), w.Op(kGetVar);
    P({"iconText"}), w.Op(kGetMember), P({"textWidth"}), w.Op(kGetMember), w.Op(kAdd2), P({"extern"});
    w.Op(kGetVar), P({"_gap"}), w.Op(kGetMember), w.Op(kToNumber), w.Op(kAdd2), w.Op(kSetVar);
    // i++; }
    P({"i", "i"}), w.Op(kGetVar), w.Op(kIncrement), w.Op(kSetVar);
    w.Land(w.Branch(kJump), loop);
    // return (854 - strLengthInPixels) / 2;
    w.Land(done, w.code.size());
    P({854, "strLengthInPixels"}), w.Op(kGetVar), w.Op(kSub), P({2}), w.Op(kDivide), w.Op(kReturn);
}

// a function's Push arrays: (capacity, offset in the .apt)
void OldArrays(uint8_t* base, uint32_t apt, Function f, std::vector<std::pair<uint32_t, uint32_t>>& arrays) {
    for (uint32_t a = apt + f.def + kBody; a < apt + f.def + kBody + f.size;) {
        uint8_t op = base[a];
        if (op < 0x80) {
            a++;
            continue;
        }
        uint32_t args = (a + 4) & ~3u;
        if (op == kPush) arrays.emplace_back(U32(base, args), U32(base, args + 4));
        a = args + (op == kPush ? 8 : 4);
    }
}

// the help bar's main block about to be linked: its functions written anew, once (linked, they are no longer as they
// ship)
void Patch(uint8_t* base, uint32_t code, uint32_t apt, uint32_t consts_file) {
    constexpr Function kFunctions[] = {kDisplayIcons, kShow, kFindStartX};
    constexpr void (*kWrite[])(Writer&) = {WriteDisplayIcons, WriteShow, WriteFindStartX};
    if (code != apt + kMainBlock || std::memcmp(base + apt, "Apt Data:6", 10) ||
        Crc32(base + apt + kDisplayIcons.def, kFindStartX.def + kBody + kFindStartX.size - kDisplayIcons.def) !=
            kFunctionsCrc)
        return;
    for (Function f : kFunctions)
        if (base[apt + f.def] != kDefineFunction || U32(base, apt + f.def + 16) != f.size) return;
    Consts consts{base, consts_file, U32(base, consts_file + 0x1C), U32(base, consts_file + 0x18)};
    // the names the functions need that the help bar has not, in place of text no longer used, as long or longer
    constexpr std::pair<const char*, const char*> kRenamed[] = {
        {"ICON COUNT = ", "platformIcon"}, {"NEW ICON = ", "_visible"}, {"What is my FRAME LABEL ? ", "extern"},
        {"input to the filter function = ", "_gap"}, {"totalLenghtOfHBMessages", "_start"}};
    for (auto [from, to] : kRenamed)
        if (consts.Find(from) < 0) {
            LOGW("help bar: no \"%s\" to rename, left as it is", from);
            return;
        }
    for (auto [from, to] : kRenamed) {
        int e = consts.Find(from);
        std::strcpy(reinterpret_cast<char*>(base + consts_file + U32(base, consts.entries + 8 * e + 4)), to);
    }
    std::vector<Writer> writers;
    std::vector<std::pair<uint32_t, uint32_t>> arrays;
    for (size_t i = 0; i < std::size(kFunctions); i++) {
        writers.push_back(Writer{consts, kFunctions[i], apt + kFunctions[i].def + kBody});
        kWrite[i](writers.back());
        OldArrays(base, apt, kFunctions[i], arrays);
        const Writer& w = writers.back();
        if (!w.ok || w.code.size() > w.function.size) {
            LOGW("help bar: not rewritten, the function at +%X %zu bytes of %u", w.function.def, w.code.size(),
                 w.function.size);
            return;
        }
    }
    // the arrays to the Pushes, the fullest Push first, each to the smallest array that holds it
    std::vector<std::pair<Writer*, const Push*>> pushes;
    for (Writer& w : writers)
        for (const Push& p : w.pushes) pushes.emplace_back(&w, &p);
    std::stable_sort(pushes.begin(), pushes.end(),
                     [](auto& a, auto& b) { return a.second->entries.size() > b.second->entries.size(); });
    std::sort(arrays.begin(), arrays.end());
    std::vector<uint32_t> array_of(pushes.size());
    for (size_t i = 0; i < pushes.size(); i++) {
        auto fit = std::find_if(arrays.begin(), arrays.end(),
                                [&](auto& a) { return a.first >= pushes[i].second->entries.size(); });
        if (fit == arrays.end()) {
            LOGW("help bar: not rewritten, too few Push arrays");
            return;
        }
        array_of[i] = fit->second;
        arrays.erase(fit);
    }
    for (Writer& w : writers) {
        // the rest after its end, never run, a 1 byte action (the linker stops at an End, 0)
        w.code.resize(w.function.size, 0x02);
        std::memcpy(base + w.at, w.code.data(), w.function.size);
    }
    for (size_t i = 0; i < pushes.size(); i++) {
        auto [w, p] = pushes[i];
        U32(base, w->at + uint32_t(p->at)) = uint32_t(p->entries.size());
        U32(base, w->at + uint32_t(p->at) + 4) = array_of[i];
        for (size_t k = 0; k < p->entries.size(); k++) U32(base, apt + array_of[i] + 4 * uint32_t(k)) = p->entries[k];
    }
    LOGI("help bar: rewritten for the keys (%zu Pushes)", pushes.size());
}

// the key an icon's button is pressed with in the menus: A's is use's or reload's when it has none of its own
std::string MenuKey(int type) {
    constexpr int kTypes[] = {kA, kB, kX, kY};
    if (type < 1 || type > 4) return {};
    int action = kTypes[type - 1];
    std::string key = FirstKey(action);
    if (key.empty() && action == kA) key = FirstKey(kUse);
    if (key.empty() && action == kA) key = FirstKey(kReload);
    return key;
}

}  // namespace

// a block of a movie's bytecode linked
X86_EXTERN_FUNC(__imp__sub_00287830);
X86_FUNC(sub_00287830) {
    uint32_t code = U32(base, ctx.esp + 4), apt = U32(base, ctx.esp + 8), consts = U32(base, ctx.esp + 12);
    if (consts) Patch(base, code, apt, consts);
    __imp__sub_00287830(ctx, base);
}

// extern's variables: the help bar's "_1" to "_4", the button type's key on the keys; its "_gap" after a label and
// "_start", -2 times what is added to the bar's start: on a controller as the game has them (the gap 60, the start
// 26), on the keys without the glyph's room (the glyph 32 wide, centred at the icon's left, the label at 18)
X86_EXTERN_FUNC(__imp__sub_00170770);
X86_FUNC(sub_00170770) {
    const char* name = reinterpret_cast<const char*>(base + U32(base, ctx.esp + 4));
    bool keys = UsingKeyboard();
    std::string value;
    if (!std::strcmp(name, "_gap")) {
        value = keys ? "30" : "60";
    } else if (!std::strcmp(name, "_start")) {
        value = keys ? "8" : "-52";
    } else if (name[0] == '_' && name[1] >= '1' && name[1] <= '4' && !name[2]) {
        std::string key = keys ? MenuKey(name[1] - '0') : std::string();
        if (!key.empty()) value = std::string("$") + kMark + key + kMarkEnd;
    } else {
        __imp__sub_00170770(ctx, base);
        return;
    }
    uint32_t text = kernel::CallGuest(ctx, U32(base, kMovieAlloc), {uint32_t(value.size() + 1)});
    if (text) std::memcpy(base + text, value.c_str(), value.size() + 1);
    ctx.eax = text;
    ctx.esp += 4;  // ret
}

// a text made the strings': a key marked out at its start, "[KEY] "
X86_EXTERN_FUNC(__imp__sub_001710A0);
X86_FUNC(sub_001710A0) {
    uint32_t& text = U32(base, ctx.esp + 4);
    uint32_t& out = U32(base, ctx.esp + 8);
    const char* s = reinterpret_cast<const char*>(base + text);
    const char* end = s[0] == '$' && s[1] == kMark ? std::strchr(s, kMarkEnd) : nullptr;
    if (end) {
        std::string key = "[" + std::string(s + 2, end) + "] ";
        for (char c : key) {
            *reinterpret_cast<uint16_t*>(base + out) = uint8_t(c);
            out += 2;
        }
        text += uint32_t(end + 1 - s);
        *reinterpret_cast<uint16_t*>(base + out) = 0;
        if (!base[text]) {
            ctx.esp += 4;  // ret: nothing after the key
            return;
        }
    }
    __imp__sub_001710A0(ctx, base);
}
