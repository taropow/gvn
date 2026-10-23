#include "gvn/edit.h"
#include "test.h"
using namespace gvn;
Image sample() {
    Image image;
    image.width = 3;
    image.height = 2;
    image.frames = {{{{255, 0, 0, 255},
                      {0, 255, 0, 128},
                      {0, 0, 255, 0},
                      {10, 20, 30, 64},
                      {250, 240, 230, 255},
                      {0, 0, 0, 0}},
                     100}};
    return image;
}
TEST(edit_memory_physical_and_decode_budgets_are_independent) {
    EditMemory memory{24, false};
    CHECK(memory.pixels(3, 2) == 6 && memory.pixels(1, 1) == 1);
    THROWS(memory.pixels(7, 1));
    memory.physical_bytes = 23;
    THROWS(memory.allocate(3, 2));
    memory.allow_physical_overflow = true;
    CHECK(memory.allocate(3, 2).size() == 6);
    CHECK(memory.pixels(8193, 8192) == 8193ull * 8192);
    THROWS(checked_pixels(8193, 8192));
    CHECK(memory.pixels(16384, 16384) * sizeof(Pixel) == kImageLimit);
    THROWS(memory.pixels(16385, 16384));
    for (const auto &[width, height] : {std::pair{0ull, 1ull},
                                        {1ull, 0ull},
                                        {65536ull, 1ull},
                                        {UINT64_MAX, UINT64_MAX},
                                        {65535ull, 65535ull}})
        THROWS(memory.pixels(width, height));
    memory.physical_bytes = 0;
    CHECK(memory.pixels(3, 2) == 6);
    memory.allow_physical_overflow = false;
    THROWS(memory.pixels(3, 2));
    CHECK(physical_memory_bytes() > 0);
}
TEST(actual_small_edit_outputs_follow_policy_and_preserve_alpha) {
    const auto image = sample();
    size_t allocations{}, last_bytes{};
    EditMemory memory{23, false, [&](size_t bytes) {
                          ++allocations;
                          last_bytes = bytes;
                      }};
    THROWS(resize_image(image, 3, 2, false, memory));
    THROWS(canvas_image(image, 3, 2, memory));
    THROWS(new_image(3, 2, memory));
    CHECK(!allocations);
    memory.allow_physical_overflow = true;
    const auto resized = resize_image(image, 3, 2, false, memory);
    CHECK(resized.frames[0].pixels == image.frames[0].pixels && last_bytes == 24);
    CHECK(canvas_image(image, 3, 2, memory).frames[0].pixels == image.frames[0].pixels);
    CHECK(new_image(3, 2, memory).frames[0].pixels == std::vector<Pixel>(6));
    CHECK(allocations == 3);
    memory.physical_bytes = 24;
    memory.allow_physical_overflow = false;
    CHECK(resize_image(image, 3, 2, false, memory).frames[0].pixels == image.frames[0].pixels);
    memory.before_allocate = [](size_t) { throw std::bad_alloc(); };
    THROWS(canvas_image(image, 3, 2, memory));
    CHECK(image.frames[0].pixels == sample().frames[0].pixels);
}
TEST(edit_axis_limit_is_supported_with_bounded_actual_allocations) {
    const auto image = sample();
    const auto horizontal = resize_image(image, 65535, 1, false);
    const auto vertical = canvas_image(image, 1, 65535);
    CHECK(horizontal.width == 65535 && horizontal.frames[0].pixels.size() == 65535);
    CHECK(vertical.height == 65535 && vertical.frames[0].pixels.size() == 65535);
    CHECK(still_frame(horizontal, 0).frames[0].pixels == horizontal.frames[0].pixels);
}
TEST(transforms_are_lossless) {
    const auto image = sample();
    auto result = image;
    for (int i = 0; i < 4; ++i)
        result = rotate_quarters(result, 1);
    CHECK(result.frames[0].pixels == image.frames[0].pixels);
    CHECK(flip(flip(image, true), true).frames[0].pixels == image.frames[0].pixels);
    CHECK(flip(flip(image, false), false).frames[0].pixels == image.frames[0].pixels);
    result = rotate_quarters(image, 1);
    CHECK(result.width == 2 && result.height == 3);
    CHECK(result.frames[0].pixels[0] == image.frames[0].pixels[3]);
}
TEST(crop_canvas_join) {
    const auto image = sample();
    auto result = crop(image, {1, 0, 2, 1});
    CHECK(result.width == 2 && result.height == 1 && result.frames[0].pixels[0] == image.frames[0].pixels[1]);
    THROWS(crop(image, {10, 10, 1, 1}));
    result = canvas_image(image, 5, 4);
    CHECK(result.frames[0].pixels[6] == image.frames[0].pixels[0]);
    CHECK(result.frames[0].pixels[0].a == 0);
    result = join_pages(image, crop(image, {0, 0, 1, 1}), true);
    CHECK(result.width == 4 && result.height == 2);
    CHECK(result.frames[0].pixels[1] == image.frames[0].pixels[0]);
}
TEST(resize_transparent_edges) {
    Image image;
    image.width = 2;
    image.height = 1;
    image.frames = {{{{255, 0, 0, 255}, {0, 0, 255, 0}}, 100}};
    const auto result = resize_image(image, 3, 1);
    CHECK(result.frames[0].pixels[1].r == 255);
    CHECK(result.frames[0].pixels[1].b == 0);
    CHECK(result.frames[0].pixels[1].a == 128);
    THROWS(resize_image(image, 0, 1));
}
TEST(effects_preserve_alpha_and_selection) {
    auto image = sample();
    auto result = effect(image, Effect::Negative, 1, {0, 0, 1, 1});
    CHECK(result.frames[0].pixels[0] == Pixel{0, 255, 255, 255});
    CHECK(result.frames[0].pixels[1] == image.frames[0].pixels[1]);
    for (auto type : {Effect::Grayscale, Effect::Sepia, Effect::Brightness, Effect::Contrast, Effect::Gamma,
                      Effect::Saturation, Effect::Hue, Effect::Sharpen, Effect::Edge, Effect::Emboss,
                      Effect::Posterize}) {
        result = effect(image, type, 1);
        for (size_t i = 0; i < 6; ++i)
            CHECK(result.frames[0].pixels[i].a == image.frames[0].pixels[i].a);
    }
    CHECK(effect(effect(image, Effect::Negative), Effect::Negative).frames[0].pixels ==
          image.frames[0].pixels);
}
int main() {
    return run_tests();
}
TEST(paste_transparent_region) {
    const auto image = sample();
    auto layer = sample();
    layer.width = 3;
    layer.height = 2;
    auto result = paste_region(image, layer, {1, 1, 1, 1});
    CHECK(result.frames[0].pixels[4] == Pixel{255, 0, 0, 255});
    CHECK(result.frames[0].pixels[0] == image.frames[0].pixels[0]);
    layer.frames[0].pixels[0] = {250, 20, 0, 0};
    result = paste_region(image, layer, {0, 0, 1, 1});
    CHECK(result.frames[0].pixels == image.frames[0].pixels);
}
TEST(advanced_adjustments_and_alpha) {
    const auto image = sample();
    const auto identity = levels(image, 0, 255, 1);
    CHECK(identity.frames[0].pixels == image.frames[0].pixels);
    THROWS(levels(image, 50, 50, 1));
    auto swapped = swap_channels(image, 3);
    CHECK(swap_channels(swapped, 4).frames[0].pixels == image.frames[0].pixels);
    auto rotated = rotate_image(image, 90);
    CHECK(rotated.frames[0].pixels == rotate_quarters(image, 1).frames[0].pixels);
    rotated = rotate_image(image, 30);
    CHECK(rotated.width == 4 && rotated.height == 4);
    auto blurred = gaussian(image, 1);
    CHECK(blurred.width == 3 && blurred.height == 2);
    auto sharpened = unsharp(image, .6, 0, 0);
    CHECK(sharpened.frames[0].pixels == image.frames[0].pixels);
    auto replaced = replace_color(image, {0, 255, 0, 0}, {20, 30, 40, 255});
    CHECK(replaced.frames[0].pixels[1] == Pixel{20, 30, 40, 128});
}
TEST(mask_roundtrips) {
    const auto image = sample();
    CHECK(xor_mask(xor_mask(image)).frames[0].pixels == image.frames[0].pixels);
    CHECK(glass_mask(glass_mask(image, 2, true), 2, true).frames[0].pixels == image.frames[0].pixels);
    CHECK(glass_mask(glass_mask(image, 2, false), 2, false).frames[0].pixels == image.frames[0].pixels);
    auto wide = resize_image(image, 32, 2, false);
    CHECK(win_mask(win_mask(wide)).frames[0].pixels == wide.frames[0].pixels);
}
TEST(known_color_adjustment_values) {
    Image image;
    image.width = 1;
    image.height = 1;
    image.frames = {{{{64, 128, 192, 77}}, 100}};
    CHECK(effect(image, Effect::Brightness, 20).frames[0].pixels[0] == Pixel{84, 148, 212, 77});
    CHECK(effect(image, Effect::Contrast, 2).frames[0].pixels[0] == Pixel{1, 129, 255, 77});
    CHECK(effect(image, Effect::Gamma, 2).frames[0].pixels[0] == Pixel{128, 181, 221, 77});
    CHECK(effect(image, Effect::Grayscale).frames[0].pixels[0] == Pixel{119, 119, 119, 77});
    CHECK(effect(image, Effect::Saturation, 0).frames[0].pixels[0] == Pixel{119, 119, 119, 77});
    CHECK(effect(image, Effect::Threshold, 120).frames[0].pixels[0] == Pixel{0, 0, 0, 77});
    CHECK(effect(image, Effect::Posterize, 3).frames[0].pixels[0] == Pixel{128, 128, 255, 77});
    CHECK(levels(image, 0, 255, 1, 0, 128).frames[0].pixels[0] == Pixel{32, 64, 96, 77});
}

