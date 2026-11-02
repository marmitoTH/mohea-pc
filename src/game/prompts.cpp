// The button prompts as the controls are remapped (common/pc_input.h). A rich text (the action prompts' csa_richtext,
// the tutorials) is parsed by sub_0015DFF0 (stdcall (text), ecx the rich text): text into text chunks (sub_0015DBA0,
// (text)), and each [$NAME] or [>Name] into an icon chunk (sub_0015DC80, (icon, flag)). [$ACTION] (the use prompts:
// "Press [$ACTION] to pick up %1") and every [>Name] (by the CRC-32 of the name: Aim, Shoot, Send_Squad,
// Activate_Medkit, Move_ForwardBackward, accept_button and so on) get their icon from sub_0016B410 (cdecl (name)): the
// button the layout has for the name's control (Sharp Shooter always, game/layout.cpp), as the HUD's icon for it (the
// CRC-32 of its name: icon_cross_button for A, icon_L1_button for the left trigger, icon_L2_button for white...).
// Here, on a controller, the icon is the button the action is remapped to; on the keys, the token is the action's key
// in brackets ("Press [E] to pick up %1"), written into the text before it is parsed between two marks the parser
// leaves alone (it drops anything else in brackets) and made brackets as the text chunk is made. The use prompts show
// use's own button or key when it has one, reload's reload's.
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "common/pc_input.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {

using namespace pc_input;

// the HUD's icons
constexpr uint32_t kCross = 0xD8F9521C, kCircle = 0xD78C27DD, kSquare = 0x20534658, kTriangle = 0x855D2461;
constexpr uint32_t kR2 = 0x9567858C, kL2 = 0xF1651C6F, kL1 = 0x7FEA1B8C, kR1 = 0x1BE8826F;
constexpr uint32_t kDUp = 0xF3009690, kDDown = 0xA34BCAE5, kDLeft = 0x586AC0B8, kDRight = 0xB09C5803;
constexpr uint32_t kStartIcon = 0x3F23584A, kSelect = 0xF8B7AD68, kL3 = 0x3DCF1CF1, kR3 = 0x59CD8512;
constexpr uint32_t kActionIcon = 0xC69700EE;  // the action prompts' own A, the action unbound
constexpr uint32_t kL3UpDown = 0x65C91D69, kL3LeftRight = 0x8DC62922, kR3UpDown = 0xA36CD55C, kR3LeftRight = 0x0AF904BE;
// each of a controller's buttons (the actions' order, pc_input.h), its icon: black and white are R2 and L2, the
// triggers L1 and R1
constexpr uint32_t kButtonIcons[kPadButtons] = {kCross, kCircle, kSquare, kTriangle, kR2, kL2, kL1, kR1,
                                                kDUp, kDDown, kDLeft, kDRight, kStartIcon, kSelect, kL3, kR3};

// the names' CRC-32s that are not plain controls: the use and reload prompts, and the menus' accept and cancel (the
// tutorials' continue and skip), which a controller always has on A and B, unmapped out of play
constexpr uint32_t kAction = 0x406089A4, kReloadName = 0xA514CB63, kAccept = 0xFBC50CD3, kCancel = 0xF82137E7;

// the guest's allocator (cdecl (file, size, flags)), free (cdecl (block)), a file name for its records
constexpr uint32_t kAlloc = 0x155010, kFree = 0x155300, kAllocFile = 0x31F3D4;
constexpr char kOpen = '\x01', kClose = '\x02';

// The game's thread only. The rich texts of a tutorial's box (its "Press [$ACTION] to continue" is A, its controls
// remapped, in play or not), made by sub_00160B50 the first time it shows; the one being parsed, and each one parsed
// for the keys, a controller in play or a controller out of it (a tutorial's only for the keys or not)
std::unordered_set<uint32_t> g_menu_texts;
bool g_in_menu = false;
uint32_t g_parsing = 0;
std::unordered_map<uint32_t, int> g_parsed;
bool Menu(uint32_t text) { return g_menu_texts.count(text) != 0; }
int Parsing(uint32_t text) { return UsingKeyboard() ? 2 : InPlay() && !Menu(text) ? 1 : 0; }

