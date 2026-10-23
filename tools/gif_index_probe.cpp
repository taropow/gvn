// Probe raw WIC frame palettes/numbers of generated fixtures; no GUI or clipboard.
#include "gvn/codecs.h"
#include "../src/indexed_loading.h"
#include <propvarutil.h>
#include <fstream>
#include <iostream>
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("WIC index probe failed");}
struct ComScope {
    ComScope(){check(CoInitializeEx(nullptr,COINIT_MULTITHREADED));}
    ~ComScope(){CoUninitialize();}
};
uint32_t number(IWICMetadataQueryReader *reader,const wchar_t *key,uint32_t initial=0) {
    if(!reader)return initial;PROPVARIANT v;PropVariantInit(&v);auto result=initial;
    if(SUCCEEDED(reader->GetMetadataByName(key,&v))){if(v.vt==VT_UI1)result=v.bVal;else if(v.vt==VT_UI2)result=v.uiVal;else if(v.vt==VT_UI4)result=v.ulVal;else if(v.vt==VT_BOOL)result=v.boolVal!=VARIANT_FALSE;}
    PropVariantClear(&v);return result;
}
}
int wmain(int argc,wchar_t **argv) {
    if(argc!=3)return 2;
    try {
        ComScope com;
        const auto bytes=gvn::read_file(argv[1]);ComPtr<IWICImagingFactory> factory;
        check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
        ComPtr<IWICStream> stream;check(factory->CreateStream(&stream));check(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()),DWORD(bytes.size())));
        ComPtr<IWICBitmapDecoder> decoder;check(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&decoder));
        UINT count{};check(decoder->GetFrameCount(&count));ComPtr<IWICMetadataQueryReader> global;decoder->GetMetadataQueryReader(&global);
        std::ofstream out{std::filesystem::path(argv[2])};if(!out)throw std::runtime_error("Cannot write probe report");
        out<<"{\"width\":"<<number(global.Get(),L"/logscrdesc/Width")<<",\"height\":"<<number(global.Get(),L"/logscrdesc/Height")<<",\"background\":"<<number(global.Get(),L"/logscrdesc/BackgroundColorIndex")<<",\"frames\":[";
        for(UINT n=0;n<count;++n) {
            if(n)out<<',';ComPtr<IWICBitmapFrameDecode> frame;check(decoder->GetFrame(n,&frame));UINT width{},height{};check(frame->GetSize(&width,&height));
            const auto indices=gvn::read_indexed(factory.Get(),frame.Get(),width,height,gvn::kDecodeLimit);if(!indices)throw std::runtime_error("No original indexed WIC frame");
            ComPtr<IWICMetadataQueryReader> metadata;frame->GetMetadataQueryReader(&metadata);
            out<<"{\"width\":"<<width<<",\"height\":"<<height<<",\"left\":"<<number(metadata.Get(),L"/imgdesc/Left")<<",\"top\":"<<number(metadata.Get(),L"/imgdesc/Top")<<",\"descriptor_width\":"<<number(metadata.Get(),L"/imgdesc/Width")<<",\"descriptor_height\":"<<number(metadata.Get(),L"/imgdesc/Height")<<",\"disposal\":"<<number(metadata.Get(),L"/grctlext/Disposal")<<",\"transparent\":"<<number(metadata.Get(),L"/grctlext/TransparencyFlag")<<",\"transparent_index\":"<<number(metadata.Get(),L"/grctlext/TransparentColorIndex")<<",\"bits\":"<<indices->bits<<",\"palette\":[";
            for(size_t i=0;i<indices->palette.size();++i){if(i)out<<',';const auto p=indices->palette[i];out<<'['<<unsigned(p.r)<<','<<unsigned(p.g)<<','<<unsigned(p.b)<<','<<unsigned(p.a)<<']';}
            out<<"],\"indices\":[";bool comma=false;for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){if(comma)out<<',';comma=true;out<<unsigned(indices->index(x,y));}out<<"]}";
        }
        out<<"]}\n";if(!out)throw std::runtime_error("Cannot finish probe report");std::cout<<count<<" indexed WIC frames\n";return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