#include "mask_reference.h"
TEST(existing_mask_primitives_match_official_default_options) {
    const auto source = gvn_test::mask_reference_source();
    size_t checked{};
    for (const auto &item : gvn_test::mask_references()) {
        Image actual;
        if ((item.kind == "vertical" || item.kind == "horizontal") && !item.remainder && item.size <= 256)
            actual = glass_mask(source, item.size, item.kind == "vertical");
        else if (item.kind == "xor" && item.xor_channels == 7)
            actual = xor_mask(source);
        else if (item.kind == "swap")
            actual = swap_channels(source, item.swap);
        else if (item.kind == "image_negative")
            actual = effect(source, Effect::Negative);
        else if (item.kind == "win")
            actual = win_mask(source);
        else continue;
        CHECK(actual.frames[0].pixels == item.expected.frames[0].pixels); ++checked;
    }
    CHECK(checked == 19);
}
TEST(glass_reference_upper_range_matches_actual_999_pixel_setting) {
    const auto source = gvn_test::mask_reference_source();
    size_t checked{};
    for (const auto &item : gvn_test::mask_references())
        if ((item.kind == "vertical" || item.kind == "horizontal") && item.size == 999 && !item.remainder) {
            const auto actual = glass_mask(source, item.size, item.kind == "vertical");
            CHECK(actual.frames[0].pixels == item.expected.frames[0].pixels); ++checked;
        }
    CHECK(checked == 2);
}

