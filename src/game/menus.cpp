// The keys the menus take on their own (common/pc_input.h MenuKeys): the move keys as the d-pad and crouch's as X,
// given to the menus alone, so in play the game never sees them as the d-pad (the d-pad's up there is the grenade).
// The menus (the front end, the pause menu, the dialogs: EA's Apt movies, game/helpbar.cpp) take a controller's
// buttons as key events, sent for what changed (sub_001789F0, each frame for each port), from sub_001779D0 (stdcall
// (port, released, pressed), on the menus): the port's buttons as the menus' bits (the d-pad's up 0x1, down 0x2, left
// 0x4, right 0x8, A 0x10, X 0x20, Y 0x40, B 0x80 and so on), kept at 0x51FB88 + port * 4, and those released and
// pressed since the last time. Here port 0's have the keys' bits too, and the right mouse button's B.
//
// And the mouse (common/pc_input.h MenuPointer): each key event goes to the movies (sub_002775D0, cdecl (key,
// released, controller: the port + 2), queued for the next frame), where every clip with an onClipEvent(keyDown) runs
// it and Key.getCode() reads the key: the controller's small numbers made the movies' codes (the d-pad's up 14 made
// 38, down 15 40, left 1 37, right 2 39, A 302 as it is: game/helpbar.cpp's KEYCODE), any other as it is. A menu
// (cafela/cafela_menu.big, stdMenuClass, every screen's) has such a clip event, calling its getKey(): which with the
// menu enabled moves the focus on the d-pad (nextButton) and presses the control focused on A (executeCommand). Many
// screens' own handlers then look at the key too: the profiles' list sets the help bar for the profile focused on the
// d-pad's up and down, the audio options apply a volume changed on left and right.
//
// The mouse sends codes of its own, while the menus take buttons: 603 as it moves, 604 for a click, 605 as it moves
// with the button held. getKey() is written anew to give them to nextButton() (as 301, 302 and 303, numbers the movie
// has), written anew too, its d-pad as it was and the mouse: on a move or a click the control under the cursor
// (extern's "_mx" and "_my", the stage's) takes the focus as on the d-pad, its sound and all (focusChangeManagement):
// of the visible, unlocked ones that hold it the nearest (their boxes overlap), found by rollCall() (a roll call of the
// controls that did nothing, written anew, and init() no longer calls it) with setLock() (nothing calls it) written
// anew as the test of one. The cursor holds a control when its box does (extern's "_hit" and the control's name,
// MenuExtern below) or, without one, as it hit-tests. Then, as the control is:
// - a scrolling list (the profiles): its line under the cursor focused by the d-pad's up or down, as many as it is
//   from the line focused, and a click presses A after;
// - a slider: a click on its bar or a drag set it where the cursor is, by its value put a step (delta) the other side
//   and the d-pad pressed once to step it there;
// - a button: a click presses A;
// - a spinner: a click on its left arrow (from 40 before its value's text to 50 into it) presses the d-pad's left,
//   anywhere else on it the d-pad's right.
// The presses are asked for through extern ("_key" and the movie's code, or for more than one press the code times
// 100 plus the times) and sent from here, one a frame, the mouse's codes held back meanwhile, so each screen's handler
// sees them as from a controller. Each other clip's handler ignores the mouse's codes as it does any key it has no use
// for. The code has the old functions' room and no more, so it keeps the cursor, the control and a value at hand in
// the registers (the movie has no entries for their values: four for traces no longer made are made them).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

#include "common/pc_input.h"
#include "game/apt_rewrite.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {

using namespace apt;
using clock = std::chrono::steady_clock;

constexpr uint32_t kButtons = 0x51FB88;  // each port's bits, as of the last time
constexpr uint32_t kA = 0x10;
constexpr uint32_t kSendKey = 0x2775D0;   // a key event for the movies, cdecl (key, released, controller)
constexpr uint32_t kKeysState = 0x51FB74;  // 12 while the menus take no buttons (sub_001789F0)
constexpr uint32_t kNoButtons = 12;
constexpr uint32_t kMove = 603, kClick = 604, kDrag = 605;

// cafela_menu.apt: its initaction block (+0x57C) holds stdMenuClass, each function's DefineFunction at def; init()'s
// call of rollCall() at +0x692. mainLite.apt (the menus in play: the pause menu, its options) has the same class, all
// of it 0x12004 further on
constexpr uint32_t kMenuBlock = 0x57C, kRollCall = 0x7A0, kGetKey = 0xC64, kNextButton = 0x13D0;
constexpr uint32_t kInitRollCall = 0x692, kSetLock = 0x1128;
constexpr uint32_t kMainLite = 0x12004;

using Values = std::initializer_list<Value>;
const Value kYes = Bool{true}, kNo = Bool{false}, kR0 = Reg{0}, kR1 = Reg{1}, kR2 = Reg{2}, kR3 = Reg{3};

