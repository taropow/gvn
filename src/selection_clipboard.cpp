#include "gvn/selection_clipboard.h"
#include <bit>
#include <cstring>
namespace gvn {
namespace {
constexpr uint8_t magic[]{'G','V','N','S','E','L','1',0};
uint32_t get(std::span<const uint8_t> b,size_t p){return uint32_t(b[p])|(uint32_t(b[p+1])<<8)|(uint32_t(b[p+2])<<16)|(uint32_t(b[p+3])<<24);}
void put(std::span<uint8_t> b,size_t p,uint32_t v){for(int i=0;i<4;++i)b[p+i]=uint8_t(v>>(8*i));}
bool dimension(uint32_t v){return v>0&&v<=65535;}
}
SelectionClipboard decode_selection_clipboard(std::span<const uint8_t> b) {
    const auto fail=[](){throw std::runtime_error("Invalid GVN.Selection.v1 clipboard payload");};
    if(b.size()<64||std::memcmp(b.data(),magic,8)||get(b,8)!=64||get(b,12)!=1||(get(b,40)&~1u))fail();
    for(size_t i=48;i<64;++i)if(b[i])fail();
    for(size_t i:{24,28,32,36})if(!dimension(get(b,i)))fail();
    const auto length=get(b,44);
    if(length<8||length>512ull*1024*1024||length>b.size()-64)fail();
    const uint8_t png_magic[]{137,80,78,71,13,10,26,10};
    if(std::memcmp(b.data()+64,png_magic,8))fail();
    auto image=decode(b.subspan(64,length),L".png");
    if(image.width!=get(b,32)||image.height!=get(b,36))fail();
    image.path.clear();SelectionClipboard value{std::move(image)};
    if(get(b,40)&1)value.origin=SelectionOrigin{std::bit_cast<int32_t>(get(b,16)),std::bit_cast<int32_t>(get(b,20)),get(b,24),get(b,28)};
    return value;
}
std::array<uint8_t,64> selection_clipboard_header(size_t png_bytes,uint32_t width,uint32_t height,SelectionOrigin origin,bool known) {
    if(!dimension(width)||!dimension(height)||!dimension(origin.canvas_width)||!dimension(origin.canvas_height)||png_bytes>512ull*1024*1024)
        throw std::invalid_argument("Invalid selection metadata dimensions");
    std::array<uint8_t,64> result{};std::memcpy(result.data(),magic,8);
    put(result,8,64);put(result,12,1);put(result,16,std::bit_cast<uint32_t>(origin.x));put(result,20,std::bit_cast<uint32_t>(origin.y));
    put(result,24,origin.canvas_width);put(result,28,origin.canvas_height);put(result,32,width);put(result,36,height);
    put(result,40,known?1:0);put(result,44,uint32_t(png_bytes));return result;
}
std::vector<uint8_t> encode_selection_clipboard(std::span<const uint8_t> png,uint32_t width,uint32_t height,SelectionOrigin origin,bool known) {
    const auto header=selection_clipboard_header(png.size(),width,height,origin,known);
    std::vector<uint8_t> result(64+png.size());std::copy(header.begin(),header.end(),result.begin());
    std::copy(png.begin(),png.end(),result.begin()+64);return result;
}
} // namespace gvn
