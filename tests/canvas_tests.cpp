#include "gvn/edit.h"
#include "gvn/codecs.h"
#include "gvn/resize_dialog.h"
#include "gvn/settings.h"
#include "gvn/ui_theme.h"
#include "test.h"
using namespace gvn;
namespace id=resize_control;
TEST(canvas_nine_anchors_copy_exact_rgba_for_expansion_crop_and_mixed_axes) {
    const auto source=load_image(std::filesystem::path(GVN_FIXTURES)/L"alpha.png");
    const Pixel fill{17,39,61,33};
    for(int anchor=0;anchor<9;++anchor)for(const auto size:{std::pair{6u,5u},std::pair{2u,1u},std::pair{2u,5u}}) {
        const auto out=canvas_image(source,size.first,size.second,CanvasOptions{anchor,fill});
        const int dx=(int(size.first)-int(source.width))*(anchor%3)/2,dy=(int(size.second)-int(source.height))*(anchor/3)/2;
        for(unsigned y=0;y<out.height;++y)for(unsigned x=0;x<out.width;++x) {
            const int sx=int(x)-dx,sy=int(y)-dy;
            const auto expected=sx>=0&&sx<int(source.width)&&sy>=0&&sy<int(source.height)?source.frames[0].pixels[size_t(sy)*source.width+sx]:fill;
            CHECK(out.frames[0].pixels[size_t(y)*out.width+x]==expected);
        }
    }
    const auto center=canvas_image(source,5,4);CHECK(center.frames[0].pixels[6]==source.frames[0].pixels[0]&&center.frames[0].pixels[0]==Pixel{});
}
TEST(canvas_identity_preserves_palette_and_hidden_rgba_while_limits_precede_allocation) {
    const auto source=load_image(std::filesystem::path(GVN_FIXTURES)/L"palette/png-4-alpha.png");
    const auto same=canvas_image(source,source.width,source.height,CanvasOptions{8,{20,40,60,80}});
    CHECK(same.frames[0].pixels==source.frames[0].pixels&&same.frames[0].indexed==source.frames[0].indexed&&same.frames[0].indexed);
    size_t allocations{};EditMemory memory{16,false,[&](size_t){++allocations;}};
    THROWS(canvas_image(source,3,2,CanvasOptions{},memory));CHECK(!allocations);
    THROWS(canvas_image(source,1,1,CanvasOptions{9},memory));CHECK(!allocations);
    THROWS(canvas_image(Image{},1,1,CanvasOptions{},memory));CHECK(!allocations);
}
TEST(canvas_controls_anchor_aspect_rgba_validation_transparency_and_cancel_without_allocation) {
    HWND owner=CreateWindowExW(0,L"STATIC",L"Owned canvas test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,900,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(owner);
    size_t allocations{};EditMemory memory{1024,false,[&](size_t){++allocations;}};
    auto dialog=ResizeDialog::for_canvas(3,2,{},[&](uint32_t w,uint32_t h){memory.pixels(w,h);});CHECK(dialog->create(owner));
    CHECK(dialog->valid()&&dialog->canvas_options().anchor==4&&!dialog->canvas_options().keep_aspect);
    for(int n=0;n<9;++n){const auto cell=dialog->control(id::Anchor+n);SendMessageW(cell,BM_CLICK,0,0);CHECK(dialog->canvas_options().anchor==n);
        RECT box{};GetClientRect(cell,&box);CHECK(box.right==box.bottom&&box.right==MulDiv(20,GetDpiForWindow(cell),96));
        const auto dc=CreateCompatibleDC(nullptr),screen=GetDC(cell);const auto bitmap=CreateCompatibleBitmap(screen,box.right,box.bottom);ReleaseDC(cell,screen);const auto old=SelectObject(dc,bitmap);
        SetFocus(dialog->control(id::Width));SendMessageW(cell,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
        CHECK(GetPixel(dc,box.right/2,box.bottom/2)==ui_palette().accent);CHECK(GetPixel(dc,3,3)==ui_palette().accent);
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
        for(int j=0;j<9;++j)CHECK(SendMessageW(dialog->control(id::Anchor+j),BM_GETCHECK,0,0)==(n==j?BST_CHECKED:BST_UNCHECKED));}
    SendMessageW(dialog->control(id::Anchor),BM_CLICK,0,0);SendMessageW(dialog->control(id::Anchor),WM_KEYDOWN,VK_RIGHT,0);CHECK(dialog->canvas_options().anchor==1);
    SendMessageW(dialog->control(id::Anchor+1),WM_KEYDOWN,VK_DOWN,0);CHECK(dialog->canvas_options().anchor==4);
    SendMessageW(dialog->control(id::Anchor+8),BM_CLICK,0,0);
    CHECK(!IsWindowEnabled(dialog->control(id::Red)));SendMessageW(dialog->control(id::Transparent),BM_CLICK,0,0);
    CHECK(IsWindowEnabled(dialog->control(id::Red))&&IsWindowEnabled(dialog->control(id::Color)));
    SetWindowTextW(dialog->control(id::Red),L"256");CHECK(!dialog->valid()&&!IsWindowEnabled(dialog->control(IDOK)));
    SendMessageW(dialog->control(id::Transparent),BM_CLICK,0,0);CHECK(dialog->valid());
    SendMessageW(dialog->control(id::Transparent),BM_CLICK,0,0);CHECK(!dialog->valid());
    SetWindowTextW(dialog->control(id::Red),L"17");SetWindowTextW(dialog->control(id::Green),L"39");SetWindowTextW(dialog->control(id::Blue),L"61");SetWindowTextW(dialog->control(id::Alpha),L"33");CHECK(dialog->valid());
    SendMessageW(dialog->control(id::KeepAspect),BM_CLICK,0,0);SetWindowTextW(dialog->control(id::Width),L"6");
    CHECK(dialog->canvas_options().width==6&&dialog->canvas_options().height==4);
    CHECK(dialog->canvas_options().color==0x11273d&&dialog->canvas_options().alpha==33&&!allocations);
    SendMessageW(dialog->control(IDOK),BM_CLICK,0,0);CHECK(dialog->canvas_result()&&dialog->canvas_result()->anchor==8&&!allocations);
    dialog=ResizeDialog::for_canvas(3,2,{});CHECK(dialog->create(owner));SendMessageW(dialog->control(IDCANCEL),BM_CLICK,0,0);CHECK(!dialog->canvas_result());
    dialog.reset();DestroyWindow(owner);
}
TEST(canvas_settings_persist_all_anchors_background_and_transparency) {
    const auto path=std::filesystem::temp_directory_path()/(L"GVN-canvas-"+std::to_wstring(GetCurrentProcessId())+L".ini");std::filesystem::remove(path);
    Settings settings;settings.load(path);CHECK(settings.canvas_anchor==4&&settings.canvas_transparent&&!settings.canvas_keep_aspect&&settings.canvas_alpha==255);
    for(int n=0;n<9;++n){settings.canvas_anchor=n;settings.canvas_keep_aspect=n%2;settings.canvas_transparent=n%2==0;settings.canvas_color=0x11273d;settings.canvas_alpha=33;settings.save(path);
        Settings saved;saved.load(path);CHECK(saved.canvas_anchor==n&&saved.canvas_keep_aspect==settings.canvas_keep_aspect&&saved.canvas_transparent==settings.canvas_transparent&&saved.canvas_color==0x11273d&&saved.canvas_alpha==33);}
    std::filesystem::remove(path);
}
TEST(canvas_pixel_placement_does_not_composite_over_fill_or_mutate_source) {
    Image source;source.width=2;source.height=1;source.frames.push_back({{{123,45,67,0},{201,17,93,1}},100});
    const auto out=canvas_image(source,3,2,CanvasOptions{8,{17,39,61,33}});
    CHECK(out.frames[0].pixels==std::vector<Pixel>{{17,39,61,33},{17,39,61,33},{17,39,61,33},{17,39,61,33},{123,45,67,0},{201,17,93,1}});
    CHECK(source.frames[0].pixels==std::vector<Pixel>{{123,45,67,0},{201,17,93,1}});
}
