// Actual product spool loop + real Microsoft Print to PDF, on an owned private desktop.
#include "gvn/printing.h"
#include "gvn/ui.h"
#include "../tests/test_desktop.h"
#include <winspool.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <array>
#include <cmath>
#include <ole2.h>

using namespace gvn;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
std::wstring default_printer() {
    DWORD count{};GetDefaultPrinterW(nullptr,&count);
    if(!count)return {};
    std::wstring text(count,L'\0');check(GetDefaultPrinterW(text.data(),&count)!=FALSE,"Default printer read failed");
    text.resize(wcslen(text.c_str()));return text;
}
struct Device {
    static constexpr auto name=L"Microsoft Print to PDF";
    HANDLE printer{};HDC dc{};std::vector<uint8_t> mode;
    Device() {
        check(OpenPrinterW(const_cast<wchar_t*>(name),&printer,nullptr)!=FALSE,"PDF printer not available");
        const auto size=DocumentPropertiesW(nullptr,printer,const_cast<wchar_t*>(name),nullptr,nullptr,0);
        check(size>0,"Cannot query private DEVMODE");mode.resize(size);
        auto dm=reinterpret_cast<DEVMODEW*>(mode.data());
        check(DocumentPropertiesW(nullptr,printer,const_cast<wchar_t*>(name),dm,nullptr,DM_OUT_BUFFER)==IDOK,"Cannot read private DEVMODE");
        dm->dmFields|=DM_PAPERSIZE|DM_ORIENTATION|DM_COPIES|DM_COLLATE;
        dm->dmPaperSize=DMPAPER_A4;dm->dmOrientation=DMORIENT_PORTRAIT;dm->dmCopies=1;dm->dmCollate=DMCOLLATE_FALSE;
        check(DocumentPropertiesW(nullptr,printer,const_cast<wchar_t*>(name),dm,dm,DM_IN_BUFFER|DM_OUT_BUFFER)==IDOK,"Cannot validate private DEVMODE");
        check(dm->dmCopies==1,"Driver multiplied copies");
        dc=CreateDCW(L"WINSPOOL",name,nullptr,dm);check(dc!=nullptr,"Cannot create actual PDF DC");
    }
    ~Device(){if(dc)DeleteDC(dc);if(printer)ClosePrinter(printer);}
    PrintPaper paper() const {
        const double sx=GetDeviceCaps(dc,LOGPIXELSX)/25.4,sy=GetDeviceCaps(dc,LOGPIXELSY)/25.4;
        check(sx>0&&sy>0,"Invalid printer DPI");
        PrintPaper p;p.width=GetDeviceCaps(dc,PHYSICALWIDTH)/sx;p.height=GetDeviceCaps(dc,PHYSICALHEIGHT)/sy;
        p.left=GetDeviceCaps(dc,PHYSICALOFFSETX)/sx;p.top=GetDeviceCaps(dc,PHYSICALOFFSETY)/sy;
        p.right=std::max(0.,p.width-p.left-GetDeviceCaps(dc,HORZRES)/sx);
        p.bottom=std::max(0.,p.height-p.top-GetDeviceCaps(dc,VERTRES)/sy);return p;
    }
    void wait_job_gone(int id) const {
        const auto deadline=GetTickCount64()+15000;
        for(;;) {
            DWORD bytes{};SetLastError(0);const auto ok=GetJobW(printer,id,1,nullptr,0,&bytes);const auto error=GetLastError();
            if(!ok&&(error==ERROR_INVALID_PARAMETER||error==ERROR_FILE_NOT_FOUND||error==ERROR_NOT_FOUND))return;
            check(ok||error==ERROR_INSUFFICIENT_BUFFER,"Cannot inspect owned print job");
            check(GetTickCount64()<deadline,"Owned print job did not finish");Sleep(20);
        }
    }
};
struct Canceller {
    HWND owner{};PrintJobTrace *trace{};bool posted{},owner_disabled{};
    static inline Canceller *current{};
    UINT_PTR timer{};
    Canceller(HWND h,PrintJobTrace &t):owner(h),trace(&t) {
        current=this;
        timer=SetTimer(nullptr,0,10,[](HWND,UINT,UINT_PTR,DWORD){
            if(!current)return;
            auto &self=*current;if(self.posted||self.trace->job_id<=0)return;
            EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM p)->BOOL {
                auto &s=*reinterpret_cast<Canceller*>(p);wchar_t title[64]{};GetWindowTextW(h,title,64);
                if(GetWindow(h,GW_OWNER)==s.owner&&wcscmp(title,L"GVN 印刷中")==0&&GetDlgItem(h,IDCANCEL)) {
                    s.owner_disabled=!IsWindowEnabled(s.owner);
                    s.posted=PostMessageW(h,WM_COMMAND,MAKEWPARAM(IDCANCEL,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(h,IDCANCEL)))!=FALSE;
                    return FALSE;
                }
                return TRUE;
            },reinterpret_cast<LPARAM>(&self));
        });check(timer!=0,"Cannot schedule actual Cancel button");
    }
    ~Canceller(){if(timer)KillTimer(nullptr,timer);current=nullptr;}
};
void trace_json(std::ostream &out,const PrintJobTrace &t) {
    out<<"{\"job_id\":"<<t.job_id<<",\"pages_started\":"<<t.pages_started<<",\"pages_ended\":"<<t.pages_ended
       <<",\"completed\":"<<(t.completed?"true":"false")<<",\"cancelled\":"<<(t.cancelled?"true":"false")
       <<",\"aborted\":"<<(t.aborted?"true":"false")<<",\"abort_result\":"<<t.abort_result<<"}";
}
std::wstring text(HWND h) {
    wchar_t value[1024]{};GetWindowTextW(h,value,1024);return value;
}
struct SetupScript {
    HWND owner{},main{};int phase{},opens{};bool cancelled_preserved{},selected_pdf{};
    const char *error{};std::wstring initial;ULONGLONG deadline{GetTickCount64()+30000};
    UINT_PTR timer{};bool active{};
    static inline SetupScript *current{};
    explicit SetupScript(HWND h):owner(h) {
        current=this;timer=SetTimer(nullptr,0,20,[](HWND,UINT,UINT_PTR,DWORD){if(current&&!current->active)current->tick();});
        check(timer!=0,"Cannot schedule real printer setup script");
    }
    ~SetupScript(){if(timer)KillTimer(nullptr,timer);current=nullptr;}
    HWND child_dialog() const {
        struct Find {HWND owner{},found{};} find{main};
        EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM p)->BOOL {
            auto &f=*reinterpret_cast<Find*>(p);wchar_t cls[64]{};GetClassNameW(h,cls,64);
            if(GetWindow(h,GW_OWNER)==f.owner&&wcscmp(cls,L"#32770")==0&&GetDlgItem(h,IDOK)&&GetDlgItem(h,IDCANCEL)&&
               (GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE)){f.found=h;return FALSE;}return TRUE;
        },reinterpret_cast<LPARAM>(&find));return find.found;
    }
    void fail(const char *message) {
        error=message;phase=5;
        if(const auto child=child_dialog())PostMessageW(child,WM_COMMAND,IDCANCEL,0);
        if(main)PostMessageW(main,WM_COMMAND,IDCANCEL,0);
    }
    void tick() {
        active=true;struct Reset {bool &v;~Reset(){v=false;}} reset{active};
        if(!main) {
            EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM p)->BOOL {
                auto &s=*reinterpret_cast<SetupScript*>(p);
                if(GetWindow(h,GW_OWNER)==s.owner&&GetDlgItem(h,print_control::Printer)&&GetDlgItem(h,print_control::Setup)) {
                    s.main=h;return FALSE;
                }return TRUE;
            },reinterpret_cast<LPARAM>(this));
        }
        if(GetTickCount64()>deadline){fail("Real printer setup UI timed out");return;}
        if(!main)return;
        const auto child=child_dialog();
        if(phase==0&&IsWindowEnabled(main)) {
            initial=text(GetDlgItem(main,print_control::Printer));
            if(initial.empty()||initial==L"プリンタを選択してください"){fail("Actual default printer unavailable");return;}
            phase=1;++opens;PostMessageW(GetDlgItem(main,print_control::Setup),BM_CLICK,0,0);
        } else if(phase==1&&child&&IsWindowEnabled(child)) {
            phase=2;PostMessageW(child,WM_COMMAND,IDCANCEL,0);
        } else if(phase==2&&!child&&IsWindowEnabled(main)) {
            cancelled_preserved=text(GetDlgItem(main,print_control::Printer))==initial;
            if(!cancelled_preserved){fail("Printer setup Cancel changed device");return;}
            phase=3;++opens;PostMessageW(GetDlgItem(main,print_control::Setup),BM_CLICK,0,0);
        } else if(phase==3&&child&&IsWindowEnabled(child)) {
            struct Choice {HWND combo{};LRESULT index{CB_ERR};} choice;
            EnumChildWindows(child,[](HWND h,LPARAM p)->BOOL {
                wchar_t cls[64]{};GetClassNameW(h,cls,64);if(_wcsicmp(cls,WC_COMBOBOXW)!=0)return TRUE;
                const auto index=SendMessageW(h,CB_FINDSTRINGEXACT,WPARAM(-1),LPARAM(Device::name));
                if(index!=CB_ERR){auto &c=*reinterpret_cast<Choice*>(p);c.combo=h;c.index=index;return FALSE;}return TRUE;
            },reinterpret_cast<LPARAM>(&choice));
            if(!choice.combo)return; // Wait for the OS to populate its printer list.
            phase=4;
            SendMessageW(choice.combo,CB_SETCURSEL,choice.index,0);
            SendMessageW(GetParent(choice.combo),WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(choice.combo),CBN_SELCHANGE),LPARAM(choice.combo));
            PostMessageW(child,WM_COMMAND,IDOK,0);
        } else if(phase==4&&!child&&IsWindowEnabled(main)) {
            selected_pdf=text(GetDlgItem(main,print_control::Printer))==Device::name;
            if(!selected_pdf){fail("Actual printer setup did not return PDF device");return;}
            phase=5;PostMessageW(main,WM_COMMAND,IDCANCEL,0);
        }
    }
};
}
int wmain(int argc,wchar_t **argv) {
    if(argc!=2&&(argc!=3||wcscmp(argv[2],L"--setup-only")!=0))return 2;
    try {
        const auto out=std::filesystem::absolute(argv[1]);check(!std::filesystem::exists(out),"Probe output must be new");std::filesystem::create_directories(out);
        gvn_test::ClipboardLock lock;check(bool(lock),"Cannot acquire private station lock");
        const auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);
        check(station&&gvn_test::noninteractive(station)&&SetProcessWindowStation(station),"Cannot create private station");
        const auto desktop=CreateDesktopW(L"GVN-owned-print-driver",nullptr,nullptr,0,GENERIC_ALL,nullptr);
        check(desktop&&SetThreadDesktop(desktop),"Cannot create private desktop");
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        check(SUCCEEDED(OleInitialize(nullptr))&&initialize_controls(),"Cannot initialize native UI");
        const auto owner=CreateWindowExW(0,L"STATIC",L"GVN owned print driver",WS_OVERLAPPEDWINDOW,100,100,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(owner!=nullptr,"Cannot create owned print window");ShowWindow(owner,SW_SHOWNOACTIVATE);
        const auto before_default=default_printer();
        if(argc==3) {
            Image sample;sample.width=sample.height=2;sample.frames.push_back({std::vector<Pixel>(4,{20,100,220,127}),100});
            const auto before=sample.frames[0].pixels;bool preserved{},selected{};int opens{},phase{};const char *error{};
            {SetupScript script(owner);print_image(owner,sample,0);preserved=script.cancelled_preserved;selected=script.selected_pdf;opens=script.opens;phase=script.phase;error=script.error;}
            if(error)throw std::runtime_error(error);
            check(opens==2&&phase==5&&preserved&&selected,"Real printer setup flow incomplete");
            check(IsWindowEnabled(owner)&&default_printer()==before_default&&sample.frames[0].pixels==before,"Print setup changed owner/default/source");
            std::ofstream report(out/L"native-setup.json");report<<"{\"passed\":true,\"actual_product_print_image\":true,\"private_station\":true,\"os_setup_opens\":2,\"os_setup_cancel\":1,\"os_setup_accept\":1,\"pdf_device_selected\":true,\"cancel_preserved_device\":true,\"main_print_dialog_cancelled\":true,\"default_printer_unchanged\":true,\"source_unchanged\":true,\"print_jobs_requested\":0}\n";report.close();
            DestroyWindow(owner);OleUninitialize();std::cout<<"PASS actual product Print/OSsetup Cancel andPDF selection;no jobs requested\n";return 0;
        }
        Device device;const auto paper=device.paper();
        check(std::abs(paper.width-210)<1&&std::abs(paper.height-297)<1,"Driver did not accept private A4");
        Image image;image.width=300;image.height=200;image.metadata.dpi_x=144;image.metadata.dpi_y=288;
        image.frames.push_back({std::vector<Pixel>(60000,{255,0,255,255}),100});
        image.frames.push_back({std::vector<Pixel>(60000),120});
        const std::array<Pixel,6> colors{{{240,30,20,255},{10,220,40,128},{40,60,240,0},{64,96,128,64},{255,192,24,254},{12,80,200,1}}};
        for(unsigned y=0;y<200;++y)for(unsigned x=0;x<300;++x)image.frames[1].pixels[y*300+x]=colors[(y/100)*3+x/100];
        const auto first=image.frames[0].pixels,second=image.frames[1].pixels;
        std::ofstream report(out/L"native.json");report<<std::setprecision(17);
        report<<"{\"private_station\":true,\"printer\":\"Microsoft Print to PDF\",\"driver_copies\":1,\"paper\":["<<paper.width<<','<<paper.height<<"],\"dpi\":["<<GetDeviceCaps(device.dc,LOGPIXELSX)<<','<<GetDeviceCaps(device.dc,LOGPIXELSY)<<"],\"cases\":[";
        for(int which=0;which<2;++which) {
            PrintOptions options;options.sizing=PrintSizing::Specified;options.width=60;options.height=which?90:40;
            options.copies=which?1:2;options.position=which?8:4;options.density=which?2:0;
            options.rotation=which?PrintRotation::Right:PrintRotation::None;
            const auto plan=print_layout(image.width,image.height,image.metadata,paper,options);
            const auto file=out/(which?L"rotated-density.pdf":L"two-copies.pdf");PrintJobTrace trace;
            spool_print_image(owner,device.dc,image,1,plan,options,file,&trace);
            check(trace.completed&&!trace.cancelled&&trace.pages_started==options.copies&&trace.pages_ended==options.copies,"Actual print job did not finish expected pages");
            device.wait_job_gone(trace.job_id);check(IsWindowEnabled(owner),"Print left owner disabled");
            if(which)report<<',';report<<"{\"file\":\""<<(which?"rotated-density.pdf":"two-copies.pdf")<<"\",\"copies\":"<<options.copies<<",\"density\":"<<options.density<<",\"position\":"<<options.position<<",\"quarter_turns\":"<<plan.quarter_turns<<",\"box\":["<<plan.image.x<<','<<plan.image.y<<','<<plan.image.width<<','<<plan.image.height<<"],\"trace\":";trace_json(report,trace);report<<'}';report.flush();
        }
        PrintOptions options;options.copies=100;const auto plan=print_layout(image.width,image.height,image.metadata,paper,options);PrintJobTrace cancelled;
        bool posted{},disabled{};
        {Canceller cancel(owner,cancelled);spool_print_image(owner,device.dc,image,1,plan,options,out/L"cancelled.pdf",&cancelled);posted=cancel.posted;disabled=cancel.owner_disabled;}
        check(posted&&disabled&&cancelled.job_id>0&&cancelled.cancelled&&!cancelled.completed&&cancelled.aborted&&cancelled.pages_ended<100,"Actual Cancel did not abort owned job");
        device.wait_job_gone(cancelled.job_id);
        check(IsWindowEnabled(owner)&&default_printer()==before_default,"Print did not restore owner/default printer");
        check(image.frames[0].pixels==first&&image.frames[1].pixels==second&&image.metadata.dpi_x==144&&image.metadata.dpi_y==288,"Print changed source image");
        report<<"],\"cancel\":";trace_json(report,cancelled);report<<",\"cancel_button_posted\":true,\"source_unchanged\":true,\"default_printer_unchanged\":true,\"passed\":true}\n";report.close();
        DestroyWindow(owner);OleUninitialize();std::cout<<"PASS actual PDF driver2jobs3pages plusowned cancellation\n";return 0;
    } catch(const std::exception &e){std::cerr<<"FAIL actual PDF driver: "<<e.what()<<'\n';return 1;}
}