// shorthands for the code
struct Asm {
    Writer& w;
    void P(Values v) { w.Values(v); }
    void Op(uint8_t op) { w.Op(op); }
    // name.member
    void Get(const char* name, const char* member) { P({name}), Op(kGetVar), P({member}), Op(kGetMember); }
    // the value on the stack to a register
    void Keep(int reg) { w.Store(reg), Op(kPop); }
    // var == v
    void Is(const char* var, Value v) { P({var}), Op(kGetVar), P({v}), Op(kEquals2); }
    // the cursor: reg0 = Number(extern._mx); reg1 = Number(extern._my);
    void Mouse() {
        Get("extern", "_mx"), Op(kToNumber), Keep(0);
        Get("extern", "_my"), Op(kToNumber), Keep(1);
    }
    void Sound() { P({"Navigate", 1, "_global"}), Op(kGetVar), P({"playMenuSound"}), Op(kCallMethod), Op(kPop); }
    // extern["_key" + n]: the d-pad or A pressed (MenuExtern)
    void Press(Value n) { P({"extern"}), Op(kGetVar), P({"_key", n}), Op(kAdd2), Op(kGetMember), Op(kPop); }
    // reg1 = new Object(); reg1.x = x; reg1.y = 0; clip.localToGlobal(reg1);  (x pushed by the function)
    template <typename F>
    void Global(Value clip, F x) {
        P({0, "Object"}), Op(kNew), Keep(1);
        P({kR1, "x"}), x(), Op(kSetMember);
        P({kR1, "y", 0}), Op(kSetMember);
        P({kR1, 1, clip, "localToGlobal"}), Op(kCallMethod), Op(kPop);
    }
};

// getKey(): the old one, its traces and the other consoles' buttons left out, and the mouse's codes to nextButton() (as
// 301 a move, 302 a click, 303 a drag), the cursor in reg0 and reg1
void WriteGetKey(Writer& w) {
    w.merge = true;
    Asm c{w};
    // if (_global.gStateHandler.getAlertStatus()) return undefined;
    c.P({0}), c.Get("_global", "gStateHandler"), c.P({"getAlertStatus"}), c.Op(kCallMethod), c.Op(kNot);
    size_t no_alert = w.Branch(kIf);
    c.P({Undefined{}}), c.Op(kReturn), w.Land(no_alert);
    // the cursor, reg0 and reg1, read for any key, the menu's taking it the menu there (game/menus.cpp's Start)
    c.Mouse();
    // var myKeyCode = Key.getCode(); if (!this.enable) return false;
    c.P({"myKeyCode", 0, "Key"}), c.Op(kGetVar), c.P({"getCode"}), c.Op(kCallMethod), c.Op(kDefineLocal);
    c.Get("this", "enable");
    size_t enabled = w.Branch(kIf);
    c.P({kNo}), c.Op(kReturn), w.Land(enabled);
    // var count = myKeyCode - 602;
    c.P({"count", "myKeyCode"}), c.Op(kGetVar), c.P({602}), c.Op(kSub), c.Op(kDefineLocal);
    // if (count > 0) { this.nextButton(300 + count); return false; }
    c.P({"count"}), c.Op(kGetVar), c.P({0}), c.Op(kGreater), c.Op(kNot);
    size_t keys = w.Branch(kIf);
    c.P({"count"}), c.Op(kGetVar), c.P({300}), c.Op(kAdd2), c.P({1, "this"}), c.Op(kGetVar);
    c.P({"nextButton"}), c.Op(kCallMethod), c.Op(kPop), c.P({kNo}), c.Op(kReturn);
    // _global.KEYCODE[myKeyCode], against "dUp", "dDown", "dLeft", "dRight" and "xboxA"
    w.Land(keys);
    c.Get("_global", "KEYCODE"), c.P({"myKeyCode"}), c.Op(kGetVar), c.Op(kGetMember);
    size_t to[5];
    const char* names[5] = {"dUp", "dDown", "dLeft", "dRight", "xboxA"};
    for (int i = 0; i < 5; i++) c.Op(kPushDuplicate), c.P({names[i]}), c.Op(kStrictEquals), to[i] = w.Branch(kIf);
    c.Op(kPop);
    // else if (this.forwardKE__param) this.forwardKeyEvent(myKeyCode);
    std::vector<size_t> selection;
    c.Get("this", "forwardKE__param"), c.Op(kNot);
    selection.push_back(w.Branch(kIf));
    c.P({"myKeyCode"}), c.Op(kGetVar), c.P({1, "this"}), c.Op(kGetVar), c.P({"forwardKeyEvent"}), c.Op(kCallMethod);
    c.Op(kPop), selection.push_back(w.Branch(kJump));
    // the d-pad: this.nextButton(0 up, 1 down, 2 left, 3 right);
    std::vector<size_t> next;
    for (int i = 0; i < 4; i++) {
        w.Land(to[i]), c.Op(kPop), c.P({i});
        if (i < 3) next.push_back(w.Branch(kJump));
    }
    for (size_t b : next) w.Land(b);
    c.P({1, "this"}), c.Op(kGetVar), c.P({"nextButton"}), c.Op(kCallMethod), c.Op(kPop);
    selection.push_back(w.Branch(kJump));
    // A: this.executeCommand();
    w.Land(to[4]), c.Op(kPop);
    c.P({0, "this"}), c.Op(kGetVar), c.P({"executeCommand"}), c.Op(kCallMethod), c.Op(kPop);
    // return this.get_selection();
    for (size_t b : selection) w.Land(b);
    c.P({0, "this"}), c.Op(kGetVar), c.P({"get_selection"}), c.Op(kCallMethod), c.Op(kReturn);
}