uint32_t Crc32(const char* s, size_t n) {
    uint32_t c = 0xFFFFFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= uint8_t(s[i]);
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & (0 - (c & 1)));
    }
    return ~c;
}

// what a prompt shows: an action, or a stick's pair of directions (0 none)
struct Shown {
    int action = -1;
    int stick = 0;  // 1 moving forward and back, 2 left and right, 3 looking up and down, 4 left and right
};

// the action a token's icon stands for; the use prompts' A use's, reload's reload's, when they have their own
Shown Resolve(uint32_t name, uint32_t icon, bool keys) {
    auto own = [&](int action) { return keys ? !FirstKey(action).empty() : PadButton(action) >= 0; };
    Shown s;
    for (int b = 0; b < kPadButtons; b++)
        if (icon == kButtonIcons[b]) s.action = b;
    if (icon == kActionIcon) s.action = kA;
    if (icon == kL3UpDown) s.stick = 1;
    if (icon == kL3LeftRight) s.stick = 2;
    if (icon == kR3UpDown) s.stick = 3;
    if (icon == kR3LeftRight) s.stick = 4;
    if (name == kAction && own(kUse)) s.action = kUse;
    if (name == kReloadName && own(kReload)) s.action = kReload;
    return s;
}

// the icon on a controller: the action's button now; the sticks swapped
uint32_t PadIcon(uint32_t name, uint32_t icon) {
    // accept and cancel are A and B always; a tutorial's [$ACTION] is its continue, A; out of play, but in a tutorial's
    // box, a controller is as it is
    if (name == kAccept || name == kCancel) return icon;
    if (Menu(g_parsing) ? name == kAction : !InPlay()) return icon;
    Shown s = Resolve(name, icon, false);
    if (s.stick) {
        if (!g_swap_sticks) return icon;
        constexpr uint32_t kSwapped[] = {0, kR3UpDown, kR3LeftRight, kL3UpDown, kL3LeftRight};
        return kSwapped[s.stick];
    }
    int button = s.action >= 0 ? PadButton(s.action) : -1;
    return button >= 0 ? kButtonIcons[button] : icon;
}

// the keys a token stands for, "" to leave it an icon
std::string KeyText(uint32_t name, uint32_t icon) {
    Shown s = Resolve(name, icon, true);
    auto pair = [](int a, int b) {
        std::string x = FirstKey(a), y = FirstKey(b);
        return x.empty() || y.empty() ? std::string() : x + "/" + y;
    };
    switch (s.stick) {
        case 1: return pair(kMoveForward, kMoveBack);
        case 2: return pair(kMoveLeft, kMoveRight);
        case 3:
        case 4: return "Mouse";
    }
    if (s.action < 0) return {};
    std::string key = FirstKey(s.action);
    // the use prompts' A without a key of its own: use's, else reload's
    if (key.empty() && s.action == kA) key = FirstKey(kUse);
    if (key.empty() && s.action == kA) key = FirstKey(kReload);
    return key;
}

}  // namespace

// a control's icon
X86_EXTERN_FUNC(__imp__sub_0016B410);
X86_FUNC(sub_0016B410) {
    uint32_t name = *reinterpret_cast<uint32_t*>(base + ctx.esp + 4);
    __imp__sub_0016B410(ctx, base);
    if (!UsingKeyboard()) ctx.eax = PadIcon(name, ctx.eax);
}

