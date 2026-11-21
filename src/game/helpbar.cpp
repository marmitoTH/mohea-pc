// The menus' buttons on the keys: the help bar's along the bottom ("A Select  B Back", the pause menu's "Back Map")
// and the dialogs' choices ("A Yes", "B No") as the keys that press them ("[E] Select  [Q] Back"). The menus are EA's
// Apt movies (Flash: DATA/SHARED/UI/FE/mohfe.viv), the help bar scrns/helpBar.big, which each screen asks for its
// buttons (displayIcons, a list of button types and of labels, "$HB_Select", "$HB_Back" and so on, string table
// names). For each one show(iconType, iconLable) makes an icon
// (mcButtonIconSymbol: a button glyph, platformIcon, and its label, iconText) at the frame for the button (type 1 A,
// 2 B, 3 X, 4 Y) and gives the label its text, which the text field makes the string's when it starts with $ or #
// (sub_00171270, by sub_001710A0, cdecl (text, wide text out)).
//
// Here show(), displayIcons() and findStartX() (which centres the bar, measuring the labels) are written anew before
// their bytecode is linked (game/apt_rewrite.h; the block the main timeline's, at +0x1810, holding them): show() asks
// extern (the game's variables: sub_00170770, cdecl (name), a string allocated for the movie) for "_" and the type,
// which is "" on a controller and the key marked out ($, \x01, the key, \x02) on the keys; the label is that before
// the string's name, and the glyph is hidden unless it is "". The marked key is made "[KEY] " as the text is made.
// findStartX() measures the labels so, and it and displayIcons() space them as extern says, closer without the glyph.
// A dialog's choices are the same (scrns/dialogueWindow.apt, below). The bar is made as a screen opens, so a device
// switched to shows from the next screen on. extern's "_mx" and "_my" are the menus' mouse (game/menus.cpp).
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "common/pc_input.h"
#include "core/log.h"
#include "game/apt_rewrite.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {

using namespace apt;
using namespace pc_input;

constexpr uint32_t kMovieAlloc = 0x534410;  // the movies' allocator, cdecl (size)
constexpr char kMark = '\x01', kMarkEnd = '\x02';

// The help bar, scrns/helpBar.apt: the main timeline's block (+0x1810) holds its functions, each one's DefineFunction
// at def
constexpr uint32_t kHelpBarBlock = 0x1810, kHide = 0x184C, kDisplayIcons = 0x1AB4, kShow = 0x1E74;
constexpr uint32_t kFindStartX = 0x23A8;