TEST(mask_all_560_official_outputs_and_alpha_follow_the_same_source_pixels) {
    const auto opaque = gvn_test::mask_reference_source(), alpha = gvn_test::mask_reference_alpha_source();
    const auto references = gvn_test::mask_references();
    size_t checked{};
    for (const auto &item : references) {
        if (item.kind == "fl") continue;
        const auto expected = gvn_test::mask_reference_alpha_expected(item, references);
        const auto actual = gvn_test::apply_mask_reference(opaque, item);
        if (actual.frames[0].pixels != item.expected.frames[0].pixels)
            std::cout << "MASK_REFERENCE_MISMATCH " << item.label << '\n';
        CHECK(actual.frames[0].pixels == item.expected.frames[0].pixels);
        CHECK(gvn_test::apply_mask_reference(alpha, item).frames[0].pixels == expected.frames[0].pixels);
        ++checked;
    }
    CHECK(checked == 560);
}
TEST(mask_selection_uses_local_coordinates_and_keeps_all_outside_rgba) {
    const auto references = gvn_test::mask_references();
    const auto source = gvn_test::mask_reference_alpha_source();
    Image canvas; canvas.width = 35; canvas.height = 25;
    canvas.frames.push_back({std::vector<Pixel>(35 * 25, {17,39,61,33}), 100});
    for (uint32_t y = 0; y < source.height; ++y)
        std::copy_n(source.frames[0].pixels.begin() + y * 29, 29, canvas.frames[0].pixels.begin() + (y + 2) * 35 + 3);
    for (const auto &item : references) {
        if (item.kind == "fl") continue;
        const auto expected = gvn_test::mask_reference_alpha_expected(item, references);
        const auto actual = gvn_test::apply_mask_reference(canvas, item, {3,2,29,19});
        for (uint32_t y = 0; y < canvas.height; ++y)
            for (uint32_t x = 0; x < canvas.width; ++x) {
                const bool inside = x >= 3 && x < 32 && y >= 2 && y < 21;
                CHECK(actual.frames[0].pixels[y * 35 + x] == (inside ? expected.frames[0].pixels[(y - 2) * 29 + x - 3]
                                                                    : canvas.frames[0].pixels[y * 35 + x]));
            }
    }
}
TEST(mask_indexed_palette_and_indices_remain_consistent_without_mutating_the_source) {
    for (const unsigned bits : {1,2,4,8}) {
        Image image; image.width = 17; image.height = 13;
        auto indexed = std::make_shared<IndexedPixels>(); indexed->width = 17; indexed->height = 13; indexed->bits = bits;
        for (unsigned i = 0; i < (1u << bits); ++i)
            indexed->palette.push_back({static_cast<uint8_t>(i * 31), static_cast<uint8_t>(i * 53),
                                        static_cast<uint8_t>(i * 97), static_cast<uint8_t>(i)});
        indexed->packed.resize(indexed->stride() * image.height);
        image.frames.push_back({std::vector<Pixel>(17 * 13), 100});
        for (uint32_t y = 0; y < 13; ++y) for (uint32_t x = 0; x < 17; ++x) {
            const auto index = static_cast<uint8_t>((y * 17 + x) % (1u << bits));
            indexed->set_index(x,y,index); image.frames[0].pixels[y * 17 + x] = indexed->palette[index];
        }
        image.frames[0].indexed = indexed;
        const auto packed = indexed->packed;
        const auto pixels = image.frames[0].pixels, palette = indexed->palette;
        for (unsigned flags = 0; flags < 64; ++flags) {
            MaskOptions options; options.glass_size = 3; options.q0_flags = flags;
            const auto actual = mask_image(image, Mask::Q0, options);
            const bool cropped = ((flags & 1) && (flags & 32)) || ((flags & 2) && (flags & 16));
            const bool color = (flags & 12) != 0;
            CHECK(bool(actual.frames[0].indexed) == !(cropped && color));
            if (actual.frames[0].indexed) {
                CHECK(actual.frames[0].indexed->bits == bits && actual.frames[0].indexed->matches(actual.frames[0].pixels));
                if (!color) CHECK(actual.frames[0].indexed->palette == palette);
            }
            CHECK(mask_image(actual, Mask::Q0, options).frames[0].pixels == pixels);
            CHECK(indexed->packed == packed && indexed->palette == palette && image.frames[0].pixels == pixels);
        }
        auto actual = mask_image(image, Mask::Negative);
        CHECK(actual.frames[0].indexed && actual.frames[0].indexed->packed == packed);
        CHECK(actual.frames[0].indexed->palette[0] == Pixel{255,255,255,0});
        actual = mask_image(image, Mask::Negative, {}, {1,1,4,5});
        CHECK(!actual.frames[0].indexed && image.frames[0].indexed == indexed);
    }
}
TEST(mask_invalid_options_and_empty_intersection_are_bounded) {
    const auto image = sample(); MaskOptions options;
    options.glass_size = 0; THROWS(mask_image(image, Mask::Q0, options));
    options.glass_size = 1000; THROWS(mask_image(image, Mask::Vertical, options));
    options.glass_size = 8; options.q0_flags = 64; THROWS(mask_image(image, Mask::Q0, options));
    options.q0_flags = 7; options.negative_channels = 8; THROWS(mask_image(image, Mask::Negative, options));
    options.negative_channels = 7; options.xor_channels = 8; THROWS(mask_image(image, Mask::Xor, options));
    options.xor_channels = 7; options.swap = 5; THROWS(mask_image(image, Mask::Swap, options));
    options.swap = 3;
    CHECK(mask_image(image, Mask::Q0, options, {100,100,3,4}).frames[0].pixels == image.frames[0].pixels);
}

