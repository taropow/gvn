#include "gvn/codecs.h"
#include "gvn/saving.h"
#include "gvn/preferences.h"
#include "gvn/transfer.h"
#include "gvn/ui.h"
#include "../src/image_metadata.h"
#include "test.h"
#include "test_desktop.h"
using namespace gvn;
namespace {
const auto fixtures=std::filesystem::path(GVN_SOURCE_ROOT)/L"tests/fixtures";
struct Owner {
    HWND hwnd=CreateWindowExW(0,L"STATIC",L"Owned bitfield test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,1000,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Owner(){CHECK(hwnd);}~Owner(){DestroyWindow(hwnd);}
};
}
TEST(bitfield_viewer_default_and_raw_mode_match_actual_native_loading_and_save_depth) {
    const auto normalized=Settings{}.loading_options();const LoadingOptions raw;CHECK(normalized.bitfield_to24&&!raw.bitfield_to24);auto viewer_raw=normalized;viewer_raw.bitfield_to24=false;CHECK((viewer_raw.flags()^normalized.flags())==(1u<<15));
    for(const auto *name:{L"rgb555",L"rgb555-fields",L"rgb565"}) {
        const auto path=fixtures/L"auto-bmp"/(std::wstring(name)+L".bmp");const auto bytes=read_file(path);const auto original=decode(bytes,L"BMP",kDecodeLimit,raw),converted=decode(bytes,L"BMP",kDecodeLimit,normalized);CHECK(original.rgb16&&!converted.rgb16);
        const auto reference=load_image(fixtures/L"bitfield-loading"/(L"on-"+std::wstring(name)+L".png"));CHECK(converted.frames[0].pixels==reference.frames[0].pixels);CHECK(original.metadata==converted.metadata);
        const auto sixteen=encode_for_save(original,0,SaveFormat::Bmp,{}),twentyfour=encode_for_save(converted,0,SaveFormat::Bmp,{});CHECK(sixteen[28]==16&&twentyfour[28]==24);CHECK(read_file(path)==bytes);
    }
}
TEST(bitfield_normalization_preserves_all_alpha_copy_png_and_explicit_background_compositing) {
    Owner owner;const auto normalized=Settings{}.loading_options();
    for(const auto *name:{L"alpha-v4.bmp",L"alpha-v5.bmp"}) {
        const auto bytes=read_file(fixtures/L"loading"/name);const auto raw=decode(bytes);const auto image=decode(bytes,L"BMP",kDecodeLimit,normalized);CHECK(image.frames[0].pixels==raw.frames[0].pixels);CHECK(image.frames[0].pixels[1].a==128&&image.frames[0].pixels[2].a==0);
        copy_image(owner.hwnd,image,0);CHECK(paste_image(owner.hwnd).frames[0].pixels==raw.frames[0].pixels);const auto png=encode_for_save(image,0,SaveFormat::Png,{});CHECK(decode(png).frames[0].pixels==raw.frames[0].pixels);
        auto options=normalized;options.preserve_alpha=false;options.background=0x123456;const auto flattened=decode(bytes,L"BMP",kDecodeLimit,options);for(size_t n=0;n<raw.frames[0].pixels.size();++n)CHECK(flattened.frames[0].pixels[n]==over(raw.frames[0].pixels[n],{18,52,86,255}));
        auto broken=bytes;for(int n=0;n<4;++n)broken[58+n]=broken[54+n];auto unchanged=raw;CHECK(!normalize_bmp_bitfields(unchanged,broken));CHECK(unchanged.frames[0].pixels==raw.frames[0].pixels);CHECK(!normalize_bmp_bitfields(unchanged,std::span<const uint8_t>(bytes).first(40)));
    }
}
TEST(bitfield_actual_settings_checkbox_apply_reset_cancel_and_profile_defaults_are_connected) {
    Owner owner;Settings initial;initial.zoom_step=37;Settings applied=initial;unsigned calls{};const auto path=std::filesystem::temp_directory_path()/(L"GVN-bitfield-settings-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    PreferencesDialog dialog(initial,[&](const Settings &next){applied=next;next.save(path);++calls;});CHECK(dialog.create(owner.hwnd));dialog.select_page(PreferencePage::Loading);const auto checkbox=dialog.control(preference_id::BitfieldTo24);CHECK(checkbox&&SendMessageW(checkbox,BM_GETCHECK,0,0)==BST_CHECKED);
    SendMessageW(checkbox,BM_CLICK,0,0);SendMessageW(dialog.control(preference_id::ApplyButton),BM_CLICK,0,0);CHECK(calls==1&&!applied.bitfield_to24&&applied.zoom_step==37);Settings read;read.load(path);CHECK(!read.bitfield_to24&&!read.loading_options().bitfield_to24);
    SendMessageW(dialog.control(preference_id::Reset),BM_CLICK,0,0);SendMessageW(dialog.control(preference_id::ApplyButton),BM_CLICK,0,0);CHECK(calls==2&&applied.bitfield_to24&&applied.zoom_step==37);SendMessageW(checkbox,BM_CLICK,0,0);SendMessageW(dialog.control(IDCANCEL),BM_CLICK,0,0);CHECK(calls==2&&applied.bitfield_to24);
    CHECK(WritePrivateProfileStringW(L"Viewer",L"bitfield_to24",nullptr,path.c_str()));Settings legacy;legacy.load(path);CHECK(legacy.bitfield_to24&&legacy.zoom_step==37);std::filesystem::remove(path);
}
int main(){gvn_test::ClipboardLock lock;if(!lock)return 2;const auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);if(!station||!gvn_test::noninteractive(station)||!SetProcessWindowStation(station))return 2;const auto desktop=CreateDesktopW(L"GVN-bitfield-loading",nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desktop||!SetThreadDesktop(desktop))return 3;SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);if(FAILED(OleInitialize(nullptr))||!initialize_controls())return 4;const auto result=run_tests();OleUninitialize();return result;}
