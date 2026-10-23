#include "gvn/color_depth.h"
#include "gvn/codecs.h"
#include "gvn/saving.h"
#include "test.h"
#include <fstream>
#include <set>
using namespace gvn;
namespace {
const auto fixtures=std::filesystem::path(GVN_SOURCE_ROOT)/L"tests/fixtures";
Image flat(unsigned count) {
    Image image;image.width=count;image.height=1;image.frames.resize(1);
    for(unsigned i=0;i<count;++i)image.frames[0].pixels.push_back({uint8_t(i),uint8_t(i>>8),uint8_t(i*71),uint8_t(i*53)});
    return image;
}
Image gradient(bool alpha) {
    Image image;image.width=67;image.height=41;image.frames.resize(1);
    for(unsigned y=0;y<41;++y)for(unsigned x=0;x<67;++x)image.frames[0].pixels.push_back({uint8_t(x*19+y*7),uint8_t(x*5+y*11),uint8_t(x*3+y*31),uint8_t(alpha?(x*13+y*17)%256:255)});
    return image;
}
uint32_t le32(const uint8_t *p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
std::vector<uint8_t> read(const std::filesystem::path &path){std::ifstream file(path,std::ios::binary);CHECK(file.good());return {std::istreambuf_iterator<char>(file),{}};}
std::vector<uint16_t> codes(const std::vector<uint8_t> &bytes) {
    CHECK(bytes.size()>=54&&bytes[28]==16);const auto w=le32(bytes.data()+18);const auto h=int32_t(le32(bytes.data()+22));
    const auto height=uint32_t(h<0?-int64_t(h):h);const auto offset=le32(bytes.data()+10),stride=(w*2u+3)&~3u;std::vector<uint16_t> result;
    CHECK(size_t(offset)+size_t(stride)*height<=bytes.size());
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<w;++x){const auto at=offset+size_t(h<0?y:height-1-y)*stride+x*2ull;result.push_back(uint16_t(bytes[at])|(uint16_t(bytes[at+1])<<8));}
    return result;
}
}
TEST(color_depth_eight_bit_promotion_keeps_duplicate_unused_numbers_and_every_alpha_byte) {
    auto image=load_image(fixtures/L"palette/png-4-alpha.png");
    auto subset=std::make_shared<IndexedPixels>(*image.frames[0].indexed);std::set<unsigned> used;
    for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x){const auto n=uint8_t(subset->index(x,y)%8);subset->set_index(x,y,n);image.frames[0].pixels[size_t(y)*image.width+x]=subset->palette[n];used.insert(n);}
    image.frames[0].indexed=subset;CHECK(used.size()<subset->palette.size());
    const auto before=image.frames[0].pixels;const auto indices=image.frames[0].indexed;
    CHECK(color_depth_bits(image)==4&&indices->palette[0].r==indices->palette[1].r);
    const auto result=convert_color_depth(image,{ColorDepth::Indexed8,false});CHECK(result&&color_depth_bits(*result)==8);
    CHECK(result->frames[0].pixels==before&&result->frames[0].indexed->palette==indices->palette);
    for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x)CHECK(result->frames[0].indexed->index(x,y)==indices->index(x,y));
    const auto saved=decode(encode_png(*result,0));CHECK(saved.frames[0].pixels==before&&saved.frames[0].indexed->palette==indices->palette);
    CHECK(!convert_color_depth(*result,{ColorDepth::Indexed8,true})&&image.frames[0].pixels==before&&image.frames[0].indexed==indices);
}
TEST(color_depth_optimal_is_lossless_for_one_four_eight_bits_and_skips_no_change) {
    for(const auto [count,bits]:{std::pair{2u,1u},{3u,4u},{17u,8u}}) {
        const auto image=flat(count);const auto before=image.frames[0].pixels;
        const auto converted=convert_color_depth(image,{ColorDepth::Optimal,true});CHECK(converted&&color_depth_bits(*converted)==bits&&converted->frames[0].pixels==before);
        const auto bytes=encode_png(*converted,0);CHECK(bytes[24]==bits&&bytes[25]==3&&decode(bytes).frames[0].pixels==before);
        CHECK(!convert_color_depth(*converted,{ColorDepth::Optimal,false})&&image.frames[0].pixels==before);
    }
    const auto image=flat(257);unsigned allocations{};EditMemory memory;memory.before_allocate=[&](size_t){++allocations;};
    CHECK(!convert_color_depth(image,{ColorDepth::Optimal,true},memory)&&allocations==0);
    CHECK(!convert_color_depth(image,{ColorDepth::Rgb24,true},memory)&&allocations==0);
}
TEST(color_depth_24_bit_promotion_preserves_hidden_rgb_metadata_and_discards_only_palette_representation) {
    auto image=load_image(fixtures/L"palette/png-4-alpha.png");image.metadata.dpi_defined=true;image.metadata.dpi_x=123.45;image.metadata.dpi_y=234.56;
    const auto before=image.frames[0].pixels;const auto source_indices=image.frames[0].indexed;
    const auto output=convert_color_depth(image,{ColorDepth::Rgb24,true});CHECK(output&&color_depth_bits(*output)==24&&!output->frames[0].indexed&&!output->rgb16);
    CHECK(output->frames[0].pixels==before&&output->metadata==image.metadata&&output->path==image.path&&output->file_bytes==image.file_bytes);
    const auto bytes=encode_png(*output,0);CHECK(bytes[25]==6&&decode(bytes).frames[0].pixels==before);
    CHECK(image.frames[0].indexed==source_indices&&image.frames[0].pixels==before);
}
TEST(color_depth_555_565_non_dither_matches_official_component_codes_and_roundtrips_bmp) {
    const auto image=load_image(fixtures/L"color-depth/gradient.png");const auto before=image.frames[0].pixels;
    for(const auto target:{ColorDepth::Rgb555,ColorDepth::Rgb565}) {
        const auto output=convert_color_depth(image,{target,false});CHECK(output&&output->rgb16&&color_depth_bits(*output)==16);
        const auto reference=read(fixtures/(target==ColorDepth::Rgb555?L"color-depth/reference-555.bmp":L"color-depth/reference-565.bmp"));
        const auto bytes=encode_for_save(*output,0,SaveFormat::Bmp,{});CHECK(codes(bytes)==codes(reference));
        const auto decoded=decode(bytes);
        if(decoded.frames[0].pixels!=output->frames[0].pixels){std::cout<<"16bit expansion diagnostic target="<<int(target)<<'\n';for(size_t i=0;i<before.size();++i)if(decoded.frames[0].pixels[i]!=output->frames[0].pixels[i]){auto a=output->frames[0].pixels[i],b=decoded.frames[0].pixels[i];std::cout<<i<<" expected="<<unsigned(a.r)<<','<<unsigned(a.g)<<','<<unsigned(a.b)<<" decoded="<<unsigned(b.r)<<','<<unsigned(b.g)<<','<<unsigned(b.b)<<'\n';break;}}
        CHECK(decoded.frames[0].pixels==output->frames[0].pixels&&decoded.rgb16);
        CHECK(decode(encode_png(*output,0)).frames[0].pixels==output->frames[0].pixels);
        CHECK(!convert_color_depth(*output,{target,true})&&!convert_color_depth(*output,{ColorDepth::Optimal,true}));
        const auto promoted=convert_color_depth(*output,{ColorDepth::Rgb24,false});CHECK(promoted&&!promoted->rgb16&&promoted->frames[0].pixels==output->frames[0].pixels);
        CHECK(encode_for_save(*promoted,0,SaveFormat::Bmp,{})[28]==24);
    }
    CHECK(image.frames[0].pixels==before&&!image.rgb16);
}
TEST(color_depth_rgb_reduction_and_dither_keep_full_alpha_and_do_not_flatten_transparent_pixels) {
    const auto image=gradient(true);const auto before=image.frames[0].pixels;
    for(const auto target:{ColorDepth::Indexed8,ColorDepth::Rgb555,ColorDepth::Rgb565}) {
        const auto plain=convert_color_depth(image,{target,false}),dithered=convert_color_depth(image,{target,true});CHECK(plain&&dithered);
        CHECK(plain->frames[0].pixels!=dithered->frames[0].pixels&&!plain->rgb16&&!dithered->rgb16);
        for(const auto *result:{&*plain,&*dithered}) {
            std::set<uint32_t> rgb;
            for(size_t i=0;i<before.size();++i){const auto p=result->frames[0].pixels[i];CHECK(p.a==before[i].a);rgb.insert((uint32_t(p.r)<<16)|(uint32_t(p.g)<<8)|p.b);}
            if(target==ColorDepth::Indexed8)CHECK(rgb.size()<=256&&!result->frames[0].indexed);
            const auto bytes=encode_png(*result,0);CHECK(bytes[25]==6&&decode(bytes).frames[0].pixels==result->frames[0].pixels);
        }
    }
    CHECK(image.frames[0].pixels==before);
}
TEST(color_depth_invalid_inputs_limits_and_allocation_failure_preserve_source) {
    const auto image=load_image(fixtures/L"palette/png-4-alpha.png");const auto before=image.frames[0].pixels;const auto table=image.frames[0].indexed;
    THROWS(convert_color_depth({},{}));auto invalid=image;invalid.frames.push_back(invalid.frames[0]);THROWS(convert_color_depth(invalid,{}));
    THROWS(convert_color_depth(image,{static_cast<ColorDepth>(99),false}));EditMemory memory;memory.physical_bytes=1;
    THROWS(convert_color_depth(image,{ColorDepth::Rgb24,false},memory));unsigned allocations{};
    memory.physical_bytes=UINT64_MAX;memory.before_allocate=[&](size_t){++allocations;throw std::bad_alloc();};THROWS(convert_color_depth(image,{ColorDepth::Indexed8,true},memory));CHECK(allocations==1);
    auto stale=image;stale.frames[0].pixels[0]={1,3,7,11};CHECK(color_depth_bits(stale)==24);
    const auto fresh=convert_color_depth(stale,{ColorDepth::Indexed8,false});CHECK(fresh&&fresh->frames[0].pixels==stale.frames[0].pixels);
    CHECK(image.frames[0].pixels==before&&image.frames[0].indexed==table);
}
int main(){return run_tests();}
