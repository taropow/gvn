#include "animation_indices.h"
#include "gvn/codecs.h"
#include "indexed_loading.h"
#include <propvarutil.h>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace gvn {
namespace {
void check(HRESULT result){if(FAILED(result))throw std::runtime_error("Cannot read original GIF frame numbers");}
struct ComScope {
    HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    ComScope(){if(FAILED(result)&&result!=RPC_E_CHANGED_MODE)check(result);}
    ~ComScope(){if(SUCCEEDED(result))CoUninitialize();}
};
uint32_t number(IWICMetadataQueryReader *reader,const wchar_t *key,uint32_t initial=0) {
    if(!reader)return initial;PROPVARIANT value;PropVariantInit(&value);auto result=initial;
    if(SUCCEEDED(reader->GetMetadataByName(key,&value))) {
        if(value.vt==VT_UI1)result=value.bVal;else if(value.vt==VT_UI2)result=value.uiVal;
        else if(value.vt==VT_UI4)result=value.ulVal;else if(value.vt==VT_BOOL)result=value.boolVal!=VARIANT_FALSE;
    }
    PropVariantClear(&value);return result;
}
class GifIndices final:public IndexedSource {
    std::vector<uint8_t> encoded;
    uint32_t width,height;
    Pixel clear,background;
    bool preserve_alpha;
    Pixel projected(Pixel value) const{return preserve_alpha?value:over(value,background);}
  public:
    GifIndices(std::span<const uint8_t> data,uint32_t w,uint32_t h,Pixel c,const LoadingOptions &options,std::optional<Pixel> file_background)
        :encoded(data.begin(),data.end()),width(w),height(h),clear(c),background{uint8_t(options.background>>16),uint8_t(options.background>>8),uint8_t(options.background),255},preserve_alpha(options.preserve_alpha) {
        if(options.gif_background&&file_background)background=*file_background;background.a=255;
    }
    size_t bytes() const override{return encoded.size();}
    std::shared_ptr<const IndexedPixels> read(size_t requested) const override {
        ComScope com;ComPtr<IWICImagingFactory> factory;
        check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
        ComPtr<IWICStream> stream;check(factory->CreateStream(&stream));check(stream->InitializeFromMemory(const_cast<BYTE*>(encoded.data()),DWORD(encoded.size())));
        ComPtr<IWICBitmapDecoder> decoder;check(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&decoder));
        UINT frames{};check(decoder->GetFrameCount(&frames));if(requested>=frames)return {};
        ComPtr<IWICMetadataQueryReader> global;decoder->GetMetadataQueryReader(&global);
        if(number(global.Get(),L"/logscrdesc/Width")!=width||number(global.Get(),L"/logscrdesc/Height")!=height)return {};
        const auto preferred=number(global.Get(),L"/logscrdesc/BackgroundColorIndex",256);
        constexpr uint16_t cleared=256;
        const auto count=checked_image_pixels(width,height);std::vector<uint16_t> canvas(count,cleared),previous;
        std::vector<Pixel> rgba(count,clear),previous_rgba;
        Rect old_area{};uint32_t old_disposal{};std::shared_ptr<const IndexedPixels> source;
        for(size_t n=0;n<=requested;++n) {
            if(n) {
                if(old_disposal==3&&!previous.empty()){canvas=previous;rgba=previous_rgba;}
                else if(old_disposal==2)for(int y=0;y<old_area.h;++y) {
                    const auto begin=size_t(y+old_area.y)*width+old_area.x;
                    std::fill_n(canvas.begin()+begin,old_area.w,cleared);std::fill_n(rgba.begin()+begin,old_area.w,clear);
                }
            }
            ComPtr<IWICBitmapFrameDecode> frame;check(decoder->GetFrame(UINT(n),&frame));UINT w{},h{};check(frame->GetSize(&w,&h));
            source=read_indexed(factory.Get(),frame.Get(),w,h,kImageLimit);if(!source)return {};
            ComPtr<IWICMetadataQueryReader> metadata;frame->GetMetadataQueryReader(&metadata);
            const auto left=number(metadata.Get(),L"/imgdesc/Left"),top=number(metadata.Get(),L"/imgdesc/Top"),disposal=number(metadata.Get(),L"/grctlext/Disposal");
            if(left>=width||top>=height||w>width-left||h>height-top||disposal>3)return {};
            if(disposal==3){previous=canvas;previous_rgba=rgba;}else{previous.clear();previous_rgba.clear();}
            for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x) {
                const auto index=source->index(x,y);
                // Track painted raw numbers, not colors. Transparent later pixels
                // leave the prior layer's number in place; final RGBA is verified.
                const auto at=size_t(y+top)*width+x+left;auto color=source->palette[index];
                if(!n||color.a)canvas[at]=index;
                if(!n)rgba[at]=color.a?color:Pixel{clear.r,clear.g,clear.b,0};
                else rgba[at]=over(color,rgba[at]);
            }
            old_area={int(left),int(top),int(w),int(h)};old_disposal=disposal;
        }
        auto result=std::make_shared<IndexedPixels>();result->width=width;result->height=height;result->bits=8;result->palette=source->palette;
        for(auto &color:result->palette)color=projected(color);
        std::array<bool,256> assigned{};
        // Multiple source tables may coexist in the canvas. Keep each painted
        // raw number and require it to have one RGBA value in this final frame.
        for(size_t at=0;at<count;++at)if(canvas[at]!=cleared) {
            const auto index=canvas[at];const auto color=projected(rgba[at]);
            if(index>=result->palette.size())result->palette.resize(size_t(index)+1);
            if(assigned[index]&&result->palette[index]!=color)return {};
            result->palette[index]=color;assigned[index]=true;
        }
        const auto background_index=[&](Pixel wanted)->std::optional<uint8_t> {
            if(preferred<result->palette.size()&&result->palette[preferred]==wanted)return uint8_t(preferred);
            const auto found=std::find(result->palette.begin(),result->palette.end(),wanted);
            if(found!=result->palette.end())return uint8_t(found-result->palette.begin());
            if(result->palette.size()==256)return {};
            const auto index=uint8_t(result->palette.size());result->palette.push_back(wanted);return index;
        };
        result->packed.resize(result->stride()*height);
        for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
            const auto at=size_t(y)*width+x;const auto index=canvas[at];
            if(index==cleared){const auto clear_index=background_index(projected(rgba[at]));if(!clear_index)return {};result->set_index(x,y,*clear_index);}
            else result->set_index(x,y,uint8_t(index));
        }
        return result->valid()?result:std::shared_ptr<const IndexedPixels>{};
    }
};
}
std::shared_ptr<const IndexedSource> gif_index_source(std::span<const uint8_t> bytes,uint32_t width,uint32_t height,Pixel clear,const LoadingOptions &options,std::optional<Pixel> file_background,size_t remaining_budget) {
    if(bytes.size()>remaining_budget||bytes.size()<13||bytes.size()>kSourceLimit)return {};
    if(std::memcmp(bytes.data(),"GIF87a",6)&&std::memcmp(bytes.data(),"GIF89a",6))return {};
    checked_image_pixels(width,height);return std::make_shared<GifIndices>(bytes,width,height,clear,options,file_background);
}
} // namespace gvn