// hide(), which killIcons() calls: the old one, and the mouse's prompts gone (extern._b, game/menus.cpp)
void WriteHide(Writer& w) {
    auto P = [&](std::initializer_list<Value> v) { w.Values(v); };
    w.merge = true;
    // extern._b; this.bar._alpha = 0;
    P({"extern"}), w.Op(kGetVar), P({"_b"}), w.Op(kGetMember), w.Op(kPop);
    P({"this"}), w.Op(kGetVar), P({"bar"}), w.Op(kGetMember), P({"_alpha", 0}), w.Op(kSetMember);
    // var i = 0; while (i < this.iconLST.length) {  (hbIcon the timeline's)
    P({"i", 0}), w.Op(kDefineLocal);
    size_t loop = w.Mark();
    P({"i"}), w.Op(kGetVar), P({"this"}), w.Op(kGetVar), P({"iconLST"}), w.Op(kGetMember), P({"length"});
    w.Op(kGetMember), w.Op(kLess2), w.Op(kNot);
    size_t done = w.Branch(kIf);
    // hbIcon = this.iconLST[i]; hbIcon._alpha = 0; hbIcon.gotoAndStop(1); hbIcon.iconText.text = ""; i++; }
    P({"hbIcon", "this"}), w.Op(kGetVar), P({"iconLST"}), w.Op(kGetMember), P({"i"}), w.Op(kGetVar);
    w.Op(kGetMember), w.Op(kSetVar);
    P({"hbIcon"}), w.Op(kGetVar), P({"_alpha", 0}), w.Op(kSetMember);
    P({1, 1, "hbIcon"}), w.Op(kGetVar), P({"gotoAndStop"}), w.Op(kCallMethod), w.Op(kPop);
    P({"hbIcon"}), w.Op(kGetVar), P({"iconText"}), w.Op(kGetMember), P({"text", ""}), w.Op(kSetMember);
    P({"i", "i"}), w.Op(kGetVar), w.Op(kIncrement), w.Op(kSetVar);
    w.Land(w.Branch(kJump), loop);
    w.Land(done, w.where.size);
}

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
    // the prompt for the mouse (game/menus.cpp), the bar on the stage as it is: extern["_b" + iconType + "," + startX
    // + "," + mc.iconText.textWidth];
    P({"extern"}), w.Op(kGetVar), P({"_b", "iconType"}), w.Op(kGetVar), w.Op(kAdd2), P({","}), w.Op(kAdd2);
    P({"startX"}), w.Op(kGetVar), w.Op(kAdd2), P({","}), w.Op(kAdd2), P({"mc"}), w.Op(kGetVar), P({"iconText"});
    w.Op(kGetMember), P({"textWidth"}), w.Op(kGetMember), w.Op(kAdd2), w.Op(kGetMember), w.Op(kPop);
    // startX = mc._x + strLengthInPixels;
    P({"startX", "mc"}), w.Op(kGetVar), P({"_x"}), w.Op(kGetMember), P({"strLengthInPixels"}), w.Op(kGetVar);
    w.Op(kAdd2), w.Op(kSetVar);
    // i++; }
    P({"i", "i"}), w.Op(kGetVar), w.Op(kIncrement), w.Op(kSetVar);
    w.Land(w.Branch(kJump), loop);
    w.Land(done, w.where.size);
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

// The dialogs' choices, scrns/dialogueWindow.apt ("Do you want to save your profile?" A Yes, B No): four of one clip
// (a label, txtOption, and a button glyph, mcHotButtons, its frame the button type), each with a block of actions
// (+0x182C the second, then the first, the third, the fourth) asking the dialog for its "type,label"
// (getIconData(n)), as the help bar's
constexpr uint32_t kChoices[] = {0x1CAC, 0x182C, 0x212C, 0x25AC};
constexpr uint32_t kChoiceSize = 0x47C;  // to the End