// a rich text given its text (stdcall (text), ecx the rich text), each frame the HUD shows it: it keeps a copy (+0x98)
// and parses only a text that differs. One parsed for the other device, or a controller in or out of play, is parsed
// again: its copy made to differ
X86_EXTERN_FUNC(__imp__sub_0015E1C0);
X86_FUNC(sub_0015E1C0) {
    auto parsed = g_parsed.find(ctx.ecx);
    uint32_t copy = *reinterpret_cast<uint32_t*>(base + ctx.ecx + 0x98);
    if (parsed != g_parsed.end() && parsed->second != Parsing(ctx.ecx) && copy && base[copy]) base[copy] ^= 0x80;
    __imp__sub_0015E1C0(ctx, base);
}

// a tutorial shown (ecx its box, the tip on the stack): the rich texts it makes are a menu's
X86_EXTERN_FUNC(__imp__sub_00160B50);
X86_FUNC(sub_00160B50) {
    bool was = g_in_menu;
    g_in_menu = true;
    __imp__sub_00160B50(ctx, base);
    g_in_menu = was;
}

X86_EXTERN_FUNC(__imp__sub_0015DFF0);

namespace {

// the parse, its tokens the keys on the keys
void Parse(X86Context& ctx, uint8_t* base) {
    uint32_t& arg = *reinterpret_cast<uint32_t*>(base + ctx.esp + 4);
    if (!UsingKeyboard() || !arg) {
        __imp__sub_0015DFF0(ctx, base);
        return;
    }
    std::string text(reinterpret_cast<const char*>(base + arg)), out;
    bool changed = false;
    for (size_t i = 0; i < text.size();) {
        size_t end = text[i] == '[' ? text.find(']', i) : std::string::npos;
        if (end == std::string::npos || end - i < 3 || (text[i + 1] != '$' && text[i + 1] != '>')) {
            out += text[i++];
            continue;
        }
        std::string token = text.substr(i + 2, end - i - 2);
        uint32_t name = text[i + 1] == '>' ? Crc32(token.data(), token.size()) : token == "ACTION" ? kAction : 0;
        std::string key;
        if (name) {
            uint32_t ecx = ctx.ecx;
            uint32_t icon = kernel::CallGuest(ctx, 0x16B410, {name});
            ctx.ecx = ecx;
            key = KeyText(name, icon);
        }
        if (key.empty()) {
            out += text.substr(i, end - i + 1);
        } else {
            out += kOpen + key + kClose;
            changed = true;
        }
        i = end + 1;
    }
    if (!changed) {
        __imp__sub_0015DFF0(ctx, base);
        return;
    }
    uint32_t ecx = ctx.ecx;
    uint32_t copy = kernel::CallGuest(ctx, kAlloc, {kAllocFile, uint32_t(out.size() + 1), 0x400});
    ctx.ecx = ecx;
    if (!copy) {
        __imp__sub_0015DFF0(ctx, base);
        return;
    }
    std::memcpy(base + copy, out.c_str(), out.size() + 1);
    arg = copy;
    __imp__sub_0015DFF0(ctx, base);
    kernel::CallGuest(ctx, kFree, {copy});
}

}  // namespace

// a rich text's text (stdcall (text), ecx the rich text): parsed, a menu's marked so
X86_FUNC(sub_0015DFF0) {
    if (g_in_menu) g_menu_texts.insert(ctx.ecx);
    g_parsed[ctx.ecx] = Parsing(ctx.ecx);
    uint32_t was = g_parsing;
    g_parsing = ctx.ecx;
    Parse(ctx, base);
    g_parsing = was;
}

// a text chunk's text: the keys' marks brackets
X86_EXTERN_FUNC(__imp__sub_0015DBA0);
X86_FUNC(sub_0015DBA0) {
    uint32_t text = *reinterpret_cast<uint32_t*>(base + ctx.esp + 4);
    for (char* c = reinterpret_cast<char*>(base + text); text && *c; c++) {
        if (*c == kOpen) *c = '[';
        if (*c == kClose) *c = ']';
    }
    __imp__sub_0015DBA0(ctx, base);
}
