#include "gvn/mouse.h"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <tuple>

namespace gvn {
const wchar_t *mouse_gesture_name(MouseGesture gesture) {
    static constexpr const wchar_t *names[]{
        L"左クリック",       L"右クリック",       L"中クリック",       L"X1クリック",
        L"X2クリック",       L"左ダブルクリック", L"右ダブルクリック", L"中ダブルクリック",
        L"X1ダブルクリック", L"X2ダブルクリック", L"左ドラッグ",       L"右ドラッグ",
        L"中ドラッグ",       L"X1ドラッグ",       L"X2ドラッグ",       L"ホイール上",
        L"ホイール下",       L"水平ホイール左",   L"水平ホイール右",   L"ホバー（移動）"};
    const auto i = static_cast<unsigned>(gesture);
    return i < static_cast<unsigned>(MouseGesture::Count) ? names[i] : L"不明な操作";
}
unsigned mouse_button(MouseGesture gesture) {
    const auto i = static_cast<unsigned>(gesture);
    return i < 15 ? 1u << (i % 5) : 0;
}
bool mouse_drag(MouseGesture gesture) {
    return gesture >= MouseGesture::LeftDrag && gesture <= MouseGesture::X2Drag;
}
std::vector<CommandInfo> mouse_actions(MouseGesture gesture) {
    std::vector<CommandInfo> result{{MouseNone, L"何もしない"}};
    if (mouse_drag(gesture)) {
        for (const auto id : {MousePan, MouseZoom, MouseSelect, MouseFile})
            result.push_back({id, mouse_action_name(id)});
        if (gesture == MouseGesture::LeftDrag)
            result.push_back({MouseWindow, mouse_action_name(MouseWindow)});
    } else if (gesture == MouseGesture::Hover)
        result.push_back({MouseHover, mouse_action_name(MouseHover)});
    else {
        for (const auto &command : command_catalog())
            result.push_back(command);
        result.push_back({MouseZoomIn, mouse_action_name(MouseZoomIn)});
        result.push_back({MouseZoomOut, mouse_action_name(MouseZoomOut)});
    }
    return result;
}
const wchar_t *mouse_action_name(unsigned action) {
    switch (action) {
    case MouseNone:
        return L"何もしない";
    case MouseInherit:
        return L"標準操作を引き継ぐ";
    case MousePan:
        return L"画像をスクロール";
    case MouseZoom:
        return L"ドラッグでズーム";
    case MouseSelect:
        return L"選択ツールで範囲を選択（閲覧時は矩形）";
    case MouseFile:
        return L"ファイルとしてOLEドラッグ";
    case MouseWindow:
        return L"ウィンドウ移動（システム標準）";
    case MouseHover:
        return L"ホバースクロール";
    case MouseZoomIn:
        return L"マウス位置を中心に拡大";
    case MouseZoomOut:
        return L"マウス位置を中心に縮小";
    default:
        return command_name(action);
    }
}
std::wstring mouse_held_name(unsigned held) {
    if (!held)
        return L"他のボタンなし";
    std::wstring result;
    for (unsigned i = 0; i < 5; ++i)
        if (held & (1u << i)) {
            if (!result.empty())
                result += L" + ";
            result += std::vector<const wchar_t *>{L"左", L"右", L"中", L"X1", L"X2"}[i];
        }
    return result + L"を押しながら";
}
std::wstring mouse_modifiers_name(unsigned modifiers) {
    if (!modifiers)
        return L"修飾キーなし";
    std::wstring result;
    for (unsigned i = 0; i < 3; ++i)
        if (modifiers & (1u << i)) {
            if (!result.empty())
                result += L" + ";
            result += std::vector<const wchar_t *>{L"Ctrl", L"Shift", L"Alt"}[i];
        }
    return result;
}
MouseSettings::MouseSettings() {
    using G = MouseGesture;
    bindings = {{G::LeftDrag, 0, 0, MousePan},
                {G::LeftDrag, 0, 2, MouseSelect},
                {G::LeftDrag, 0, 4, MouseFile},
                {G::LeftDouble, 0, 0, Fullscreen},
                {G::RightClick, 0, 0, PopupAll},
                {G::MiddleClick, 0, 0, Exit},
                {G::X1Click, 0, 0, Previous},
                {G::X2Click, 0, 0, Next},
                {G::WheelUp, 0, 0, MouseZoomIn},
                {G::WheelDown, 0, 0, MouseZoomOut},
                {G::WheelUp, 0, 1, MouseZoomIn},
                {G::WheelDown, 0, 1, MouseZoomOut},
                {G::WheelUp, mouse_left, 0, Next},
                {G::WheelDown, mouse_left, 0, Previous},
                {G::WheelUp, mouse_right, 0, ScrollLeft},
                {G::WheelDown, mouse_right, 0, ScrollRight},
                {G::WheelLeft, 0, 0, ScrollLeft},
                {G::WheelRight, 0, 0, ScrollRight}};
}
unsigned MouseSettings::exact(MouseGesture gesture, unsigned held, unsigned modifiers) const {
    const auto it = std::find_if(bindings.begin(), bindings.end(), [&](const auto &b) {
        return b.gesture == gesture && b.held == held && b.modifiers == modifiers;
    });
    return it == bindings.end() ? MouseInherit : it->action;
}
unsigned MouseSettings::action(MouseGesture gesture, unsigned held, unsigned modifiers) const {
    held &= ~mouse_button(gesture);
    if (exact(MouseGesture::LeftDrag, 0, 0) == MouseWindow &&
        (gesture == MouseGesture::LeftClick || (held & mouse_left)))
        return MouseNone;
    for (auto [h, m] : {std::pair{held, modifiers}, {held, 0u}, {0u, modifiers}, {0u, 0u}}) {
        const auto value = exact(gesture, h, m);
        if (value != MouseInherit)
            return value;
    }
    return MouseNone;
}
bool MouseSettings::valid() const {
    std::set<std::tuple<unsigned, unsigned, unsigned>> used;
    for (const auto &b : bindings) {
        if (b.gesture >= MouseGesture::Count || b.held > 31 || b.modifiers > 7 ||
            (b.held & mouse_button(b.gesture)) ||
            !used.emplace(static_cast<unsigned>(b.gesture), b.held, b.modifiers).second)
            return false;
        const auto choices = mouse_actions(b.gesture);
        if (std::none_of(choices.begin(), choices.end(), [&](const auto &c) { return c.id == b.action; }))
            return false;
        if (mouse_drag(b.gesture) && b.action)
            for (const auto &other : bindings)
                if (other.gesture != b.gesture && mouse_drag(other.gesture) && other.action == b.action &&
                    other.held == b.held && other.modifiers == b.modifiers)
                    return false;
    }
    return true;
}
void MouseSettings::assign(MouseGesture gesture, unsigned held, unsigned modifiers, unsigned action) {
    auto next = *this;
    const auto matches = [&](const auto &b) {
        return b.gesture == gesture && b.held == held && b.modifiers == modifiers;
    };
    auto existing = std::find_if(next.bindings.begin(), next.bindings.end(), matches);
    if (action == MouseInherit)
        std::erase_if(next.bindings, matches);
    else if (existing != next.bindings.end())
        existing->action = action;
    else
        next.bindings.push_back({gesture, held, modifiers, action});
    if (gesture >= MouseGesture::Count || held > 31 || modifiers > 7 || (held & mouse_button(gesture)) ||
        !next.valid())
        throw std::invalid_argument("Invalid or conflicting mouse binding");
    *this = std::move(next);
}
void MouseSettings::basic_wheel(bool zoom) {
    assign(MouseGesture::WheelUp, 0, 0, zoom ? MouseZoomIn : static_cast<unsigned>(Previous));
    assign(MouseGesture::WheelDown, 0, 0, zoom ? MouseZoomOut : static_cast<unsigned>(Next));
}
void MouseSettings::basic_middle(int action) {
    assign(MouseGesture::MiddleClick, 0, 0, action == 1 ? Exit : action == 2 ? Fullscreen : MouseNone);
}
int MouseSettings::wheel_preset() const {
    const auto up = action(MouseGesture::WheelUp, 0, 0), down = action(MouseGesture::WheelDown, 0, 0);
    return up == Previous && down == Next ? 0 : up == MouseZoomIn && down == MouseZoomOut ? 1 : 2;
}
int MouseSettings::middle_preset() const {
    const auto value = action(MouseGesture::MiddleClick, 0, 0);
    return value == MouseNone ? 0 : value == Exit ? 1 : value == Fullscreen ? 2 : 3;
}
} // namespace gvn