// a choice's actions: the old ones, its traces left out, and the key before the label, which on the keys takes the
// glyph's place (extern's _dx its x)
void WriteChoice(Writer& w, int n) {
    auto P = [&](std::initializer_list<Value> v) { w.Values(v); };
    std::vector<size_t> hidden;
    // extern["_d" + n];  (the mouse's prompt for it gone, game/menus.cpp)
    P({"extern"}), w.Op(kGetVar), P({"_d", n}), w.Op(kAdd2), w.Op(kGetMember), w.Op(kPop);
    // var i = _parent.getIconData(n);
    P({"i", n, 1, "_parent"}), w.Op(kGetVar), P({"getIconData"}), w.Op(kCallMethod), w.Op(kDefineLocal);
    // this.mcHotButtons._alpha = 0; this.txtOption.text = "";
    P({"this"}), w.Op(kGetVar), P({"mcHotButtons"}), w.Op(kGetMember), P({"_alpha", 0}), w.Op(kSetMember);
    P({"this"}), w.Op(kGetVar), P({"txtOption"}), w.Op(kGetMember), P({"text", ""}), w.Op(kSetMember);
    // if (i == "") return;
    P({"i"}), w.Op(kGetVar), P({""}), w.Op(kEquals2), hidden.push_back(w.Branch(kIf));
    // var msgLST = i.split(","); var msg = msgLST[1];
    P({"msgLST", ",", 1, "i"}), w.Op(kGetVar), P({"split"}), w.Op(kCallMethod), w.Op(kDefineLocal);
    P({"msg", "msgLST"}), w.Op(kGetVar), P({1}), w.Op(kGetMember), w.Op(kDefineLocal);
    // if (msg === undefined || msg === "" || msg === "-1" || msg === "#-1") return;
    for (Value none : {Value(Undefined{}), Value(""), Value("-1"), Value("#-1")})
        P({"msg"}), w.Op(kGetVar), P({none}), w.Op(kStrictEquals), hidden.push_back(w.Branch(kIf));
    // if (msgLST[0] === "-1") return;
    P({"msgLST"}), w.Op(kGetVar), P({0}), w.Op(kGetMember), P({"-1"}), w.Op(kStrictEquals);
    hidden.push_back(w.Branch(kIf));
    // i = extern["_" + msgLST[0]];
    P({"i", "extern"}), w.Op(kGetVar), P({"_", "msgLST"}), w.Op(kGetVar), P({0}), w.Op(kGetMember), w.Op(kAdd2);
    w.Op(kGetMember), w.Op(kSetVar);
    // this.txtOption.text = i + msg; this.txtOption._x = Number(extern._dx);
    P({"this"}), w.Op(kGetVar), P({"txtOption"}), w.Op(kGetMember), P({"text", "i"}), w.Op(kGetVar), P({"msg"});
    w.Op(kGetVar), w.Op(kAdd2), w.Op(kSetMember);
    P({"this"}), w.Op(kGetVar), P({"txtOption"}), w.Op(kGetMember), P({"_x", "extern"}), w.Op(kGetVar), P({"_dx"});
    w.Op(kGetMember), w.Op(kToNumber), w.Op(kSetMember);
    // this.mcHotButtons._alpha = (i === "") * 100;
    P({"this"}), w.Op(kGetVar), P({"mcHotButtons"}), w.Op(kGetMember), P({"_alpha", "i"}), w.Op(kGetVar), P({""});
    w.Op(kStrictEquals), P({100}), w.Op(kMultiply), w.Op(kSetMember);
    // this.mcHotButtons.gotoAndStop(Number(msgLST[0]));
    P({"msgLST"}), w.Op(kGetVar), P({0}), w.Op(kGetMember), w.Op(kToNumber), P({1, "this"}), w.Op(kGetVar);
    P({"mcHotButtons"}), w.Op(kGetMember), P({"gotoAndStop"}), w.Op(kCallMethod), w.Op(kPop);
    // the prompt for the mouse: i = new Object(); i.x = 0; i.y = 0; this.localToGlobal(i);
    // extern["_d" + n + "," + msgLST[0] + "," + i.x + "," + i.y + "," + (this.txtOption._x +
    // this.txtOption.textWidth)];
    P({"i", 0, "Object"}), w.Op(kNew), w.Op(kSetVar);
    P({"i"}), w.Op(kGetVar), P({"x", 0}), w.Op(kSetMember);
    P({"i"}), w.Op(kGetVar), P({"y", 0}), w.Op(kSetMember);
    P({"i"}), w.Op(kGetVar), P({1, "this"}), w.Op(kGetVar), P({"localToGlobal"}), w.Op(kCallMethod), w.Op(kPop);
    P({"extern"}), w.Op(kGetVar), P({"_d", n}), w.Op(kAdd2), P({","}), w.Op(kAdd2), P({"msgLST"}), w.Op(kGetVar);
    P({0}), w.Op(kGetMember), w.Op(kAdd2);
    for (const char* axis : {"x", "y"}) {
        P({","}), w.Op(kAdd2), P({"i"}), w.Op(kGetVar), P({axis}), w.Op(kGetMember), w.Op(kAdd2);
    }
    P({","}), w.Op(kAdd2), P({"this"}), w.Op(kGetVar), P({"txtOption"}), w.Op(kGetMember), P({"_x"});
    w.Op(kGetMember), P({"this"}), w.Op(kGetVar), P({"txtOption"}), w.Op(kGetMember), P({"textWidth"});
    w.Op(kGetMember), w.Op(kAdd2), w.Op(kAdd2), w.Op(kGetMember), w.Op(kPop);
    for (size_t b : hidden) w.Land(b, w.code.size());
    w.Op(kEnd);
}

}  // namespace

