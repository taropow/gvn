#pragma once
#include "gvn/core.h"
#include <array>
#include <cmath>
#include <utility>

namespace gvn::detail {
inline void validate_resolution(const ImageMetadata &m) {
    if(m.dpi_defined&&(!std::isfinite(m.dpi_x)||!std::isfinite(m.dpi_y)||m.dpi_x<=0||m.dpi_y<=0||
        m.dpi_x/0.0254>INT32_MAX||m.dpi_y/0.0254>INT32_MAX))throw std::invalid_argument("Invalid output resolution");
}
inline std::pair<uint32_t,uint32_t> resolution_ppm(const ImageMetadata &m) {
    validate_resolution(m);
    return {static_cast<uint32_t>(std::max(1ll,std::llround(m.dpi_x/0.0254))),
            static_cast<uint32_t>(std::max(1ll,std::llround(m.dpi_y/0.0254)))};
}
inline void bmp_resolution(std::vector<uint8_t> &bytes,const ImageMetadata &m) {
    if(!m.dpi_defined)return;
    const auto [x,y]=resolution_ppm(m);
    if(bytes.size()<54||bytes[0]!='B'||bytes[1]!='M'||bytes[14]<40)throw std::runtime_error("Invalid encoded BMP header");
    for(unsigned i=0;i<4;++i){bytes[38+i]=static_cast<uint8_t>(x>>(i*8));bytes[42+i]=static_cast<uint8_t>(y>>(i*8));}
}
// Minimal EXIF IFD: XResolution, YResolution and inches. No source orientation tag is copied.
inline std::array<uint8_t,72> resolution_exif(const ImageMetadata &m) {
    validate_resolution(m);std::array<uint8_t,72> bytes{'E','x','i','f',0,0,'I','I',42,0,8};
    const auto put=[&](size_t at,uint32_t value){for(unsigned i=0;i<4;++i)bytes[at+i]=static_cast<uint8_t>(value>>(i*8));};
    bytes[14]=3;
    for(unsigned n=0;n<2;++n){const auto at=16+n*12;bytes[at]=static_cast<uint8_t>(0x1a+n);bytes[at+1]=1;bytes[at+2]=5;put(at+4,1);put(at+8,50+n*8);}
    bytes[40]=0x28;bytes[41]=1;bytes[42]=3;put(44,1);put(48,2);
    for(unsigned n=0;n<2;++n){const auto dpi=n?m.dpi_y:m.dpi_x;const auto denominator=static_cast<uint32_t>(std::min(1000000.,std::floor(UINT32_MAX/dpi)));if(!denominator)throw std::invalid_argument("Output resolution exceeds EXIF range");put(56+n*8,static_cast<uint32_t>(std::llround(dpi*denominator)));put(60+n*8,denominator);}
    return bytes;
}
}