// rollCall(): the control under the cursor (reg0 and reg1), of the visible, unlocked ones holding it the nearest (its
// place, _x and _y, the stage's: the controls' boxes overlap, the footer's buttons' a long way), or false for none. A
// control holds it when its box does (extern's "_hit" and its name: "1", "0", "" for no box) or, without one, as it
// hit-tests
void WriteRollCall(Writer& w) {
    w.merge = true;
    Asm c{w};
    // reg3 = this.controls_param.length; var c1 = false;  (the nearest, at the square of l from the cursor)
    c.Get("this", "controls_param"), c.P({"length"}), c.Op(kGetMember), c.Keep(3);
    c.P({"c1", kNo}), c.Op(kDefineLocal);
    // while (--reg3 >= 0) { reg2 = eval(this.control_path_param + this.controls_param[reg3]);
    size_t loop = w.Mark();
    c.P({kR3}), c.Op(kDecrement), w.Store(3), c.P({0}), c.Op(kLess2), c.Op(kNot);
    size_t more = w.Branch(kIf);
    c.P({"c1"}), c.Op(kGetVar), c.Op(kReturn), w.Land(more);
    c.Get("this", "control_path_param"), c.Get("this", "controls_param"), c.P({kR3}), c.Op(kGetMember), c.Op(kAdd2);
    c.Op(kGetVar), c.Keep(2);
    // if (!this.setLock()) continue;  (reg2 under the cursor)
    c.P({0, "this"}), c.Op(kGetVar), c.P({"setLock"}), c.Op(kCallMethod), c.Op(kNot), w.Land(w.Branch(kIf), loop);
    // var len = (reg2._x - reg0)^2 + (reg2._y - reg1)^2; if (!c1 || len < l) { l = len; c1 = reg2; } }
    c.P({"len", kR2, "_x"}), c.Op(kGetMember), c.P({kR0}), c.Op(kSub), c.Op(kPushDuplicate), c.Op(kMultiply);
    c.P({kR2, "_y"}), c.Op(kGetMember), c.P({kR1}), c.Op(kSub), c.Op(kPushDuplicate), c.Op(kMultiply), c.Op(kAdd2);
    c.Op(kDefineLocal);
    c.P({"c1"}), c.Op(kGetVar), c.Op(kNot), c.P({"len"}), c.Op(kGetVar), c.P({"l"}), c.Op(kGetVar), c.Op(kLess2);
    c.Op(kOr), c.Op(kNot), w.Land(w.Branch(kIf), loop);
    c.P({"l", "len"}), c.Op(kGetVar), c.Op(kSetVar), c.P({"c1", kR2}), c.Op(kSetVar), w.Land(w.Branch(kJump), loop);
}

// setLock(), which nothing calls: reg2 under the cursor (reg0 and reg1), visible and unlocked. It holds the cursor when
// its box does (extern's "_hit" and its name: "1", "0", "" for no box) or, without one, as it hit-tests
void WriteHit(Writer& w) {
    w.merge = true;
    Asm c{w};
    // return (extern["_hit" + reg2._name] || reg2.hitTest(reg0, reg1, false)) && reg2._visible && !reg2.getLock();
    c.P({"extern"}), c.Op(kGetVar), c.P({"_hit", kR2, "_name"}), c.Op(kGetMember), c.Op(kAdd2), c.Op(kGetMember);
    c.Op(kPushDuplicate), c.P({""}), c.Op(kStrictEquals);
    size_t no_box = w.Branch(kIf);
    c.P({"1"}), c.Op(kStrictEquals);
    size_t boxed = w.Branch(kJump);
    w.Land(no_box), c.Op(kPop), c.P({kNo, kR1, kR0, 3, kR2, "hitTest"}), c.Op(kCallMethod);
    w.Land(boxed), c.P({kR2, "_visible"}), c.Op(kGetMember), c.Op(kAnd), c.P({0, kR2, "getLock"}), c.Op(kCallMethod);
    c.Op(kNot), c.Op(kAnd), c.Op(kReturn);
}

// init()'s this.rollCall(); jumped over
void WriteInitRollCall(Writer& w) { w.Land(w.Branch(kJump), w.where.size); }

