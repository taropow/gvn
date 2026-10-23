#pragma once
#include <windows.h>
#include <cstdint>
namespace gvn {
struct UiPalette {COLORREF surface,panel,text,muted,border,accent,selected,input;bool dark{},contrast{};};
// Process-local native UI resources; source images and transparency colors are independent.
const UiPalette &ui_palette();
uint64_t ui_theme_generation();
void set_ui_theme(int mode,int density=0);
void apply_ui_theme(HWND,bool descendants=true);
HBRUSH ui_brush(COLORREF);
int ui_button_size(bool large);
int ui_icon_size(bool large);
LRESULT ui_toolbar_draw(LPARAM);
}
