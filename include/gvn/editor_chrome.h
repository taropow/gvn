#pragma once
#include "annotation.h"
#include "saving.h"
#include <windows.h>
#include <map>
namespace gvn {
namespace editor_id {
enum : int { Color=7000, Width, Opacity, Font, TextSize, Text, X, Y, W, H, Angle, Scale, Anchor,
    Apply, Cancel, Name, Format, Drag, Hint, Error, TextUpdate, Last };
}
enum class EditorPanel { None, Pen, Text, Crop, Transform };
class EditorChrome {
    HWND owner{};
    HFONT font{};UINT dpi{};
    HICON drag_icon{};
    bool filling{},drag_pending{};POINT drag_start{};
    std::map<int,HWND> controls;
    std::map<int,HWND> labels;
    EditorPanel panel{EditorPanel::None};
    std::wstring export_default;
    Pixel color{245,30,84,255};
    static LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK drag_procedure(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
    void create(HWND,bool editing=true);
    void put(int,const std::wstring &);
  public:
    HWND top{},bottom{};
    ~EditorChrome();
    int layout_top(HWND,int width,int y,UINT,bool visible,EditorPanel,bool applying);
    int layout_bottom(HWND,int width,int client_height,int occupied,UINT,bool visible,bool enabled);
    HWND control(int id) const {const auto p=controls.find(id);return p==controls.end()?nullptr:p->second;}
    bool owns(HWND child) const;
    bool dragging() const {return drag_pending;}
    void cancel_drag(){drag_pending=false;if(GetCapture()==control(editor_id::Drag))ReleaseCapture();}
    bool notification(int id,int code);
    double number(int id,double min,double max) const;
    std::wstring text(int id) const;
    PenStyle pen() const;
    Image text_image(const EditMemory &) const;
    void bounds(const FloatingBox &,double scale=1);
    FloatingBox edited_bounds(const FloatingBox &) const;
    double output_scale() const {return number(editor_id::Scale,1,10000)/100.;}
    static std::wstring timestamp_name(const SYSTEMTIME &);
    void reset_export_name();
    SaveFormat export_format() const;
    void error(const std::wstring &);
};
} // namespace gvn