// nextButton(upOrDown): the d-pad (0 up, 1 down, 2 left, 3 right) as it was, its traces left out, and the mouse on
// the focus (301 a move, 302 a click, 303 a drag)
void WriteNextButton(Writer& w) {
    w.merge = true;
    Asm c{w};
    std::vector<size_t> done;  // to return true
    auto IsId = [&](const char* id) { c.P({"cntrl_ID"}), c.Op(kGetVar), c.P({id}), c.Op(kStrictEquals); };
    auto Branch = [&](uint8_t op) { done.push_back(w.Branch(op)); };

    // if (upOrDown > 3) {  the mouse
    c.P({"upOrDown"}), c.Op(kGetVar), c.P({3}), c.Op(kGreater), c.Op(kNot);
    size_t keys = w.Branch(kIf);
    // a move or a click: the control under the cursor focused, as on the d-pad (focusChangeManagement), or for none
    // return true: if (upOrDown != 303) { reg2 = this.rollCall(); if (!reg2) return true; if (reg2 != this.focus)
    // this.focusChangeManagement(reg2, true); }
    c.Is("upOrDown", 303);
    size_t dragging = w.Branch(kIf);
    c.P({0, "this"}), c.Op(kGetVar), c.P({"rollCall"}), c.Op(kCallMethod), w.Store(2), c.Op(kNot), Branch(kIf);
    c.P({kR2}), c.Get("this", "focus"), c.Op(kEquals2);
    size_t focused = w.Branch(kIf);
    c.P({kYes, kR2, 2, "this"}), c.Op(kGetVar), c.P({"focusChangeManagement"}), c.Op(kCallMethod), c.Op(kPop);
    // reg2 = this.focus; var cntrl_ID = reg2.getID();
    w.Land(focused), w.Land(dragging), c.Mouse();
    c.Get("this", "focus"), c.Keep(2);
    c.P({"cntrl_ID", 0, kR2, "getID"}), c.Op(kCallMethod), c.Op(kDefineLocal);

    // a list, moved over or clicked: if (cntrl_ID === "ScrollingList" && upOrDown != 303) {
    IsId("ScrollingList"), c.Is("upOrDown", 303), c.Op(kNot), c.Op(kAnd), c.Op(kNot);
    size_t not_list = w.Branch(kIf);
    // the line under the cursor by its height (the lines' hit tests miss, inside the list): line n from its y, n times
    // lineHeight_param, half a line up to half a line down. reg3 = new Object(); reg3.x = 0; reg3.y = 0;
    // reg2.localToGlobal(reg3);  (the list's origin, global)
    c.P({0, "Object"}), c.Op(kNew), c.Keep(3);
    c.P({kR3, "x", 0}), c.Op(kSetMember), c.P({kR3, "y", 0}), c.Op(kSetMember);
    c.P({kR3, 1, kR2, "localToGlobal"}), c.Op(kCallMethod), c.Op(kPop);
    // reg1 = (2 * (reg1 - reg3.y) + reg2.lineHeight_param) / (2 * reg2.lineHeight_param);  (from n to n + 1 on line n)
    c.P({2, kR1, kR3, "y"}), c.Op(kGetMember), c.Op(kSub), c.Op(kMultiply), c.P({kR2, "lineHeight_param"});
    c.Op(kGetMember), c.Op(kAdd2), c.P({2, kR2, "lineHeight_param"}), c.Op(kGetMember), c.Op(kMultiply);
    c.Op(kDivide), c.Keep(1);
    // reg3 = reg2.dataSource_param.length - reg2.linePtr; if (!(reg1 < reg3)) return true;  (below the last)
    c.P({kR2, "dataSource_param"}), c.Op(kGetMember), c.P({"length"}), c.Op(kGetMember), c.P({kR2, "linePtr"});
    c.Op(kGetMember), c.Op(kSub), c.Keep(3), c.P({kR1, kR3}), c.Op(kLess2), c.Op(kNot), Branch(kIf);
    // do { if (--reg3 < 0) return true; } while (reg1 < reg3);
    size_t lines = w.Mark();
    c.P({kR3}), c.Op(kDecrement), w.Store(3), c.P({0}), c.Op(kLess2), Branch(kIf);
    c.P({kR1, kR3}), c.Op(kLess2), w.Land(w.Branch(kIf), lines);
    // reg3 -= reg2.focusFromLine: the d-pad's down reg3 times (40 * 100 + reg3), or up -reg3 times (38 * 100 - reg3)
    c.P({kR3, kR2, "focusFromLine"}), c.Op(kGetMember), c.Op(kSub), w.Store(3), c.P({0}), c.Op(kGreater);
    size_t down = w.Branch(kIf);
    c.P({kR3, 0}), c.Op(kLess2), c.Op(kNot);
    size_t there = w.Branch(kIf);
    c.P({38, 100}), c.Op(kMultiply), c.P({kR3}), c.Op(kSub);
    size_t press = w.Branch(kJump);
    w.Land(down), c.P({40, 100}), c.Op(kMultiply), c.P({kR3}), c.Op(kAdd2);
    w.Land(press), c.Keep(3), c.Press(kR3);
    // and A for a click; return true; }
    w.Land(there), c.Is("upOrDown", 302), c.Op(kNot), Branch(kIf);
    c.Press(302), Branch(kJump);

    // a slider, clicked or dragged: if (cntrl_ID === "std_slider" && upOrDown != 301) {
    w.Land(not_list), IsId("std_slider"), c.Is("upOrDown", 301), c.Op(kNot), c.Op(kAnd), c.Op(kNot);
    size_t not_slider = w.Branch(kIf);
    // reg3 = reg2.mcContent.Annunciation; its bar's left, global: reg1.x
    c.P({kR2, "mcContent"}), c.Op(kGetMember), c.P({"Annunciation"}), c.Op(kGetMember), c.Keep(3);
    c.Global(kR3, [&] { c.P({kR3, "bar"}), c.Op(kGetMember), c.P({"_x"}), c.Op(kGetMember); });
    // reg0 = 100 * (reg0 - reg1.x) / reg3.bar._width;  (where on the bar, as calc() has it)
    c.P({100, kR0, kR1, "x"}), c.Op(kGetMember), c.Op(kSub), c.Op(kMultiply), c.P({kR3, "bar"}), c.Op(kGetMember);
    c.P({"_width"}), c.Op(kGetMember), c.Op(kDivide), c.Keep(0);
    // if (upOrDown == 302 && reg0 < 0) return true;  (a click left of the bar, on its name, only focuses it)
    c.Is("upOrDown", 302), c.P({kR0, 0}), c.Op(kLess2), c.Op(kAnd), Branch(kIf);
    // if (reg2.x < reg0) { reg2.x = reg0 - reg2.delta; right; } else if (reg0 < reg2.x) { reg2.x = reg0 +
    // reg2.delta; left; } return true; }  (the slider's step keeps it in its range)
    auto Step = [&](uint8_t op, int key) {
        c.P({kR2, "x", kR0, kR2, "delta"}), c.Op(kGetMember), c.Op(op), c.Op(kSetMember), c.Press(key), Branch(kJump);
    };
    c.P({kR2, "x"}), c.Op(kGetMember), c.P({kR0}), c.Op(kLess2), c.Op(kNot);
    size_t not_right = w.Branch(kIf);
    Step(kSub, 39);
    w.Land(not_right), c.P({kR0, kR2, "x"}), c.Op(kGetMember), c.Op(kLess2), c.Op(kNot), Branch(kIf);
    Step(kAdd2, 37);

    // if (upOrDown != 302) return true; a button: A
    w.Land(not_slider), c.Is("upOrDown", 302), c.Op(kNot), Branch(kIf);
    IsId("stdButtonUI"), c.Op(kNot);
    size_t not_button = w.Branch(kIf);
    c.Press(302), Branch(kJump);
    // a spinner: reg3 = reg2.mcContent.spinnerText; if (reg3.myText == undefined) return true;
    w.Land(not_button), c.P({kR2, "mcContent"}), c.Op(kGetMember), c.P({"spinnerText"}), c.Op(kGetMember), c.Keep(3);
    c.P({kR3, "myText"}), c.Op(kGetMember), c.P({Undefined{}}), c.Op(kEquals2), Branch(kIf);
    // its value's left, global: reg1.x; if (reg1.x - 40 < reg0 && reg0 < reg1.x + 50) left; else right; return true;
    c.Global(kR3, [&] { c.P({kR3, "myText"}), c.Op(kGetMember), c.P({"_x"}), c.Op(kGetMember); });
    c.P({kR1, "x"}), c.Op(kGetMember), c.P({40}), c.Op(kSub), c.P({kR0}), c.Op(kLess2), c.P({kR0, kR1, "x"});
    c.Op(kGetMember), c.P({50}), c.Op(kAdd2), c.Op(kLess2), c.Op(kAnd), c.Op(kNot);
    size_t right = w.Branch(kIf);
    c.Press(37), Branch(kJump);
    w.Land(right), c.Press(39), Branch(kJump);

    // the d-pad: reg2 = this.focus; var button = reg2.neighbors_param[upOrDown];
    w.Land(keys), c.Get("this", "focus"), c.Keep(2);
    c.P({"button", kR2, "neighbors_param"}), c.Op(kGetMember), c.P({"upOrDown"}), c.Op(kGetVar), c.Op(kGetMember);
    c.Op(kDefineLocal);
    // reg3 = eval((reg1 = this.control_path_param) + button);  (reg3 the next_but)
    c.Get("this", "control_path_param"), w.Store(1), c.P({"button"}), c.Op(kGetVar), c.Op(kAdd2), c.Op(kGetVar);
    c.Keep(3);
    // var cCount = this.componentsInChain(upOrDown); var i = 0;
    c.P({"cCount", "upOrDown"}), c.Op(kGetVar), c.P({1, "this"}), c.Op(kGetVar), c.P({"componentsInChain"});
    c.Op(kCallMethod), c.Op(kDefineLocal);
    c.P({"i", 0}), c.Op(kDefineLocal);
    // while (reg3.getLock() && i < cCount) {
    size_t chain = w.Mark();
    c.P({0, kR3, "getLock"}), c.Op(kCallMethod), c.P({"i"}), c.Op(kGetVar), c.P({"cCount"}), c.Op(kGetVar);
    c.Op(kLess2), c.Op(kAnd), c.Op(kNot);
    size_t unlocked = w.Branch(kIf);
    // reg3 = eval("_level0.mcFG." + reg3.neighbors_param[upOrDown]); i++; }
    c.P({"_level0.mcFG.", kR3, "neighbors_param"}), c.Op(kGetMember), c.P({"upOrDown"}), c.Op(kGetVar);
    c.Op(kGetMember), c.Op(kAdd2), c.Op(kGetVar), c.Keep(3);
    c.P({"i", "i"}), c.Op(kGetVar), c.Op(kIncrement), c.Op(kSetVar);
    w.Land(w.Branch(kJump), chain);
    // if (!(i < cCount)) return false;
    w.Land(unlocked), c.P({"i"}), c.Op(kGetVar), c.P({"cCount"}), c.Op(kGetVar), c.Op(kLess2);
    size_t found = w.Branch(kIf);
    c.P({kNo}), c.Op(kReturn), w.Land(found);
    // if (!(reg3 != undefined && button != undefined && reg3 != reg1)) return true;
    c.P({kR3, Undefined{}}), c.Op(kEquals2), c.Op(kNot), c.Is("button", Undefined{}), c.Op(kNot), c.Op(kAnd);
    c.P({kR3, kR1}), c.Op(kEquals2), c.Op(kNot), c.Op(kAnd), c.Op(kNot), Branch(kIf);
    // var cntrl_ID = reg2.getID();
    c.P({"cntrl_ID", 0, kR2, "getID"}), c.Op(kCallMethod), c.Op(kDefineLocal);
    // the focus's value, or its position, before and after a call (kept from the registers, the screen's code may run
    // in it): var before = reg2.method(); ... reg2.method() == before
    auto Before = [&](const char* method) { c.P({"before", 0, kR2, method}), c.Op(kCallMethod), c.Op(kDefineLocal); };
    auto After = [&](const char* method) {
        c.P({"before"}), c.Op(kGetVar), c.P({0, kR2, method}), c.Op(kCallMethod), c.Op(kEquals2);
    };
    // a scrolling list or an 8 letter text entry: reg2.scroll(upOrDown); the sound for a list's moved, a text
    // entry's moved or its up or down
    auto Scroll = [&](const char* id, const char* position, bool vertical) {
        IsId(id), c.Op(kNot);
        size_t other = w.Branch(kIf);
        Before(position);
        c.P({"upOrDown"}), c.Op(kGetVar), c.P({1, kR2, "scroll"}), c.Op(kCallMethod), c.Op(kPop);
        After(position);
        if (vertical) c.Op(kNot), c.P({"upOrDown"}), c.Op(kGetVar), c.P({2}), c.Op(kLess2), c.Op(kOr), c.Op(kNot);
        Branch(kIf);
        c.Sound(), Branch(kJump);
        w.Land(other);
    };
    Scroll("ScrollingList", "getFocusedLineNumber", false);
    Scroll("stdTxtEntryUI", "getCharPos", true);
    // a password: reg2.processKey("X");
    IsId("PassWordEntry"), c.Op(kNot);
    size_t not_password = w.Branch(kIf);
    c.P({"X", 1, kR2, "processKey"}), c.Op(kCallMethod), c.Op(kPop), Branch(kJump);
    // the others: if (reg3 != reg2) { this.focusChangeManagement(reg3, true); return true; }
    w.Land(not_password), c.P({kR3, kR2}), c.Op(kEquals2);
    size_t same = w.Branch(kIf);
    c.P({kYes, kR3, 2, "this"}), c.Op(kGetVar), c.P({"focusChangeManagement"}), c.Op(kCallMethod), c.Op(kPop);
    Branch(kJump);
    // the focus its own neighbour: a button as it is, else reg2.advance(upOrDown); and the sound when its value
    // changed
    w.Land(same), IsId("stdButtonUI"), Branch(kIf);
    Before("get_selection");
    c.P({"upOrDown"}), c.Op(kGetVar), c.P({1, kR2, "advance"}), c.Op(kCallMethod), c.Op(kPop);
    After("get_selection"), Branch(kIf);
    c.Sound();
    // return true;
    for (size_t b : done) w.Land(b);
    c.P({kYes}), c.Op(kReturn);
}

