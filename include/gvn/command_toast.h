#pragma once
#include <windows.h>
#include <string>
#include <cstdint>
namespace gvn {
// One input-transparent child, created on the first notification. No idle timer.
class CommandToast {
    HWND hwnd{};
    RECT canvas{};
    std::wstring message,shortcut;
    HFONT font{};
    UINT dpi{};
    uint64_t expires{};
    bool busy{},timer{};
    static LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM);
    void paint(HDC);
  public:
    ~CommandToast();
    void show(HWND,RECT,const std::wstring &,const std::wstring &,bool pending,UINT duration);
    void layout(RECT);
    void dismiss();
    HWND window() const{return hwnd;}
    bool pending() const{return busy;}
    bool timer_active() const{return timer;}
    const std::wstring &text() const{return message;}
    const std::wstring &key() const{return shortcut;}
};
void play_command_pip();
}
