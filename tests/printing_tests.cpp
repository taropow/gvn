#include "gvn/printing.h"
#include "gvn/ui.h"
#include "test.h"
#include "test_desktop.h"
#include <cmath>
#include <fstream>
#include <string_view>
using namespace gvn;
namespace id=print_control;
namespace {
void close_to(double a,double b){CHECK(std::abs(a-b)<1e-5);}
Image sample(){Image i;i.width=2;i.height=3;i.frames.push_back({{{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255},{0,255,255,255},{255,0,255,128}},100});return i;}
std::wstring text(HWND h){std::wstring s(size_t(GetWindowTextLengthW(h))+1,L'\0');GetWindowTextW(h,s.data(),int(s.size()));s.pop_back();return s;}
struct Fixture {
    Image image=sample();HWND owner{};std::unique_ptr<PrintDialog> dialog;
    Fixture(){owner=CreateWindowExW(0,L"STATIC",L"Owned printing tests",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,1000,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(owner);}
    ~Fixture(){dialog.reset();DestroyWindow(owner);}
    void open(PrintOptions options={},bool ready=true,PrintDialog::Chooser chooser={}){dialog=std::make_unique<PrintDialog>(image,0,image.metadata,options,PrintDevice{L"Synthetic printer",L"A4",{210,297,3,4,5,6},ready},chooser);CHECK(dialog->create(owner));}
    void set(int key,const wchar_t *value){SetWindowTextW(dialog->control(key),value);}
    void combo(int key,int n){SendMessageW(dialog->control(key),CB_SETCURSEL,n,0);SendMessageW(dialog->window(),WM_COMMAND,MAKEWPARAM(key,CBN_SELCHANGE),LPARAM(dialog->control(key)));}
    void click(int key){SendMessageW(dialog->control(key),BM_CLICK,0,0);}
};
}
TEST(print_five_sizes_margins_and_nine_positions) {
    PrintOptions o;PrintPaper paper;ImageMetadata metadata;
    auto p=print_layout(400,200,metadata,paper,o);close_to(p.image.width,190);close_to(p.image.height,95);close_to(p.image.x,10);close_to(p.image.y,101);
    o.sizing=PrintSizing::Cover;p=print_layout(400,200,metadata,paper,o);close_to(p.image.height,277);close_to(p.image.width,554);CHECK(p.image.x<0);
    o.sizing=PrintSizing::Stretch;p=print_layout(400,200,metadata,paper,o);close_to(p.image.width,190);close_to(p.image.height,277);
    metadata.dpi_x=200;metadata.dpi_y=100;o.sizing=PrintSizing::ImageDpi;p=print_layout(400,200,metadata,paper,o);close_to(p.image.width,50.8);close_to(p.image.height,50.8);
    metadata.dpi_x=metadata.dpi_y=0;p=print_layout(400,200,metadata,paper,o);close_to(p.image.width,400*25.4/72);
    metadata.dpi_y=100;p=print_layout(400,200,metadata,paper,o);close_to(p.image.width,101.6);
    o.sizing=PrintSizing::Specified;o.width=20;o.height=30;
    for(int n=0;n<9;++n){o.position=n;p=print_layout(400,200,metadata,paper,o);close_to(p.image.x,10+170.*(n%3)/2);close_to(p.image.y,10+247.*(n/3)/2);}
    o.left=o.top=o.right=o.bottom=0;paper.left=3;paper.top=4;paper.right=5;paper.bottom=6;
    p=print_layout(4,2,metadata,paper,o);close_to(p.area.x,3);close_to(p.area.y,4);close_to(p.area.width,202);close_to(p.area.height,287);
}
TEST(print_rotation_preserves_density_axes_and_rejects_invalid_settings) {
    PrintOptions o;PrintPaper paper;ImageMetadata m;m.dpi_x=100;m.dpi_y=200;o.sizing=PrintSizing::ImageDpi;
    for(auto rotation:{PrintRotation::Left,PrintRotation::Right}){o.rotation=rotation;const auto p=print_layout(400,200,m,paper,o);CHECK(p.quarter_turns==(rotation==PrintRotation::Left?3:1));close_to(p.image.width,25.4);close_to(p.image.height,101.6);}
    o.rotation=PrintRotation::Right;CHECK(print_layout(200,400,m,paper,o).quarter_turns==0);
    o.left=210;THROWS(print_layout(2,3,m,paper,o));o.left=NAN;THROWS(print_layout(2,3,m,paper,o));
    o=PrintOptions{};o.copies=0;THROWS(print_layout(2,3,m,paper,o));o.copies=10000;THROWS(print_layout(2,3,m,paper,o));
    o=PrintOptions{};o.width=1000000;o.height=1000000;o.sizing=PrintSizing::Specified;CHECK(print_layout(2,3,m,paper,o).image.width==1000000);
    o.width=1000001;THROWS(print_layout(2,3,m,paper,o));
}
TEST(print_density_mattes_alpha_and_retains_endpoints_without_changing_source) {
    for(int d=-5;d<=5;++d){CHECK((print_pixel({255,0,0,255},d)==Pixel{0,0,255,255}));CHECK((print_pixel({9,8,7,0},d)==Pixel{255,255,255,255}));}
    const Pixel p{90,120,150,128};const auto normal=print_pixel(p,0),light=print_pixel(p,-5),dark=print_pixel(p,5);
    CHECK(light.r>normal.r&&normal.r>dark.r&&light.g>normal.g&&normal.g>dark.g);
    CHECK(p.a==128&&p.r==90);
}
TEST(print_gdi_draw_clips_to_margins_and_rotates_actual_pixels) {
    auto image=sample();const auto original=image.frames[0].pixels;
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),60,-60,1,32,BI_RGB};void *raw{};
    const auto dc=CreateCompatibleDC(nullptr);const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);CHECK(dc&&bitmap&&raw);
    const auto old=SelectObject(dc,bitmap);auto *pixels=static_cast<Pixel*>(raw);
    for(int turns:{0,1,3}) {
        std::fill_n(pixels,3600,Pixel{255,255,255,255});PrintLayout layout{{10,10,40,40},{0,0,60,60},turns};
        CHECK(draw_print_page(dc,image,0,layout,1,1));GdiFlush();
        CHECK((pixels[0]==Pixel{255,255,255,255}));const auto center=pixels[30*60+30];CHECK(center.r<255||center.g<255||center.b<255);
        CHECK((pixels[55*60+55]==Pixel{255,255,255,255}));
    }
    // Source top-left red is top-right after clockwise rotation, bottom-left after counter-clockwise.
    for(int turns:{1,3}){std::fill_n(pixels,3600,Pixel{255,255,255,255});PrintLayout p{{0,0,60,60},{0,0,60,60},turns};CHECK(draw_print_page(dc,image,0,p,1,1));GdiFlush();const auto pixel=pixels[(turns==1?5:55)*60+(turns==1?55:5)];CHECK(pixel.b>240&&pixel.r<15&&pixel.g<15);}
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);CHECK(image.frames[0].pixels==original);
}
TEST(print_controls_units_aspect_margins_errors_apply_and_cancel) {
    Fixture f;f.open();CHECK(IsWindowEnabled(f.dialog->control(IDOK)));CHECK(!IsWindowEnabled(f.dialog->control(id::Width)));
    f.combo(id::Sizing,4);CHECK(IsWindowEnabled(f.dialog->control(id::Width)));f.set(id::Width,L"6");close_to(std::stod(text(f.dialog->control(id::Height))),9);
    f.combo(id::Unit,1);close_to(std::stod(text(f.dialog->control(id::Width))),60/25.4);
    f.combo(id::Unit,0);close_to(std::stod(text(f.dialog->control(id::Width))),6);
    CHECK(text(f.dialog->control(id::Width))==L"6"&&text(f.dialog->control(id::Top))==L"1");
    f.set(id::Left,L"21");CHECK(!IsWindowEnabled(f.dialog->control(IDOK))&&!f.dialog->layout());
    f.set(id::Left,L"0");CHECK(IsWindowEnabled(f.dialog->control(IDOK)));close_to(f.dialog->layout()->area.x,3);
    f.click(id::Position);f.combo(id::Density,10);f.combo(id::Rotation,2);f.set(id::Copies,L"3");f.click(IDOK);
    const auto result=f.dialog->result();CHECK(result&&result->position==0&&result->copies==3&&result->density==5&&result->rotation==PrintRotation::Right);
    f.dialog.reset();f.open();f.set(id::Top,L"2");f.click(IDCANCEL);CHECK(!f.dialog->result());
}
TEST(print_printer_setup_cancel_and_absent_printer_do_not_allow_output) {
    Fixture f;int calls{};f.open({},false,[&](HWND owner)->std::optional<PrintDevice>{CHECK(owner==f.dialog->window());++calls;return {};});
    CHECK(!IsWindowEnabled(f.dialog->control(IDOK)));f.click(id::Setup);CHECK(calls==1&&!IsWindowEnabled(f.dialog->control(IDOK)));
    f.click(IDOK);CHECK(IsWindow(f.dialog->window())&&!f.dialog->result());f.click(IDCANCEL);
    f.dialog.reset();f.open({},false,[&](HWND)->std::optional<PrintDevice>{++calls;return PrintDevice{L"Owned fake landscape",L"A4",{297,210,5,5,5,5},true};});
    f.click(id::Setup);CHECK(calls==2&&IsWindowEnabled(f.dialog->control(IDOK))&&f.dialog->device().paper.width==297);
}
TEST(print_density_matches_all_reference_choices_with_measured_pdf_codec_tolerance) {
    std::ifstream input{std::filesystem::path(GVN_SOURCE_ROOT)/L"tests/fixtures/printing/reference-density.tsv"};CHECK(bool(input));
    unsigned count{},seen{};int density,r,g,b,expected_r,expected_g,expected_b,tolerance;
    while(input>>density>>r>>g>>b>>expected_r>>expected_g>>expected_b>>tolerance) {
        CHECK(density>=-5&&density<=5&&(tolerance==1||tolerance==4));
        const auto actual=print_pixel({uint8_t(r),uint8_t(g),uint8_t(b),255},density);
        CHECK(std::abs(int(actual.r)-expected_b)<=tolerance&&std::abs(int(actual.g)-expected_g)<=tolerance&&std::abs(int(actual.b)-expected_r)<=tolerance&&actual.a==255);
        seen|=1u<<(density+5);++count;
    }
    CHECK(input.eof()&&count==3072&&seen==0x7ff);
}
int main(int argc,char **argv){
    if(argc==3&&std::string_view(argv[1])=="--density-lut"){
        std::ofstream output{argv[2]};if(!output)return 5;
        for(int d=-5;d<=5;++d){const PrintColors colors(d);for(int n=0;n<256;++n)output<<d<<' '<<n<<' '<<unsigned(colors.channels[n])<<'\n';}
        output.close();return output.good()?0:5;
    }
    gvn_test::ClipboardLock lock;if(!lock)return 2;auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);if(!station||!gvn_test::noninteractive(station)||!SetProcessWindowStation(station))return 2;auto desktop=CreateDesktopW(L"GVN-print-test",nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desktop||!SetThreadDesktop(desktop))return 3;SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);if(!initialize_controls())return 4;return run_tests();}