// The boxes for controls whose hit test the cursor finds too small, on the stage (extern's "_hit"): the campaigns on
// the map (scrns/title.apt's campmenu), each one's name and its dot (the name's text left, right or centre aligned in
// its 467 wide text box, the box here about as wide as the name shows)
struct Box {
    const char* name;
    float left, top, right, bottom;
};
constexpr Box kBoxes[] = {
    {"StNazaire", 175, 200, 390, 268},
    {"NorthAfrica", 335, 275, 560, 335},
    {"Russia", 530, 175, 695, 233},
    {"BattleOfBulge", 190, 130, 490, 192},
};

// the presses the movies ask for, the controller's small numbers, sent one a frame; given up when the menus have not
// taken them a while (a screen gone)
constexpr auto kPressesStale = std::chrono::milliseconds(500);
std::mutex g_presses_lock;
std::deque<uint32_t> g_presses;
clock::time_point g_presses_at;
// a mouse's code sent and not yet taken (getKey() reads extern's "_mx" first, then asks for its presses), and when;
// given up after a while (no menu took it)
constexpr auto kMouseUntaken = std::chrono::milliseconds(250);
bool g_mouse_waiting = false;
clock::time_point g_mouse_sent;
uint32_t g_mouse_code = 0;
constexpr uint32_t kStart = 301;  // the controller's small number for start, the movies' code as it is

