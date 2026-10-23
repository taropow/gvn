#include "gvn/printing.h"
#include <commdlg.h>
#include <commctrl.h>
#include <cstring>
#include <cmath>

namespace gvn {
namespace {
void release(PRINTDLGW &d){if(d.hDC)DeleteDC(d.hDC);if(d.hDevMode)GlobalFree(d.hDevMode);if(d.hDevNames)GlobalFree(d.hDevNames);d.hDC=nullptr;d.hDevMode=d.hDevNames=nullptr;}
HGLOBAL duplicate(HGLOBAL h) {
    if(!h)return nullptr;const auto size=GlobalSize(h);auto copy=GlobalAlloc(GMEM_MOVEABLE,size);
    if(!copy)throw std::bad_alloc();const auto from=GlobalLock(h),to=GlobalLock(copy);
    if(!from||!to){if(from)GlobalUnlock(h);if(to)GlobalUnlock(copy);GlobalFree(copy);throw std::runtime_error("Cannot copy printer settings");}
    std::memcpy(to,from,size);GlobalUnlock(h);GlobalUnlock(copy);return copy;
}
struct PrinterSession {
    PRINTDLGW dialog{sizeof(PRINTDLGW)};
    ~PrinterSession(){release(dialog);}
    PrintDevice device() const {
        if(!dialog.hDC)return {};
        const auto dc=dialog.hDC;const double sx=GetDeviceCaps(dc,LOGPIXELSX)/25.4,sy=GetDeviceCaps(dc,LOGPIXELSY)/25.4;
        if(sx<=0||sy<=0)throw std::runtime_error("Printer reported invalid resolution");
        PrintDevice out;out.ready=true;
        if(const auto names=static_cast<const DEVNAMES*>(GlobalLock(dialog.hDevNames))) {
            out.name=reinterpret_cast<const wchar_t*>(names)+names->wDeviceOffset;GlobalUnlock(dialog.hDevNames);
        }
        auto &p=out.paper;p.width=GetDeviceCaps(dc,PHYSICALWIDTH)/sx;p.height=GetDeviceCaps(dc,PHYSICALHEIGHT)/sy;
        p.left=GetDeviceCaps(dc,PHYSICALOFFSETX)/sx;p.top=GetDeviceCaps(dc,PHYSICALOFFSETY)/sy;
        p.right=std::max(0.,p.width-p.left-GetDeviceCaps(dc,HORZRES)/sx);p.bottom=std::max(0.,p.height-p.top-GetDeviceCaps(dc,VERTRES)/sy);
        out.paper_name=p.width>p.height?L"横向き":L"縦向き";return out;
    }
    bool choose(HWND owner,bool defaults=false) {
        PRINTDLGW next{sizeof(next)};
        struct Clean {PRINTDLGW &d;~Clean(){release(d);}} clean{next};
        next.hwndOwner=owner;next.nCopies=1;
        next.Flags=PD_RETURNDC|(defaults?PD_RETURNDEFAULT:PD_PRINTSETUP|PD_USEDEVMODECOPIESANDCOLLATE);
        if(!defaults){next.hDevMode=duplicate(dialog.hDevMode);next.hDevNames=duplicate(dialog.hDevNames);}
        if(!PrintDlgW(&next)) {if(!defaults&&CommDlgExtendedError())throw std::runtime_error("Printer setup failed");return false;}
        // Copies are emitted as explicit pages. Do not also multiply them in the driver.
        if(next.hDevMode&&next.hDevNames) {
            const auto mode=static_cast<DEVMODEW*>(GlobalLock(next.hDevMode));
            const auto names=static_cast<const DEVNAMES*>(GlobalLock(next.hDevNames));
            HDC replacement{};
            if(mode&&names) {
                mode->dmFields|=DM_COPIES|DM_COLLATE;mode->dmCopies=1;mode->dmCollate=DMCOLLATE_FALSE;
                const auto base=reinterpret_cast<const wchar_t*>(names);
                replacement=CreateDCW(base+names->wDriverOffset,base+names->wDeviceOffset,nullptr,mode);
            }
            if(mode)GlobalUnlock(next.hDevMode);if(names)GlobalUnlock(next.hDevNames);
            if(!replacement)throw std::runtime_error("Cannot create printer context");
            if(next.hDC)DeleteDC(next.hDC);next.hDC=replacement;
        }
        release(dialog);dialog=next;next.hDC=nullptr;next.hDevMode=next.hDevNames=nullptr;return dialog.hDC!=nullptr;
    }
};
struct Progress {
    HWND window{},cancel{};bool cancelled{},quit{};int quit_code{};
    static LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
        auto *self=reinterpret_cast<Progress*>(data);
        if(msg==WM_CLOSE||(msg==WM_COMMAND&&LOWORD(w)==IDCANCEL)){self->cancelled=true;return 0;}
        return DefSubclassProc(h,msg,w,l);
    }
    Progress(HWND owner) {
        window=CreateWindowExW(WS_EX_DLGMODALFRAME,WC_DIALOG,L"GVN 印刷中",WS_POPUP|WS_CAPTION|WS_SYSMENU,
            CW_USEDEFAULT,CW_USEDEFAULT,320,128,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window)throw std::runtime_error("Cannot create print progress");
        SetWindowSubclass(window,proc,1,DWORD_PTR(this));
        CreateWindowExW(0,L"STATIC",L"画像を印刷しています…",WS_CHILD|WS_VISIBLE,18,16,274,22,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        cancel=CreateWindowExW(0,L"BUTTON",L"キャンセル",WS_CHILD|WS_VISIBLE|WS_TABSTOP,180,50,110,28,window,HMENU(IDCANCEL),GetModuleHandleW(nullptr),nullptr);
        RECT parent{},rect{};GetWindowRect(owner,&parent);GetWindowRect(window,&rect);
        SetWindowPos(window,nullptr,(parent.left+parent.right-(rect.right-rect.left))/2,(parent.top+parent.bottom-(rect.bottom-rect.top))/2,0,0,SWP_NOSIZE|SWP_NOZORDER);
        ShowWindow(window,SW_SHOW);UpdateWindow(window);
    }
    ~Progress(){if(IsWindow(window)){RemoveWindowSubclass(window,proc,1);DestroyWindow(window);}if(quit)PostQuitMessage(quit_code);}
    bool pump() {
        MSG msg{};
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
            if(msg.message==WM_QUIT){quit=true;quit_code=int(msg.wParam);cancelled=true;continue;}
            if(msg.hwnd==window&&(msg.message==WM_CLOSE||(msg.message==WM_COMMAND&&LOWORD(msg.wParam)==IDCANCEL))){cancelled=true;continue;}
            if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        }
        if(!IsWindow(window))cancelled=true;return !cancelled;
    }
};
thread_local Progress *progress{};
BOOL CALLBACK abort_proc(HDC,int){return !progress||progress->pump();}
}
void print_image(HWND owner,const Image &image,size_t frame,const ImageMetadata *metadata) {
    static PrintOptions previous;
    static PrinterSession printer;if(!printer.dialog.hDC)printer.choose(owner,true);
    const auto &resolution=metadata?*metadata:image.metadata;
    PrintDialog dialog(image,frame,resolution,previous,printer.device(),[&](HWND window)->std::optional<PrintDevice>{if(!printer.choose(window))return {};return printer.device();});
    const auto chosen=dialog.run(owner);if(!chosen)return;
    if(chosen->copies>1&&MessageBoxW(owner,(std::to_wstring(chosen->copies)+L"部印刷しますか？").c_str(),L"印刷部数",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK)return;
    previous=*chosen;const auto plan=print_layout(image.width,image.height,resolution,printer.device().paper,*chosen);
    spool_print_image(owner,printer.dialog.hDC,image,frame,plan,*chosen);
}
void spool_print_image(HWND owner,HDC dc,const Image &image,size_t frame,const PrintLayout &plan,
                       const PrintOptions &options,const std::filesystem::path &output,PrintJobTrace *trace) {
    if(!IsWindow(owner)||!dc||frame>=image.frames.size()||options.copies<1||options.copies>9999)
        throw std::invalid_argument("Invalid print job");
    PrintJobTrace state;
    auto &audit=trace?*trace:state;audit={};
    const bool enabled=IsWindowEnabled(owner);if(enabled)EnableWindow(owner,FALSE);
    struct Restore {HWND owner;bool enabled;~Restore(){if(enabled){EnableWindow(owner,TRUE);SetActiveWindow(owner);}}} restore{owner,enabled};
    Progress status(owner);progress=&status;
    struct Reset {~Reset(){progress=nullptr;}} reset;
    SetAbortProc(dc,abort_proc);
    DOCINFOW document{sizeof(document),L"GVN image",output.empty()?nullptr:output.c_str()};bool started=false,ok=false;
    try {
        if(status.pump()){audit.job_id=StartDocW(dc,&document);started=audit.job_id>0;}
        ok=started;
        for(int copy=0;ok&&copy<options.copies;++copy) {
            ok=status.pump()&&StartPage(dc)>0;
            if(ok)++audit.pages_started;
            if(ok)ok=draw_print_page(dc,image,frame,plan,GetDeviceCaps(dc,LOGPIXELSX)/25.4,GetDeviceCaps(dc,LOGPIXELSY)/25.4,
                GetDeviceCaps(dc,PHYSICALOFFSETX),GetDeviceCaps(dc,PHYSICALOFFSETY),options.density);
            if(ok)ok=EndPage(dc)>0;
            if(ok)++audit.pages_ended;
        }
        if(started){if(ok&&!status.cancelled)ok=EndDoc(dc)>0;else {audit.aborted=true;audit.abort_result=AbortDoc(dc);}started=false;}
    }catch(...){if(started){audit.aborted=true;audit.abort_result=AbortDoc(dc);}audit.cancelled=status.cancelled;throw;}
    audit.cancelled=status.cancelled;audit.completed=ok&&!status.cancelled;
    if(!ok&&!status.cancelled)throw std::runtime_error("Printing failed");
}
}
