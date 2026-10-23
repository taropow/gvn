// Local, fixed-token OCR for a test document's tab. Never emits other recognized text.
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>

using namespace winrt::Windows;
std::string token(std::wstring_view value) {
    std::string out;
    for(auto c:value) {
        if(c>=L'a'&&c<=L'z')c-=L'a'-L'A';
        if((c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9'))out+=char(c);
        else if(c>127)return {}; // A test filename is ASCII only.
    }
    return out;
}
int wmain(int argc,wchar_t **argv) {
    if(argc!=3)return 2;
    try {
        const auto wanted=token(argv[2]);
        if(wanted.size()<7||wanted.size()>20||!wanted.starts_with("GVN"))throw std::runtime_error("Invalid test token");
        const auto path=std::filesystem::absolute(argv[1]);
        if(!std::filesystem::is_regular_file(path)||std::filesystem::file_size(path)>10*1024*1024)
            throw std::runtime_error("Invalid OCR input");
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        const auto file=Storage::StorageFile::GetFileFromPathAsync(path.wstring()).get();
        const auto stream=file.OpenReadAsync().get();
        const auto decoder=Graphics::Imaging::BitmapDecoder::CreateAsync(stream).get();
        const auto bitmap=decoder.GetSoftwareBitmapAsync(Graphics::Imaging::BitmapPixelFormat::Bgra8,
                         Graphics::Imaging::BitmapAlphaMode::Premultiplied).get();
        auto engine=Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
        if(Media::Ocr::OcrEngine::IsLanguageSupported(Globalization::Language(L"en-US")))
            engine=Media::Ocr::OcrEngine::TryCreateFromLanguage(Globalization::Language(L"en-US"));
        if(!engine)throw std::runtime_error("Windows OCR language unavailable");
        if(bitmap.PixelWidth()>int(Media::Ocr::OcrEngine::MaxImageDimension())||
           bitmap.PixelHeight()>int(Media::Ocr::OcrEngine::MaxImageDimension()))
            throw std::runtime_error("OCR tile exceeds Windows limit");
        const auto result=engine.RecognizeAsync(bitmap).get();
        std::cout<<"{\"local_windows_ocr\":true,\"other_text_logged\":false,\"matches\":[";
        bool comma{};
        for(const auto &line:result.Lines()) {
            const auto words=line.Words();
            for(uint32_t start=0;start<words.Size();++start) {
                std::string joined;auto bounds=words.GetAt(start).BoundingRect();
                for(uint32_t end=start;end<std::min(words.Size(),start+3);++end) {
                    const auto word=words.GetAt(end);const auto part=token(word.Text());if(part.empty())break;
                    const auto b=word.BoundingRect();
                    if(end>start&&b.X-(bounds.X+bounds.Width)>std::max(bounds.Height,b.Height)*2)break;
                    const double right=std::max(bounds.X+bounds.Width,b.X+b.Width),bottom=std::max(bounds.Y+bounds.Height,b.Y+b.Height);
                    bounds.X=std::min(bounds.X,b.X);bounds.Y=std::min(bounds.Y,b.Y);
                    bounds.Width=float(right-bounds.X);bounds.Height=float(bottom-bounds.Y);joined+=part;
                    if(joined==wanted||joined==wanted+"PNG") {
                        if(comma)std::cout<<',';comma=true;
                        std::cout<<"["<<bounds.X<<','<<bounds.Y<<','<<bounds.Width<<','<<bounds.Height<<"]";break;
                    }
                    if(joined.size()>wanted.size()+3)break;
                }
            }
        }
        std::cout<<"]}\n";return 0;
    } catch(const winrt::hresult_error &e){std::cerr<<"Windows OCR failed: "<<winrt::to_string(e.message())<<'\n';return 1;}
      catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