// The prompts for buttons the help bar and the dialogs show (game/helpbar.cpp): a click on one presses its button
// (the type 1 A, 2 B, 3 X, 4 Y). The help bar's, extern's "_b" and the type, x and its label's width (y 430, the bar
// on the stage as it is, its glyph centred at x, its label at 18), all gone for "_b" alone; a dialog's choice n's,
// "_d", n, the type and its x, y and its label's right from there (its glyph centred at -230), gone for "_d" and n
// alone, and all once the dialog closes (the menus taking buttons again, or reading the cursor)
struct Prompt {
    uint32_t key;  // the controller's small number, 0 for none
    float left, top, right, bottom;
};
std::vector<Prompt> g_bar;
Prompt g_choices[4];
bool g_dialog = false;  // as of the last frame: the menus taking no buttons, a dialog up

uint32_t PromptKey(int type) {
    switch (type) {
        case 1: return 302;
        case 2: return 303;
        case 3: return 305;
        case 4: return 304;
        default: return 0;
    }
}

// a movie's key code as the controller's small number, 0 for none
uint32_t SmallKey(int code) {
    switch (code) {
        case 38: return 14;
        case 40: return 15;
        case 37: return 1;
        case 39: return 2;
        case 302:
        case 303:
        case 304:
        case 305: return uint32_t(code);
        default: return 0;
    }
}

