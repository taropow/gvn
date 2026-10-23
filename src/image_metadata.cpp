#include "image_metadata.h"
#include "gvn/prompt_metadata.h"
#include <cmath>
#include <cstring>
namespace gvn {
bool normalize_bmp_bitfields(Image &image,std::span<const uint8_t> bytes) {
    if(bytes.size()<54||bytes[0]!='B'||bytes[1]!='M'||image.frames.size()!=1)return false;
    const auto word=[&](size_t at){return uint32_t(bytes[at])|(uint32_t(bytes[at+1])<<8);};
    const auto dword=[&](size_t at){return word(at)|(word(at+2)<<16);};
    const auto header=dword(14),offset=dword(10),bits=word(28),compression=dword(30),raw_height=dword(22);
    const bool top_down=(raw_height&0x80000000u)!=0;const uint32_t height=top_down?0u-raw_height:raw_height;
    if(header<40||header>bytes.size()-14||offset<14ull+header||offset>bytes.size()||dword(18)!=image.width||height!=image.height||word(26)!=1||
       (bits!=16&&bits!=32)||(compression!=0&&compression!=3&&compression!=6)||(bits==32&&compression==0)||!image.width||!image.height||image.frames[0].pixels.size()!=uint64_t(image.width)*image.height)return false;
    uint32_t masks[]{0x7c00,0x03e0,0x001f};
    if(compression!=0){if(bytes.size()<66||offset<66)return false;for(int n=0;n<3;++n)masks[n]=dword(54+size_t(n)*4);}
    if((masks[0]&masks[1])||(masks[0]&masks[2])||(masks[1]&masks[2]))return false;
    uint32_t shifts[3]{},maxima[3]{};
    for(int n=0;n<3;++n){auto mask=masks[n];if(!mask||(bits==16&&(mask&0xffff0000u)))return false;while(!(mask&1)){++shifts[n];mask>>=1;}if(mask&(mask+1))return false;maxima[n]=mask;}
    const size_t stride=((uint64_t(image.width)*bits+31)/32)*4;if(stride*uint64_t(image.height)>bytes.size()-offset)return false;
    const auto expanded=[&](uint32_t packed,int channel){const uint64_t code=(packed&masks[channel])>>shifts[channel];return uint8_t((code*255+maxima[channel]/2)/maxima[channel]);};
    for(uint32_t y=0;y<image.height;++y){const size_t row=offset+size_t(top_down?y:image.height-1-y)*stride;for(uint32_t x=0;x<image.width;++x){const auto at=row+size_t(x)*(bits/8);const auto packed=bits==16?word(at):dword(at);auto &pixel=image.frames[0].pixels[size_t(y)*image.width+x];pixel.r=expanded(packed,0);pixel.g=expanded(packed,1);pixel.b=expanded(packed,2);}}
    image.rgb16.reset();return true;
}
void retain_bmp16_colors(Image &image, std::span<const uint8_t> bytes, size_t limit,
                         Rgb16Layout decoder_layout, Bmp16RetentionTrace *trace) {
    if (trace) *trace = {};
    const auto skip = [&](const char *reason) { if (trace) trace->reason = reason; };
    image.rgb16.reset();
    if (bytes.size() < 54 || bytes[0] != 'B' || bytes[1] != 'M' || image.frames.size() != 1 ||
        image.bytes() > limit || sizeof(Rgb16Colors) > limit - image.bytes())
        return skip("source shape or memory budget");
    const auto word = [&](size_t p) { return uint32_t(bytes[p]) | (uint32_t(bytes[p + 1]) << 8); };
    const auto dword = [&](size_t p) {
        return word(p) | (word(p + 2) << 16);
    };
    const auto header = dword(14), offset = dword(10), compression = dword(30);
    if ((header != 40 && header != 52 && header != 56 && header != 108 && header != 124) ||
        header > bytes.size() - 14 || offset < 14ull + header || offset > bytes.size() ||
        dword(18) != image.width || word(26) != 1 || word(28) != 16 ||
        (compression != 0 && compression != 3))
        return skip("BMP header fields");
    // Keep the DIB orientation separate from the normalized unsigned image height.
    const uint32_t raw_height = dword(22);
    const bool top_down = (raw_height & 0x80000000u) != 0;
    const uint32_t height = top_down ? 0u - raw_height : raw_height;
    if (!height || height != image.height)
        return skip("signed height");
    auto layout = Rgb16Layout::Rgb555;
    if (compression == 3) {
        if (offset < 66 || bytes.size() < 66 || (header >= 56 && dword(66)))
            return skip("alpha mask or missing masks");
        if (dword(54) == 0xf800 && dword(58) == 0x07e0 && dword(62) == 0x001f)
            layout = Rgb16Layout::Rgb565;
        else if (dword(54) != 0x7c00 || dword(58) != 0x03e0 || dword(62) != 0x001f)
            return skip("unsupported RGB masks");
    }
    if (layout != decoder_layout)
        return skip("decoder layout differs");
    const size_t stride = ((size_t(image.width) * 16 + 31) / 32) * 4;
    if (!image.width || !image.height || stride * image.height > bytes.size() - offset ||
        image.frames[0].pixels.size() != size_t(image.width) * image.height)
        return skip("raster bounds");
    auto result = std::make_shared<Rgb16Colors>();
    result->layout = layout;
    std::array<int, 32> red_values, blue_values;
    std::array<int, 64> green_values;
    red_values.fill(-1); blue_values.fill(-1); green_values.fill(-1);
    const auto remember = [&](auto &inverse, auto &forward, uint8_t value, unsigned code) {
        if ((inverse[value] != 255 && inverse[value] != code) ||
            (forward[code] >= 0 && forward[code] != value)) {
            if (trace) {
                trace->code = code; trace->previous_inverse = inverse[value];
                trace->previous_forward = forward[code];
            }
            return false;
        }
        inverse[value] = static_cast<uint8_t>(code);
        forward[code] = value;
        return true;
    };
    for (uint32_t y = 0; y < image.height; ++y) {
        const size_t row = offset + size_t(top_down ? y : image.height - 1u - y) * stride;
        for (uint32_t x = 0; x < image.width; ++x) {
            const auto packed = word(row + size_t(x) * 2);
            const auto p = image.frames[0].pixels[size_t(y) * image.width + x];
            if (trace) { trace->x = x; trace->y = y; trace->packed = packed; trace->pixel = p; }
            if (p.a != 255 ||
                !remember(result->red, red_values, p.r, (packed >> (layout == Rgb16Layout::Rgb565 ? 11 : 10)) & 31) ||
                !remember(result->green, green_values, p.g, (packed >> 5) & (layout == Rgb16Layout::Rgb565 ? 63 : 31)) ||
                !remember(result->blue, blue_values, p.b, packed & 31))
                return skip("component mapping is not reversible");
        }
    }
    image.rgb16 = std::move(result);
}
bool set_image_resolution(ImageMetadata &m,double x,double y) {
    if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||(!x&&!y))return false;
    if(!x)x=y;if(!y)y=x;
    m.dpi_x=x;m.dpi_y=y;m.dpi_defined=true;return true;
}
void read_container_metadata(Image &image,std::span<const uint8_t> bytes) {
    auto &m=image.metadata;m.original_width=image.width;m.original_height=image.height;
    m.source_format=image.format;m.source_bytes=bytes.size();
    const auto be16=[&](size_t p){return (uint32_t(bytes[p])<<8)|bytes[p+1];};
    const auto be32=[&](size_t p){return (uint32_t(bytes[p])<<24)|(uint32_t(bytes[p+1])<<16)|(uint32_t(bytes[p+2])<<8)|bytes[p+3];};
    const auto le16=[&](size_t p){return uint32_t(bytes[p])|(uint32_t(bytes[p+1])<<8);};
    const auto le32=[&](size_t p){return uint32_t(bytes[p])|(uint32_t(bytes[p+1])<<8)|(uint32_t(bytes[p+2])<<16)|(uint32_t(bytes[p+3])<<24);};
    if(bytes.size()>=33&&!std::memcmp(bytes.data(),"\x89PNG\r\n\x1a\n",8)) {
        m.prompt_source=read_png_prompt_source(bytes);
        static constexpr int channels[]{1,0,3,1,2,0,4};
        if(bytes[25]<std::size(channels))m.original_bpp=bytes[24]*channels[bytes[25]];
        if(bytes[25]==3)m.original_color_type=ImageColorType::Palette;
        else if(bytes[25]==0||bytes[25]==4)m.original_color_type=ImageColorType::Gray;
        else if(bytes[25]==2||bytes[25]==6)m.original_color_type=ImageColorType::Rgb;
        size_t p=8;
        while(p<=bytes.size()&&bytes.size()-p>=12) {
            const size_t count=be32(p);if(count>bytes.size()-p-12)break;
            if(count==9&&!std::memcmp(bytes.data()+p+4,"pHYs",4)&&bytes[p+16]==1)
                set_image_resolution(m,be32(p+8)*.0254,be32(p+12)*.0254);
            if(!std::memcmp(bytes.data()+p+4,"iCCP",4)&&count>2)m.icc_profile=true;
            if(!std::memcmp(bytes.data()+p+4,"IEND",4))break;
            p+=count+12;
        }
    } else if(bytes.size()>=26&&bytes[0]=='B'&&bytes[1]=='M') {
        const auto size=le32(14);
        if(size==12){m.original_bpp=le16(24);m.original_color_type=m.original_bpp<=8?ImageColorType::Palette:ImageColorType::Rgb;}
        else if(size>=40&&bytes.size()>=54) {
            m.original_bpp=le16(28);
            m.original_color_type=m.original_bpp<=8?ImageColorType::Palette:(le32(30)==3||le32(30)==6)?ImageColorType::Bitfields:ImageColorType::Rgb;
            const auto x=static_cast<int32_t>(le32(38)),y=static_cast<int32_t>(le32(42));
            if(x>=0&&y>=0)set_image_resolution(m,x*.0254,y*.0254);
            if(size>=124&&bytes.size()>=138&&le32(70)==0x4d424544&&le32(126)&&le32(130)) {
                const uint64_t start=14ull+le32(126),length=le32(130);
                if(start<=bytes.size()&&length<=bytes.size()-start){m.icc_profile=true;m.icc_bytes=static_cast<uint32_t>(length);}
            }
        }
    } else if(bytes.size()>=13&&!std::memcmp(bytes.data(),"GIF8",4)) {
        m.original_color_type=ImageColorType::Palette;
        if(bytes[10]&0x80)m.original_bpp=(bytes[10]&7)+1;
        size_t p=13+((bytes[10]&0x80)?(size_t{6}<<(bytes[10]&7)):0);
        while(p<bytes.size()) {
            const auto tag=bytes[p++];
            if(tag==0x2c) {
                if(bytes.size()-p>=9&&(bytes[p+8]&0x80))m.original_bpp=(bytes[p+8]&7)+1;
                break;
            }
            if(tag!=0x21||p>=bytes.size())break;
            ++p;
            while(p<bytes.size()){const auto n=bytes[p++];if(!n)break;if(n>bytes.size()-p)return;p+=n;}
        }
    } else if(bytes.size()>=30&&!std::memcmp(bytes.data(),"8BPS",4)) {
        m.original_bpp=be16(12)*be16(22);
        const auto mode=be16(24);m.original_color_type=mode==2?ImageColorType::Palette:mode==4?ImageColorType::Cmyk:(mode==0||mode==1||mode==8)?ImageColorType::Gray:mode==3?ImageColorType::Rgb:ImageColorType::Unknown;
        size_t p=26;const size_t color=be32(p);p+=4;
        if(color>bytes.size()-p)return;p+=color;if(bytes.size()-p<4)return;
        const size_t total=be32(p);p+=4;if(total>bytes.size()-p)return;const auto end=p+total;
        while(p<end&&end-p>=12) {
            if(std::memcmp(bytes.data()+p,"8BIM",4))break;
            const auto id=be16(p+4);p+=6;const size_t name=(size_t(bytes[p])+2)&~size_t{1};
            if(name>end-p)break;p+=name;if(end-p<4)break;
            const size_t n=be32(p);p+=4;if(n>end-p)break;
            if(id==1005&&n>=16)set_image_resolution(m,be32(p)/65536.0,be32(p+8)/65536.0);
            if(id==1039&&n){m.icc_profile=true;m.icc_bytes=static_cast<uint32_t>(n);}
            p+=n;if(n&1){if(p==end)break;++p;}
        }
    } else if(bytes.size()>=4&&bytes[0]==0xff&&bytes[1]==0xd8) {
        size_t p=2;
        while(p<bytes.size()) {
            if(bytes[p++]!=0xff)break;while(p<bytes.size()&&bytes[p]==0xff)++p;if(p==bytes.size())break;
            const auto marker=bytes[p++];if(marker==0xda||marker==0xd9)break;
            if(marker==1||(marker>=0xd0&&marker<=0xd7))continue;
            if(bytes.size()-p<2)break;const size_t n=be16(p);if(n<2||n>bytes.size()-p)break;
            const auto data=p+2;const auto size=n-2;
            if(marker==0xe0&&size>=12&&!std::memcmp(bytes.data()+data,"JFIF\0",5)) {
                const auto units=bytes[data+7];if(units==1||units==2)set_image_resolution(m,be16(data+8)*(units==1?1.:2.54),be16(data+10)*(units==1?1.:2.54));
            }
            if(marker==0xe2&&size>=14&&!std::memcmp(bytes.data()+data,"ICC_PROFILE\0",12))m.icc_profile=true;
            p+=n;
        }
    }
}
}
