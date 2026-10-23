#include "gvn/prompt_dialog.h"
#include "gvn/ui.h"
#include "gvn/codecs.h"
#include "gvn/edit.h"
#include "test.h"
#include "test_desktop.h"
#include <cstring>
using namespace gvn;
namespace {
const auto fixtures=std::filesystem::path(GVN_FIXTURES);
Image image(const wchar_t *name){return load_image(fixtures/L"prompts"/name);}
PromptDocument document(const wchar_t *name){return describe_prompts(image(name).metadata.prompt_source);}
const PromptField &field(const PromptDocument &d,const std::wstring &group,const std::wstring &label){for(const auto &f:d.fields)if(f.group==group&&f.label==label)return f;throw std::runtime_error("Missing prompt field");}
std::wstring text(HWND h){std::wstring out(size_t(GetWindowTextLengthW(h))+1,L'\0');GetWindowTextW(h,out.data(),int(out.size()));out.pop_back();return out;}
std::wstring clipboard(HWND owner){CHECK(OpenClipboard(owner));const auto memory=GetClipboardData(CF_UNICODETEXT);CHECK(memory);const auto ptr=static_cast<const wchar_t*>(GlobalLock(memory));CHECK(ptr);const std::wstring result=ptr;GlobalUnlock(memory);CHECK(CloseClipboard());return result;}
}
TEST(prompt_parts_final_is_authoritative_and_all_categories_remain_distinct){
    const auto d=document(L"parts.png");CHECK(d.state==PromptPartsState::Supported&&d.status.find(L"sdxl")!=d.status.npos);
    CHECK(d.positive==L"RESOLVED POSITIVE\n朝の光, <lora:extra:0.5>"&&d.negative==L"RESOLVED NEGATIVE\nlowres");
    CHECK(field(d,L"スロット",L"日本語原文").value==L"朝の光 ☀");CHECK(field(d,L"スロット",L"日本語の翻訳").value==L"morning light");
    CHECK(field(d,L"メイン",L"プロンプト").value==L"masterpiece, best quality");
    const auto group=L"キャラ タブ1 · 黒髪の子";CHECK(field(d,group,L"OFF · 帽子").muted&&field(d,group,L"OFF · 帽子").value==L"hat");
    CHECK(field(d,group,L"ONトグルのみ").value==L"school uniform");
    CHECK(field(d,L"拡張プロンプト",L"[画風] 柔らかい光 · 重み 1.2").value==L"soft lighting");
    bool default_zero{};for(const auto &f:d.fields)if(f.group==L"拡張プロンプト"&&f.label.find(L"[拡張2]")!=f.label.npos&&f.label.ends_with(L"重み 0"))default_zero=true;CHECK(default_zero);
    CHECK(field(d,L"生成側の記録",L"parameters（元の生成設定を含む）").value.starts_with(L"UPSTREAM POSITIVE"));
    CHECK(field(d,L"詳細",L"元JSON · inpainter-parts").value.find(L"\"final\"")!=std::wstring::npos);
}
TEST(prompt_chunk_variants_duplicates_and_apng_keep_identical_text_and_pixels){
    const auto expected=document(L"parts-only.png");const auto base=load_image(fixtures/L"alpha.png");
    for(const auto name:{L"parts-only.png",L"compressed.png",L"post-idat.png",L"duplicate.png",L"legacy-parts.png"}){
        const auto source=image(name);const auto d=describe_prompts(source.metadata.prompt_source);CHECK(d.state==PromptPartsState::Supported&&d.positive==expected.positive&&d.negative==expected.negative);
        CHECK(source.frames[0].pixels==base.frames[0].pixels);CHECK(source.bytes()>=source.frames[0].pixels.size()*sizeof(Pixel)+source.metadata.prompt_source->byte_size);
    }
    const auto animation=image(L"animation.apng"),original=load_image(fixtures/L"blend.apng");CHECK(animation.frames.size()==original.frames.size());
    CHECK(describe_prompts(animation.metadata.prompt_source).positive==expected.positive);
    for(size_t n=0;n<animation.frames.size();++n)CHECK(animation.frames[n].pixels==original.frames[n].pixels&&animation.frames[n].duration_ms==original.frames[n].duration_ms);
}
TEST(prompt_invalid_unknown_missing_and_plain_parameters_fallback){
    for(const auto name:{L"invalid.png",L"invalid-type.png",L"trailing-json.png"}){const auto d=document(name);CHECK(d.state==PromptPartsState::Invalid&&d.positive==L"UPSTREAM POSITIVE"&&d.negative==L"upstream negative");CHECK(!field(d,L"詳細",L"元JSON · inpainter-parts").value.empty());}
    const auto unknown=document(L"unknown.png");CHECK(unknown.state==PromptPartsState::Unsupported&&unknown.positive==L"UPSTREAM POSITIVE");
    const auto missing=document(L"missing-fields.png");CHECK(missing.state==PromptPartsState::Supported&&missing.positive.empty());CHECK(field(missing,L"キャラ タブ1",L"プロンプト").value.empty());
    CHECK(document(L"a1111.png").positive==L"UPSTREAM POSITIVE"&&document(L"empty-parameters.png").positive.empty());
    CHECK(describe_prompts({}).status==L"プロンプト情報なし");
}
TEST(prompt_reader_bounds_crc_and_compressed_expansion_do_not_change_image){
    auto bytes=read_file(fixtures/L"prompts/parts-only.png");const auto source=read_png_prompt_source(bytes);CHECK(source&&!source->text.empty());
    // First chunk after IHDR is iTXt. Corrupt only its CRC, not the raster.
    const size_t at=33;const size_t length=(size_t(bytes[at])<<24)|(size_t(bytes[at+1])<<16)|(size_t(bytes[at+2])<<8)|bytes[at+3];bytes[at+8+length]^=1;
    const auto broken=read_png_prompt_source(bytes);CHECK(broken&&!broken->issue.empty()&&broken->text.empty());
    for(size_t length:{0u,4u,8u,20u,42u}){const auto partial=read_png_prompt_source(std::span(bytes).first(length));if(partial)CHECK(partial->text.empty()&&!partial->issue.empty());}
    const auto compressed=read_png_prompt_source(read_file(fixtures/L"prompts/compressed-limit.png"));CHECK(compressed&&!compressed->issue.empty()&&compressed->text.empty());
    CHECK(!image(L"software-only.png").metadata.prompt_source);const auto latin=document(L"latin1.png");CHECK(latin.positive==L"café"&&field(latin,L"元メタデータ",L"Autoré").value==L"René");
    auto huge=std::make_shared<PromptSource>();huge->text.emplace_back("inpainter-parts",std::string(1024*1024+1,' '));CHECK(describe_prompts(huge).state==PromptPartsState::Invalid);
}
TEST(prompt_edits_share_original_record_without_reopening_files){
    const auto source=image(L"parts.png");const auto record=source.metadata.prompt_source;CHECK(record);
    for(const auto edited:{crop(source,{0,0,2,1}),resize_image(source,6,4),effect(source,Effect::Negative)})CHECK(edited.metadata.prompt_source==record&&describe_prompts(edited.metadata.prompt_source).positive.starts_with(L"RESOLVED POSITIVE"));
}
TEST(prompt_native_dialog_selection_readonly_copy_empty_and_resize){
    auto data=document(L"parts.png");data.source_name=L"日本語の生成画像.png";
    const auto owner=CreateWindowExW(0,L"STATIC",L"Prompt test",WS_OVERLAPPEDWINDOW,0,0,1000,760,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(owner);
    {
        PromptDialog dialog(data);CHECK(dialog.create(owner)&&dialog.ready());CHECK(text(dialog.control(prompt_control::Status))==data.status);CHECK(text(dialog.control(prompt_control::Value)).starts_with(L"RESOLVED POSITIVE"));
        CHECK((GetWindowLongPtrW(dialog.control(prompt_control::Value),GWL_STYLE)&ES_READONLY)!=0);
        for(size_t n=0;n<data.fields.size();++n)if(data.fields[n].label==L"日本語原文"&&data.fields[n].group==L"スロット"){dialog.select(n);CHECK(text(dialog.control(prompt_control::Value))==L"朝の光 ☀");SendMessageW(dialog.control(prompt_control::Copy),BM_CLICK,0,0);CHECK(clipboard(owner)==L"朝の光 ☀");}
        SendMessageW(dialog.control(prompt_control::Positive),BM_CLICK,0,0);CHECK(clipboard(owner)==data.positive);
        SendMessageW(dialog.control(prompt_control::Negative),BM_CLICK,0,0);CHECK(clipboard(owner)==data.negative);
        for(const auto size:{SIZE{720,460},SIZE{1200,800}}){SetWindowPos(dialog.window(),nullptr,0,0,size.cx,size.cy,SWP_NOMOVE|SWP_NOZORDER);RECT body{},client{};GetWindowRect(dialog.control(prompt_control::Value),&body);GetClientRect(dialog.window(),&client);MapWindowPoints(dialog.window(),nullptr,reinterpret_cast<POINT*>(&client),2);CHECK(body.left>=client.left&&body.right<=client.right&&body.bottom<client.bottom&&body.bottom>body.top);}
        SendMessageW(dialog.window(),WM_COMMAND,IDOK,0);CHECK(!dialog.ready());
    }
    {PromptDialog dialog(describe_prompts({}));CHECK(dialog.create(owner));CHECK(!IsWindowEnabled(dialog.control(prompt_control::Copy))&&!IsWindowEnabled(dialog.control(prompt_control::Positive)));}
    DestroyWindow(owner);
}
int main(){gvn_test::ClipboardLock lock;if(!lock)return 2;const auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);if(!station||!gvn_test::noninteractive(station)||!SetProcessWindowStation(station))return 2;const auto desktop=CreateDesktopW(L"GVN-prompt-test",nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desktop||!SetThreadDesktop(desktop))return 3;SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);if(FAILED(OleInitialize(nullptr))||!initialize_controls())return 4;const auto result=run_tests();OleUninitialize();return result;}