// a key event for the movies, pressed and let go, from port 0
void SendKey(X86Context& ctx, uint32_t key) {
    kernel::CallGuest(ctx, kSendKey, {key, 0, 2});
    kernel::CallGuest(ctx, kSendKey, {key, 1, 2});
}

}  // namespace

std::vector<apt::Rewrite> apt::MenuRewrites() {
    // the names the code needs, and the registers' values, over the traces no longer made (and those given to
    // echo2Dsp() by the code left, which does nothing; keyNameIs(), which nothing calls), each in the .const once
    auto menu = [](const char* name, uint32_t at, uint32_t crc) {
        return Rewrite{name,
                       {at + kMenuBlock},
                       crc,
                       {{"MEN Instantiate menu class ", "hitTest"},
                        {"MEN Roll call --> ", "extern"},
                        {" <-- controls associated with this MENU", "_visible"},
                        {"=== control is set to eval: _level0.mcFG.", "_mx"},
                        {" control...", "_my"},
                        {"=== Pointer to neigbhor button         --------------------------------->",
                         "dataSource_param"},
                        {"focus = spinner type control : should each control have a typeID?: ", "localToGlobal"},
                        {"Found a locked component...searching for an unlocked component...", "focusFromLine"},
                        {"=== what's the path?         --------------------------------->", "Annunciation"},
                        {"=== Who has focus         --------------------------------->", "spinnerText"},
                        {"? I can't tell what to do?", "mcContent"},
                        {"focus = Slider  control ", "linePtr"},
                        {"focus = Pass Word Entry ", "_width"},
                        {"Process keycode =  ", "myText"},
                        {"next_button = ", "delta"},
                        {"focus = scrolling 8 char text input  control ", "_y"},
                        {"focus = scrolling list  control ", "lineHeight_param"},
                        {"Control ID = ", "_name"},
                        {"next_button", "_key"},
                        {"myKeyName", "_hit"},
                        {"NEW FOCUS", "bar"},
                        {"Std Menu", "_x"},
                        {"Menu", "x"},
                        {"MENU Kill", "y"},
                        {"EXE ", "1"}},
                       {{{at + kInitRollCall, 0x18, false}, WriteInitRollCall},
                        {{at + kRollCall + kBody, 0x188, false}, WriteRollCall},
                        {{at + kGetKey + kBody, 0x2D1, false}, WriteGetKey},
                        {{at + kSetLock + kBody, 0xCA, false}, WriteHit},
                        {{at + kNextButton + kBody, 0x9A5, false}, WriteNextButton}},
                       {{"MEN we have N Items where N = ", 0},
                        {"MEN Set ", 1},
                        {" state for this control = ", 2},
                        {"lock button ", 3}}};
    };
    return {menu("menu", 0, 0x8465B7EF), menu("menu in play", kMainLite, 0xF3616C5D)};
}

