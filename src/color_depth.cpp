#include "gvn/color_depth.h"
#include "gvn/saving.h"
#include <array>
#include <cmath>
#include <unordered_map>

namespace gvn {
namespace {
uint32_t key(Pixel p) {return uint32_t(p.r)|(uint32_t(p.g)<<8)|(uint32_t(p.b)<<16)|(uint32_t(p.a)<<24);}
const IndexedPixels *indices(const Image &image) {
    if(image.frames.size()!=1)return nullptr;
    const auto *p=image.frames[0].indexed.get();
    return p&&p->width==image.width&&p->height==image.height&&p->matches(image.frames[0].pixels)?p:nullptr;
}
std::shared_ptr<IndexedPixels> exact_indices(const Image &image,unsigned bits=0,unsigned skip_bits=0,const EditMemory *memory=nullptr) {
    std::unordered_map<uint32_t,uint8_t> entries;std::vector<Pixel> palette;
    for(auto p:image.frames[0].pixels) {
        if(entries.contains(key(p)))continue;
        if(palette.size()==256)return {};
        entries.emplace(key(p),uint8_t(palette.size()));palette.push_back(p);
    }
    const auto target=bits?bits:palette.size()<=2?1u:palette.size()<=16?4u:8u;
    if(target==skip_bits)return {};
    if(memory)memory->pixels(image.width,image.height);
    auto output=std::make_shared<IndexedPixels>();output->width=image.width;output->height=image.height;
    output->bits=target;output->palette=std::move(palette);
    output->packed.resize(output->stride()*output->height);
    for(uint32_t y=0;y<image.height;++y)for(uint32_t x=0;x<image.width;++x)
        output->set_index(x,y,entries.at(key(image.frames[0].pixels[size_t(y)*image.width+x])));
    return output;
}
Image output_image(const Image &source,const EditMemory &memory) {
    Image out;out.width=source.width;out.height=source.height;out.path=source.path;out.format=source.format;
    out.file_bytes=source.file_bytes;out.load_ms=source.load_ms;out.metadata=source.metadata;
    out.frames.push_back({memory.allocate(out.width,out.height),source.frames[0].duration_ms});return out;
}
// Match the WIC decoder's bit replication, so an automatic16bit BMP roundtrip
// retains displayed RGB as well as the reference component codes.
uint8_t expand(unsigned code,unsigned bits) {return uint8_t((code<<(8-bits))|(code>>(2*bits-8)));}
}
unsigned color_depth_bits(const Image &image) {
    if(image.frames.size()!=1||image.frames[0].pixels.size()!=checked_image_pixels(image.width,image.height))
        throw std::invalid_argument("Color depth requires a valid still frame");
    if(const auto *p=indices(image))return p->bits;
    if(image.rgb16&&image.rgb16->matches(image.frames[0].pixels))return 16;
    return 24;
}
std::optional<Image> convert_color_depth(const Image &image,ColorDepthChoice choice,const EditMemory &memory) {
    const auto bits=color_depth_bits(image);const auto &pixels=image.frames[0].pixels;
    if(choice.depth==ColorDepth::Optimal) {
        if(bits==1||bits==16)return {};
        const auto indexed=exact_indices(image,0,bits,&memory);
        if(!indexed)return {};
        auto out=output_image(image,memory);out.frames[0].pixels=pixels;out.frames[0].indexed=indexed;return out;
    }
    if(choice.depth==ColorDepth::Rgb24) {
        if(bits==24)return {};
        auto out=output_image(image,memory);out.frames[0].pixels=pixels;return out;
    }
    if(choice.depth==ColorDepth::Indexed8) {
        if(bits==8)return {};
        auto out=output_image(image,memory);auto &result=out.frames[0].pixels;
        if(const auto *original=indices(image)) {
            // A depth promotion retains duplicate/unused entries and exact source numbers.
            auto indexed=std::make_shared<IndexedPixels>();indexed->width=image.width;indexed->height=image.height;
            indexed->bits=8;indexed->palette=original->palette;indexed->packed.resize(pixels.size());
            for(uint32_t y=0;y<image.height;++y)for(uint32_t x=0;x<image.width;++x)
                indexed->set_index(x,y,original->index(x,y));
            result=pixels;out.frames[0].indexed=std::move(indexed);return out;
        }
        // Reuse the output allocation as the quantizer's opaque RGB input. The
        // original image supplies alpha after quantization, including alpha0 RGB.
        for(size_t i=0;i<pixels.size();++i){result[i]=pixels[i];result[i].a=255;}
        const auto rgb=index_image(out,0,8,0xffffff,choice.dither);
        for(size_t i=0;i<pixels.size();++i){result[i]=rgb.palette[rgb.indices[i]];result[i].a=pixels[i].a;}
        out.frames[0].indexed=exact_indices(out,8,0,&memory);return out;
    }
    if(choice.depth!=ColorDepth::Rgb555&&choice.depth!=ColorDepth::Rgb565)
        throw std::invalid_argument("Unknown target color depth");
    const auto layout=choice.depth==ColorDepth::Rgb565?Rgb16Layout::Rgb565:Rgb16Layout::Rgb555;
    if(bits==16&&image.rgb16->layout==layout)return {};
    auto out=output_image(image,memory);auto &result=out.frames[0].pixels;
    auto codes=std::make_shared<Rgb16Colors>();codes->layout=layout;const unsigned channel_bits[]{5,layout==Rgb16Layout::Rgb565?6u:5u,5};
    for(unsigned code=0;code<32;++code)codes->red[expand(code,5)]=codes->blue[expand(code,5)]=uint8_t(code);
    for(unsigned code=0;code<(1u<<channel_bits[1]);++code)codes->green[expand(code,channel_bits[1])]=uint8_t(code);
    using Error=std::array<double,3>;
    std::vector<Error> row,next;if(choice.dither){row.resize(size_t(image.width)+2);next.resize(row.size());}
    bool opaque=true;
    for(uint32_t y=0;y<image.height;++y) {
        for(uint32_t x=0;x<image.width;++x) {
            const auto at=size_t(y)*image.width+x;const auto source=pixels[at];Pixel target=source;
            const int values[]{source.r,source.g,source.b};uint8_t *channels[]{&target.r,&target.g,&target.b};
            for(unsigned channel=0;channel<3;++channel) {
                const double value=std::clamp(values[channel]+(choice.dither?row[size_t(x)+1][channel]:0.),0.,255.);
                const auto maximum=(1u<<channel_bits[channel])-1;
                const auto code=unsigned(std::floor(value*maximum/255.+.5));*channels[channel]=expand(code,channel_bits[channel]);
                if(choice.dither) {
                    const auto error=value-*channels[channel];row[size_t(x)+2][channel]+=error*7/16;
                    next[x][channel]+=error*3/16;next[size_t(x)+1][channel]+=error*5/16;next[size_t(x)+2][channel]+=error/16;
                }
            }
            opaque=opaque&&target.a==255;result[at]=target;
        }
        if(choice.dither){row.swap(next);std::fill(next.begin(),next.end(),Error{});}
    }
    if(opaque)out.rgb16=std::move(codes);return out;
}
} // namespace gvn
