#pragma once
#include "commands.h"
#include <cstdint>

namespace gvn {
enum class MouseGesture : unsigned {
    LeftClick,
    RightClick,
    MiddleClick,
    X1Click,
    X2Click,
    LeftDouble,
    RightDouble,
    MiddleDouble,
    X1Double,
    X2Double,
    LeftDrag,
    RightDrag,
    MiddleDrag,
    X1Drag,
    X2Drag,
    WheelUp,
    WheelDown,
    WheelLeft,
    WheelRight,
    Hover,
    Count
};
enum MouseAction : unsigned {
    MouseNone = 0,
    MousePan = 5000,
    MouseZoom,
    MouseSelect,
    MouseFile,
    MouseWindow,
    MouseHover,
    MouseZoomIn = 5010,
    MouseZoomOut,
    MouseInherit = UINT32_MAX
};
constexpr unsigned mouse_left = 1, mouse_right = 2, mouse_middle = 4, mouse_x1 = 8, mouse_x2 = 16;
struct MouseBinding {
    MouseGesture gesture{};
    unsigned held{}, modifiers{}, action{};
    bool operator==(const MouseBinding &) const = default;
};
const wchar_t *mouse_gesture_name(MouseGesture gesture);
unsigned mouse_button(MouseGesture gesture);
bool mouse_drag(MouseGesture gesture);
std::vector<CommandInfo> mouse_actions(MouseGesture gesture);
const wchar_t *mouse_action_name(unsigned action);
std::wstring mouse_held_name(unsigned held);
std::wstring mouse_modifiers_name(unsigned modifiers);
struct MouseSettings {
    int wheel_scroll{60}, drag_scroll{100}, drag_zoom{100}, drag_axis{}, drag_threshold{4}, hover_margin{10},
        hover_min{};
    bool ignore_activation{}, slow_wheel{};
    std::vector<MouseBinding> bindings;
    MouseSettings();
    unsigned exact(MouseGesture gesture, unsigned held, unsigned modifiers) const;
    unsigned action(MouseGesture gesture, unsigned held, unsigned modifiers) const;
    bool valid() const;
    void assign(MouseGesture gesture, unsigned held, unsigned modifiers, unsigned action);
    void basic_wheel(bool zoom);
    void basic_middle(int action);
    int wheel_preset() const;
    int middle_preset() const;
    bool operator==(const MouseSettings &) const = default;
};
} // namespace gvn