bool apt::MenuExtern(const char* name, std::string& value) {
    float x = -1000.0f, y = -1000.0f;  // off the stage when there is none
    pc_input::MenuPointer(x, y);
    if (!std::strcmp(name, "_mx") || !std::strcmp(name, "_my")) {
        // a menu reading it (getKey(), with no dialog up: in play the menus take buttons with one up too): the
        // dialog's choices gone
        if (name[2] == 'x') {
            std::lock_guard lock(g_presses_lock);
            g_mouse_waiting = false;
            std::fill(std::begin(g_choices), std::end(g_choices), Prompt{});
        }
        value = std::to_string(name[2] == 'x' ? x : y);
        return true;
    }
    // "_hit" and a control's name: "1" with the cursor in its box, "0" out of it, "" for no box
    if (!std::strncmp(name, "_hit", 4)) {
        value.clear();
        for (const Box& b : kBoxes)
            if (!std::strcmp(name + 4, b.name))
                value = x >= b.left && x < b.right && y >= b.top && y < b.bottom ? "1" : "0";
        return true;
    }
    // the prompts
    if (name[0] == '_' && name[1] == 'b' && (!name[2] || (name[2] >= '0' && name[2] <= '9'))) {
        int type = 0;
        float px = 0, width = 0;
        std::lock_guard lock(g_presses_lock);
        if (!name[2]) g_bar.clear();
        else if (std::sscanf(name + 2, "%d,%f,%f", &type, &px, &width) == 3 && PromptKey(type))
            g_bar.push_back({PromptKey(type), px - 18, 430 - 16, px + 20 + width, 430 + 16});
        value.clear();
        return true;
    }
    if (name[0] == '_' && name[1] == 'd' && name[2] >= '1' && name[2] <= '4') {
        int type = 0;
        float px = 0, py = 0, right = 0;
        Prompt& choice = g_choices[name[2] - '1'];
        std::lock_guard lock(g_presses_lock);
        choice = {};
        if (name[3] && std::sscanf(name + 3, ",%d,%f,%f,%f", &type, &px, &py, &right) == 4)
            choice = {PromptKey(type), px - 250, py - 16, px + right + 16, py + 16};
        value.clear();
        return true;
    }
    // "_key" and a key's code (once), or its code times 100 plus the times to press it
    if (!std::strncmp(name, "_key", 4)) {
        long n = std::strtol(name + 4, nullptr, 10), times = 1;
        if (n >= 1000) times = n % 100, n /= 100;
        uint32_t key = SmallKey(int(n));
        std::lock_guard lock(g_presses_lock);
        if (g_presses.empty()) g_presses_at = clock::now();
        for (long i = 0; key && i < times; i++) g_presses.push_back(key);
        value.clear();
        return true;
    }
    return false;
}

X86_EXTERN_FUNC(__imp__sub_001779D0);
X86_FUNC(sub_001779D0) {
    uint32_t self = ctx.ecx, port = U32(base, ctx.esp + 4);
    uint32_t released = U32(base, ctx.esp + 8), pressed = U32(base, ctx.esp + 12);
    uint32_t before = port < 4 ? U32(base, kButtons + 4 * port) : 0;
    __imp__sub_001779D0(ctx, base);
    if (port != 0) return;
    uint32_t now = U32(base, kButtons) | pc_input::MenuKeys();
    U32(base, released) = before & ~now;
    U32(base, pressed) = now & ~before;
    U32(base, kButtons) = now;
    // A pressed makes its port the menus' (as the game does), and so does any press from here: the mouse's (else
    // the single player's controller locked to another port, FSCommand:lockSPController, nothing works after)
    if (U32(base, pressed) & kA) U32(base, self + 0x18) = port;
    auto send = [&](uint32_t key) {
        U32(base, self + 0x18) = port;
        SendKey(ctx, key);
    };
    // the mouse, while the menus take buttons (or a dialog is up, for its prompts): the presses it asked for first,
    // one a frame, what it does meanwhile left for after
    bool taking = U32(base, kKeysState) != kNoButtons;
    uint32_t key = 0;
    {
        std::lock_guard lock(g_presses_lock);
        if (g_dialog == taking) {
            g_presses.clear();
            if (taking) std::fill(std::begin(g_choices), std::end(g_choices), Prompt{});
            g_dialog = !taking;
        }
        if (clock::now() - g_presses_at > kPressesStale) g_presses.clear();
        if (!g_presses.empty()) {
            key = g_presses.front();
            g_presses.pop_front();
            g_presses_at = clock::now();
        }
    }
    if (key) {
        send(key);
        return;
    }
    // the mouse's next code once the last is taken, so the presses it asks for come first; a click no menu took (no
    // menu there: the title's press start, a screen to go on from) is start
    bool untaken = false;
    {
        std::lock_guard lock(g_presses_lock);
        if (g_mouse_waiting && clock::now() - g_mouse_sent < kMouseUntaken) return;
        // (not with a dialog up: in play the menus ignore the mouse then)
        bool dialog = std::any_of(std::begin(g_choices), std::end(g_choices), [](const Prompt& p) { return p.key; });
        untaken = g_mouse_waiting && g_mouse_code == kClick && !dialog;
        g_mouse_waiting = false;
    }
    if (untaken && taking) {
        send(kStart);
        return;
    }
    pc_input::MenuMouse mouse = pc_input::TakeMenuMouse();
    float x = 0, y = 0;
    pc_input::MenuPointer(x, y);
    // a click on a prompt: its button, a dialog's choices with one up, else the help bar's in a menu
    if (mouse.clicked) {
        std::lock_guard lock(g_presses_lock);
        auto in = [&](const Prompt& p) { return p.key && x >= p.left && x < p.right && y >= p.top && y < p.bottom; };
        const Prompt* hit = nullptr;
        for (const Prompt& p : g_choices)
            if (in(p)) hit = &p;
        if (taking && !hit)
            for (const Prompt& p : g_bar)
                if (in(p)) hit = &p;
        if (hit) key = hit->key;
    }
    if (key) {
        send(key);
        return;
    }
    if (!taking) return;
    uint32_t code = mouse.clicked ? kClick : mouse.dragged ? kDrag : mouse.moved ? kMove : 0;
    if (!code) return;
    {
        std::lock_guard lock(g_presses_lock);
        g_mouse_waiting = true;
        g_mouse_sent = clock::now();
        g_mouse_code = code;
    }
    send(code);
}