std::vector<apt::Rewrite> apt::HelpBarRewrites() {
    std::vector<Rewrite> r;
    r.push_back({"help bar",
                 {kHelpBarBlock},
                 0x289E7003,
                 {{"ICON COUNT = ", "platformIcon"},
                  {"NEW ICON = ", "_visible"},
                  {"What is my FRAME LABEL ? ", "extern"},
                  {"input to the filter function = ", "_gap"},
                  {"totalLenghtOfHBMessages", "_start"},
                  {"enableLocalization", "_b"},
                  {"filtered_text", ","}},
                 {{{kHide + kBody, 0x10C, false}, WriteHide},
                  {{kDisplayIcons + kBody, 0x254, false}, WriteDisplayIcons},
                  {{kShow + kBody, 0x1C2, false}, WriteShow},
                  {{kFindStartX + kBody, 0x132, false}, WriteFindStartX}}});
    Rewrite choices{"dialog",
                    {std::begin(kChoices), std::end(kChoices)},
                    0xD522C799,
                    {{"after split = ", "extern"}, {"One = ", "_"}, {"And Two = ", "_x"},
                     {"GET and DSP Dialog MSG ", "_dx"}, {"after split = ", "_d"}, {"after split = ", "localToGlobal"},
                     {"after split = ", "textWidth"}, {"And Two = ", "Object"}, {"And Two = ", "x"},
                     {"And Two = ", "y"}},
                    {}};
    for (int n = 1; n <= 4; n++)
        choices.code.push_back({{kChoices[n - 1], kChoiceSize, true}, [n](Writer& w) { WriteChoice(w, n); }});
    r.push_back(std::move(choices));
    return r;
}

namespace {

// the key an icon's button is pressed with in the menus: A's is use's or reload's when it has none of its own, X's
// crouch's (common/pc_input.cpp)
std::string MenuKey(int type) {
    constexpr int kTypes[] = {kA, kB, kX, kY};
    if (type < 1 || type > 4) return {};
    int action = kTypes[type - 1];
    std::string key = FirstKey(action);
    if (key.empty() && action == kA) key = FirstKey(kUse);
    if (key.empty() && action == kA) key = FirstKey(kReload);
    if (key.empty() && action == kX) key = FirstKey(kCrouch);
    return key;
}

}  // namespace

// extern's variables: "_1" to "_4", the button type's key on the keys; the help bar's "_gap" after a label and
// "_start", -2 times what is added to the bar's start, and the dialog choices' label's x, "_dx": on a controller as
// the game has them (the gap 60, the start 26, the x -211.4), on the keys without the glyph's room (the glyph 32 wide,
// centred at the icon's left, the label at 18; a choice's glyph at -230.15). And the menus' mouse's (game/menus.cpp)
X86_EXTERN_FUNC(__imp__sub_00170770);
X86_FUNC(sub_00170770) {
    const char* name = reinterpret_cast<const char*>(base + U32(base, ctx.esp + 4));
    bool keys = UsingKeyboard();
    std::string value;
    if (MenuExtern(name, value)) {
        // the menus' mouse
    } else if (!std::strcmp(name, "_gap")) {
        value = keys ? "30" : "60";
    } else if (!std::strcmp(name, "_start")) {
        value = keys ? "8" : "-52";
    } else if (!std::strcmp(name, "_dx")) {
        value = keys ? "-244" : "-211.4";
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