#include "fl_reference.h"
#include "cp_reference.h"
TEST(fl_reference_native_rgb_indexed_and_selected_alpha) {
    size_t checked{}, alpha{};
    for (const auto &item : gvn_test::fl_references()) {
        if (item.kind != "fl") continue;
        const auto before = item.source.frames[0].pixels;
        const auto actual = mask_image(item.source, gvn_test::fl_operation(item), item.options,
                                       item.selected ? item.rect : Rect{});
        if (actual.frames[0].pixels != item.expected.frames[0].pixels) std::cout << "FL_MISMATCH " << item.label << '\n';
        CHECK(actual.frames[0].pixels == item.expected.frames[0].pixels);
        gvn_test::fl_check_indexed(actual,item.expected);
        CHECK(mask_image(actual,gvn_test::fl_operation(item),item.options,item.selected?item.rect:Rect{}).frames[0].pixels==before);
        CHECK(item.source.frames[0].pixels == before);
        if (item.alpha) {
            auto source=item.source, expected=item.expected;
            for (size_t i=0;i<before.size();++i) {
                source.frames[0].pixels[i].a=static_cast<uint8_t>(i); expected.frames[0].pixels[i].a=item.expected_alpha[i];
            }
            CHECK(mask_image(source,gvn_test::fl_operation(item),item.options,item.selected?item.rect:Rect{}).frames[0].pixels==expected.frames[0].pixels);
            ++alpha;
        }
        ++checked;
    }
    CHECK(checked==92 && alpha==72);
}
TEST(existing_masks_match_14_actual_selected_rgb_and_palette_results) {
    size_t checked{};
    for (const auto &item : gvn_test::fl_references()) {
        if (item.kind=="fl") continue;
        const auto actual=mask_image(item.source,gvn_test::fl_operation(item),item.options,item.rect);
        CHECK(actual.frames[0].pixels==item.expected.frames[0].pixels);
        gvn_test::fl_check_indexed(actual,item.expected); ++checked;
    }
    CHECK(checked==14);
}
TEST(meko_native_56_outputs_42_alpha_eight_large_permutations_and_limits) {
    const auto operation = [](bool reverse) { return reverse?Mask::MekoReverse:Mask::MekoForward; };
    size_t checked{}, alpha{};
    for (const auto &item : gvn_test::fl_references(L"meko-reference",56)) {
        const bool reverse=item.kind=="meko-reverse";
        const auto rect=item.selected?item.rect:Rect{};
        const auto before=item.source.frames[0].pixels;
        const auto actual=mask_image(item.source,operation(reverse),{},rect);
        CHECK(actual.frames[0].pixels==item.expected.frames[0].pixels);
        gvn_test::fl_check_indexed(actual,item.expected);
        CHECK(mask_image(actual,operation(!reverse),{},rect).frames[0].pixels==before);
        CHECK(item.source.frames[0].pixels==before);
        if(item.alpha) {
            auto source=item.source, expected=item.expected;
            for(size_t i=0;i<before.size();++i) {
                source.frames[0].pixels[i].a=static_cast<uint8_t>(i);expected.frames[0].pixels[i].a=item.expected_alpha[i];
            }
            CHECK(mask_image(source,operation(reverse),{},rect).frames[0].pixels==expected.frames[0].pixels);++alpha;
        }
        ++checked;
    }
    CHECK(checked==56 && alpha==42);
    const auto folder=std::filesystem::path(GVN_FIXTURES)/L"meko-reference";
    std::ifstream rows(folder/L"permutations.tsv");CHECK(rows);checked=0;
    for(std::string line;std::getline(rows,line);) {
        std::istringstream row(line);std::string kind,file;unsigned width{},height{};size_t count{};
        CHECK(row>>kind>>width>>height>>file>>count);
        std::ifstream permutation(folder/file,std::ios::binary);CHECK(permutation);
        auto source=new_image(width,height);auto &pixels=source.frames[0].pixels;
        for(size_t i=0;i<pixels.size();++i)pixels[i]={static_cast<uint8_t>(i),static_cast<uint8_t>(i>>8),static_cast<uint8_t>(i>>16),static_cast<uint8_t>(i)};
        const auto actual=mask_image(source,operation(kind=="meko-reverse"));
        const auto columns=width/16;
        for(size_t n=0;n<count;++n) {
            std::array<unsigned char,2> bytes{};CHECK(permutation.read(reinterpret_cast<char*>(bytes.data()),2));
            const auto origin=unsigned(bytes[0])|unsigned(bytes[1])<<8;
            for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x) {
                auto expected=pixels[(origin/columns*16+y)*width+origin%columns*16+x];
                expected.r^=255;expected.g^=255;expected.b^=255;
                CHECK(actual.frames[0].pixels[(n/columns*16+y)*width+n%columns*16+x]==expected);
            }
        }
        CHECK(permutation.peek()==std::char_traits<char>::eof());++checked;
    }
    CHECK(checked==8);
    auto oversized=new_image(1536,1536);
    for(const bool reverse:{false,true}) {
        bool bounded{};
        try { mask_image(oversized,operation(reverse)); }
        catch(const std::runtime_error &error) {bounded=std::string(error.what()).find("9215")!=std::string::npos;}
        CHECK(bounded);
    }
}
TEST(rainbow_27_native_outputs_13_alpha_palette_and_selection) {
    size_t checked{},alpha{};
    for(const auto &item:gvn_test::fl_references(L"rainbow-reference",27)) {
        const auto rect=item.selected?item.rect:Rect{};
        auto options=item.options;options.rainbow_size=options.glass_size;
        const auto actual=mask_image(item.source,Mask::Rainbow,options,rect);
        CHECK(actual.frames[0].pixels==item.expected.frames[0].pixels);
        gvn_test::fl_check_indexed(actual,item.expected);
        if(item.alpha) {
            auto source=item.source,expected=item.expected;
            for(size_t i=0;i<source.frames[0].pixels.size();++i) {
                source.frames[0].pixels[i].a=static_cast<uint8_t>(i);expected.frames[0].pixels[i].a=item.expected_alpha[i];
            }
            CHECK(mask_image(source,Mask::Rainbow,options,rect).frames[0].pixels==expected.frames[0].pixels);++alpha;
        }
        ++checked;
    }
    CHECK(checked==27&&alpha==13);
}
TEST(rainbow_current_frame_and_invalid_parameters_preserve_source) {
    const auto cases=gvn_test::fl_references(L"rainbow-reference",27);
    const auto it=std::find_if(cases.begin(),cases.end(),[](const auto &r){return r.alpha&&!r.selected&&r.source.width==53&&r.options.glass_size==8;});
    CHECK(it!=cases.end());auto source=it->source,expected=it->expected;
    for(size_t i=0;i<source.frames[0].pixels.size();++i)source.frames[0].pixels[i].a=expected.frames[0].pixels[i].a=static_cast<uint8_t>(i);
    source.frames.push_back(source.frames[0]);
    for(auto &pixel:source.frames[1].pixels){pixel.r^=0x51;pixel.g^=0x22;pixel.a=255-pixel.a;}
    for(auto &pixel:expected.frames[0].pixels){pixel.r^=0x51;pixel.g^=0x22;pixel.a=255-pixel.a;}
    const auto first=source.frames[0].pixels,second=source.frames[1].pixels;
    const auto actual=rainbow_image(source,8,{},1);
    CHECK(actual.frames.size()==1&&actual.frames[0].pixels==expected.frames[0].pixels);
    CHECK(source.frames[0].pixels==first&&source.frames[1].pixels==second);
    CHECK(rainbow_image(actual,8).frames[0].pixels==second);
    for(const int size:{-1,0,3,33,64})THROWS(rainbow_image(source,size));
    THROWS(rainbow_image(source,8,{},2));
    CHECK(rainbow_image(source,8,{1000,1000,8,8},1).frames[0].pixels==second);
}
TEST(cp_107_native_outputs_99_alpha_palette_selection_and_inverse) {
    size_t checked{},alpha{};
    for(const auto &reference:gvn_test::cp_references()) {
        const auto &item=reference.pixels;const auto rect=item.selected?item.rect:Rect{};
        const auto actual=cp_image(item.source,reference.code,rect);
        CHECK(actual.frames[0].pixels==item.expected.frames[0].pixels);
        gvn_test::fl_check_indexed(actual,item.expected);
        CHECK(cp_image(actual,reference.code,rect).frames[0].pixels==item.source.frames[0].pixels);
        if(item.alpha) {
            auto source=item.source,expected=item.expected;
            for(size_t i=0;i<source.frames[0].pixels.size();++i) {
                source.frames[0].pixels[i].a=static_cast<uint8_t>(i);
                expected.frames[0].pixels[i].a=item.expected_alpha[i];
            }
            const auto before=source.frames[0].pixels;
            const auto result=cp_image(source,reference.code,rect);
            CHECK(result.frames[0].pixels==expected.frames[0].pixels);
            CHECK(cp_image(result,reference.code,rect).frames[0].pixels==before);
            CHECK(source.frames[0].pixels==before);++alpha;
        }
        ++checked;
    }
    CHECK(checked==107&&alpha==99);
}
TEST(cp_native_input_filter_current_frame_and_invalid_geometry) {
    CHECK(normalize_cp_code(L"a b")==L"AB");CHECK(normalize_cp_code(L"ab1")==L"AB");
    CHECK(normalize_cp_code(L"あ").empty());CHECK(normalize_cp_code(L"abcdefghijklmnopq")==L"ABCDEFGHIJKLMNOP");
    const auto references=gvn_test::cp_references();
    const auto it=std::find_if(references.begin(),references.end(),[](const auto &r) {
        return r.code==L"a"&&r.pixels.source.width==65&&!r.pixels.selected&&!r.pixels.source.frames[0].indexed;
    });CHECK(it!=references.end());auto source=it->pixels.source,expected=it->pixels.expected;
    for(size_t i=0;i<source.frames[0].pixels.size();++i) {
        source.frames[0].pixels[i].a=static_cast<uint8_t>(i);
        expected.frames[0].pixels[i].a=it->pixels.expected_alpha[i];
    }
    source.frames.push_back(source.frames[0]);
    const auto invert=[](Pixel &p){p.r^=255;p.g^=255;p.b^=255;p.a^=255;};
    for(auto &p:source.frames[1].pixels)invert(p);
    for(auto &p:expected.frames[0].pixels)invert(p);
    const auto first=source.frames[0].pixels,second=source.frames[1].pixels;
    const auto actual=cp_image(source,L"a",{},1);
    CHECK(actual.frames.size()==1&&actual.frames[0].pixels==expected.frames[0].pixels);
    CHECK(cp_image(actual,L"A").frames[0].pixels==second);
    CHECK(source.frames[0].pixels==first&&source.frames[1].pixels==second);
    CHECK(cp_image(source,L"あ",{},1).frames[0].pixels==second);
    CHECK(cp_image(source,L"A",{1000,1000,8,8},1).frames[0].pixels==second);
    THROWS(cp_image(source,L"A",{},2));
    auto truncated=source;truncated.frames[0].pixels.pop_back();THROWS(cp_image(truncated,L"A"));
}
TEST(cp_wide_block_indices_and_circular_free_slot_sets_roundtrip) {
    auto source=new_image(2048,2048);auto &pixels=source.frames[0].pixels;
    for(size_t i=0;i<pixels.size();++i)pixels[i]={static_cast<uint8_t>(i),static_cast<uint8_t>(i>>8),static_cast<uint8_t>(i>>16),static_cast<uint8_t>(i*13)};
    const auto result=cp_image(source,L"ABCDEFGHIJKLMNOP");
    CHECK(result.frames[0].pixels!=pixels&&cp_image(result,L"abcdefghijklmnop").frames[0].pixels==pixels);
}
