#include "gvn/command_toast.h"
#include "gvn/toast_duration.h"
#include "gvn/ui_theme.h"
#include "gvn/resources.h"
#include <algorithm>
#include <bit>
#include <mmsystem.h>
namespace gvn {
CommandToast::~CommandToast(){if(IsWindow(hwnd))DestroyWindow(hwnd);if(font)DeleteObject(font);}
void CommandToast::dismiss(){
    if(!hwnd)return;
    KillTimer(hwnd,1);timer=false;busy=false;message.clear();shortcut.clear();ShowWindow(hwnd,SW_HIDE);
}
void CommandToast::repaint(){
    if(hwnd&&(GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_VISIBLE)){InvalidateRect(hwnd,nullptr,FALSE);UpdateWindow(hwnd);}
}
void CommandToast::show(HWND owner,RECT area,const std::wstring &value,const std::wstring &key,bool pending,UINT duration){
    if(!hwnd){
        WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"GVN.CommandToast";
        RegisterClassW(&cls);
        hwnd=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TRANSPARENT,cls.lpszClassName,L"操作通知",WS_CHILD|WS_CLIPSIBLINGS,
                            0,0,1,1,owner,nullptr,cls.hInstance,this);
        if(!hwnd)return;
    }
    message=value;shortcut=key;busy=pending;
    KillTimer(hwnd,1);timer=false;
    expires=0;
    layout(area);ShowWindow(hwnd,SW_SHOWNA);InvalidateRect(hwnd,nullptr,FALSE);
    // Paint the acceptance before a synchronous encoder/edit begins. Does not
    // dispatch unrelated queued commands or touch the canvas render target.
    UpdateWindow(hwnd);
    if(!busy){duration=std::clamp(duration,UINT(kToastMinDuration),UINT(kToastMaxDuration));expires=GetTickCount64()+duration;timer=SetTimer(hwnd,1,duration,nullptr)!=0;}
}
void CommandToast::layout(RECT area){
    canvas=area;if(!hwnd)return;
    const auto next_dpi=GetDpiForWindow(GetParent(hwnd));
    if(dpi!=next_dpi){dpi=next_dpi;if(font)DeleteObject(font);font=CreateFontW(-MulDiv(13,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Yu Gothic UI");}
    const auto px=[&](int n){return MulDiv(n,dpi,96);};
    const int width=std::max(1,std::min(px(360),int(area.right-area.left)-px(24))),height=px(shortcut.empty()?44:64);
    const RECT next{std::max(area.left,area.right-px(12)-width),area.top+px(12),std::max(area.left,area.right-px(12)-width)+width,area.top+px(12)+height};
    RECT before{};GetWindowRect(hwnd,&before);MapWindowPoints(nullptr,GetParent(hwnd),reinterpret_cast<POINT *>(&before),2);
    if(!EqualRect(&before,&next))SetWindowPos(hwnd,HWND_TOP,next.left,next.top,width,height,SWP_NOACTIVATE);
    InvalidateRect(hwnd,nullptr,FALSE);
}
void CommandToast::paint(HDC dc){
    RECT r{};GetClientRect(hwnd,&r);FillRect(dc,&r,ui_brush(ui_palette().surface));
    const auto px=[&](int n){return MulDiv(n,dpi,96);};
    auto mark=r;mark.right=px(3);FillRect(dc,&mark,ui_brush(ui_palette().accent));
    FrameRect(dc,&r,ui_brush(ui_palette().border));SetBkMode(dc,TRANSPARENT);
    const auto old=SelectObject(dc,font);SetTextColor(dc,ui_palette().text);
    RECT label{px(14),px(6),r.right-px(12),px(38)};
    DrawTextW(dc,message.c_str(),-1,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    if(!shortcut.empty()){SetTextColor(dc,ui_palette().muted);label.top=px(36);label.bottom=r.bottom-px(5);DrawTextW(dc,shortcut.c_str(),-1,&label,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);}
    SelectObject(dc,old);
}
LRESULT CALLBACK CommandToast::procedure(HWND h,UINT m,WPARAM w,LPARAM l){
    auto self=reinterpret_cast<CommandToast *>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(m==WM_NCCREATE){self=static_cast<CommandToast *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);self->hwnd=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(!self)return DefWindowProcW(h,m,w,l);
    if(m==WM_NCHITTEST)return HTTRANSPARENT;
    if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT){PAINTSTRUCT ps{};auto dc=BeginPaint(h,&ps);self->paint(dc);EndPaint(h,&ps);return 0;}
    if(m==WM_PRINTCLIENT){self->paint(reinterpret_cast<HDC>(w));return 0;}
    if(m==WM_TIMER&&w==1){if(!self->busy&&self->timer&&GetTickCount64()>=self->expires)self->dismiss();return 0;}
    if(m==WM_NCDESTROY){KillTimer(h,1);self->hwnd=nullptr;self->timer=false;}
    return DefWindowProcW(h,m,w,l);
}
void play_command_pip(){
    // Load audio only on the first requested pip. Keep the module alive while
    // its asynchronous player is running; Windows releases it at process exit.
    static const auto module=LoadLibraryExW(L"winmm.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    static const auto play=module?std::bit_cast<decltype(&PlaySoundW)>(GetProcAddress(module,"PlaySoundW")):nullptr;
    if(play)play(MAKEINTRESOURCEW(IDW_GVN_PIP),GetModuleHandleW(nullptr),SND_RESOURCE|SND_ASYNC|SND_NODEFAULT|SND_NOSTOP);
}
}
