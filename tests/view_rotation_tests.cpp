#include "gvn/loader.h"
#include "gvn/settings.h"
#include "folder_test_tree.h"
#include "test.h"
#include <array>
#include <fstream>
#include <future>
#include <sstream>
using namespace gvn;
namespace {
struct ReferenceCase {
    std::string group, index, first, second, expected;
    int rotation{}, before{}, height{}, rtl{}, single_wide{};
};
const std::vector<ReferenceCase> &references() {
    static const auto cases = [] {
        std::vector<ReferenceCase> result;
        std::ifstream stream(std::filesystem::path(GVN_FIXTURES) / L"view-rotation/cases.tsv");
        CHECK(stream);
        for (std::string line; std::getline(stream, line);) {
            std::istringstream input(line);
            std::array<std::string, 10> fields;
            for (auto &field : fields)
                CHECK(std::getline(input, field, '\t'));
            result.push_back({fields[0], fields[1], fields[2], fields[3], fields[4], std::stoi(fields[5]),
                              std::stoi(fields[6]), std::stoi(fields[7]), std::stoi(fields[8]),
                              std::stoi(fields[9])});
        }
        CHECK(result.size() == 792);
        return result;
    }();
    return cases;
}
void compare_reference(const ReferenceCase &reference, FolderTestTree &tree) {
    const auto root = std::filesystem::path(GVN_FIXTURES) / L"view-rotation";
    const auto ini = tree.root / (reference.group + reference.index + ".ini");
    {
        std::ofstream stream(ini);
        CHECK(stream);
        stream << "[Viewer]\nview_rotation=" << reference.rotation
               << "\ncomic_rotate_before=" << reference.before
               << "\ncomic_single_wide=" << reference.single_wide << "\ncomic_mode=" << reference.height
               << "\ncomic_down=0\ncomic_up=0\ncomic_fill=0\ncomic_rtl=" << reference.rtl << '\n';
    }
    Settings settings;
    settings.load(ini);
    std::promise<Loaded> promise;
    auto future = promise.get_future();
    Loader loader([&](Loaded result) { promise.set_value(std::move(result)); });
    loader.request(root / reference.first, false,
                   reference.second == "-" ? std::filesystem::path{} : root / reference.second,
                   settings.comic_rtl, settings.comic_options(), settings.loading_options());
    CHECK(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    const auto loaded = future.get();
    CHECK(loaded.image && loaded.error.empty());
    const auto expected = load_source(root / reference.expected);
    const auto &actual = *loaded.image;
    CHECK(expected.frames.size() == 1 && actual.frames.size() == 1);
    if (actual.width != expected.width || actual.height != expected.height ||
        actual.frames[0].pixels != expected.frames[0].pixels) {
        std::ostringstream message;
        message << reference.group << '/' << reference.index << " rotation=" << reference.rotation
                << " before=" << reference.before << " wide_single=" << reference.single_wide
                << " expected=" << expected.width << 'x' << expected.height << " actual=" << actual.width
                << 'x' << actual.height;
        throw std::runtime_error(message.str());
    }
}
template <class Predicate> void compare_all(Predicate accept, size_t expected) {
    FolderTestTree tree;
    size_t count{};
    for (const auto &reference : references())
        if (accept(reference)) {
            compare_reference(reference, tree);
            ++count;
        }
    CHECK(count == expected);
}
} // namespace
TEST(configured_single_rotation_matches_all_twenty_four_reference_copies) {
    compare_all([](const auto &c) { return c.group == "single"; }, 24);
}
TEST(configured_rotation_after_comic_matches_all_reference_shapes_and_directions) {
    compare_all([](const auto &c) { return c.group == "comic" && !c.before; }, 192);
}
TEST(configured_rotation_before_comic_matches_all_reference_shapes_and_directions) {
    compare_all([](const auto &c) { return c.group == "comic" && c.before; }, 192);
}
TEST(configured_wide_single_policy_uses_dimensions_at_the_selected_rotation_stage) {
    compare_all([](const auto &c) { return c.group == "comic-wide"; }, 384);
}
TEST(configured_square_comic_pages_remain_eligible_when_wide_single_is_enabled) {
    compare_all(
        [](const auto &c) {
            return c.group == "comic-wide" && c.rotation == 0 && c.first.find("SS-") != std::string::npos;
        },
        12);
}
int main() {
    return run_tests();
}

#include "gvn/view_rotation.h"
namespace {
Image rotation_sample() {
    Image image;
    image.width = 2;
    image.height = 3;
    image.format = L"GIF";
    image.path = L"original.gif";
    image.file_bytes = 1234;
    image.load_ms = 7;
    image.file_loops = 9;
    image.animation_compensate = false;
    const std::array<uint8_t, 6> alpha{0, 1, 64, 128, 254, 255};
    for (unsigned frame = 0; frame < 3; ++frame) {
        Frame next;
        next.duration_ms = 17 + 37 * frame;
        for (unsigned i = 0; i < 6; ++i)
            next.pixels.push_back({static_cast<uint8_t>(10 + i + frame * 20),
                                   static_cast<uint8_t>(30 + i * 7),
                                   static_cast<uint8_t>(50 + i * 11), alpha[i]});
        image.frames.push_back(std::move(next));
    }
    return image;
}
} // namespace
TEST(view_rotation_policy_excludes_squares_from_both_conditional_classes) {
    const std::array<std::array<int, 8>, 3> expected{{
        {0, 3, 1, 3, 1, 0, 0, 2},
        {0, 3, 1, 0, 0, 3, 1, 2},
        {0, 3, 1, 0, 0, 0, 0, 2}}};
    const std::array<std::pair<uint32_t, uint32_t>, 3> shapes{{{5, 7}, {7, 5}, {5, 5}}};
    for (size_t shape = 0; shape < shapes.size(); ++shape)
        for (int mode = 0; mode < 8; ++mode)
            CHECK(view_rotation_quarters(mode, shapes[shape].first, shapes[shape].second) ==
                  expected[shape][mode]);
    THROWS(view_rotation_quarters(-1, 5, 7));
    THROWS(view_rotation_quarters(8, 5, 7));
}
TEST(view_rotation_preserves_every_frame_alpha_hidden_rgb_duration_and_source_metadata) {
    const auto original = rotation_sample();
    for (const auto &[mode, order] : std::array<std::pair<int, std::array<size_t, 6>>, 3>{{
             {1, {1, 3, 5, 0, 2, 4}}, {2, {4, 2, 0, 5, 3, 1}}, {7, {5, 4, 3, 2, 1, 0}}}}) {
        auto image = original;
        apply_view_rotation(image, mode);
        CHECK(image.width == (mode == 7 ? 2 : 3) && image.height == (mode == 7 ? 3 : 2));
        CHECK(image.frames.size() == original.frames.size());
        CHECK(image.path == original.path && image.format == original.format &&
              image.file_bytes == original.file_bytes && image.load_ms == original.load_ms &&
              image.file_loops == original.file_loops && !image.animation_compensate);
        for (size_t frame = 0; frame < image.frames.size(); ++frame) {
            CHECK(image.frames[frame].duration_ms == original.frames[frame].duration_ms);
            for (size_t pixel = 0; pixel < order.size(); ++pixel)
                CHECK(image.frames[frame].pixels[pixel] == original.frames[frame].pixels[order[pixel]]);
        }
    }
}
TEST(view_rotation_four_quarter_turns_restore_all_animation_frames_exactly) {
    const auto original = rotation_sample();
    auto image = original;
    for (unsigned i = 0; i < 4; ++i)
        apply_view_rotation(image, 1);
    CHECK(image.width == original.width && image.height == original.height);
    for (size_t frame = 0; frame < image.frames.size(); ++frame)
        CHECK(image.frames[frame].pixels == original.frames[frame].pixels);
}
TEST(view_rotation_no_op_retains_owned_pixel_and_palette_buffers) {
    auto image = rotation_sample();
    std::vector<const Pixel *> pointers;
    for (const auto &frame : image.frames)
        pointers.push_back(frame.pixels.data());
    apply_view_rotation(image, 0);
    apply_view_rotation(image, 5); // Portrait does not meet the landscape condition.
    for (size_t frame = 0; frame < image.frames.size(); ++frame)
        CHECK(image.frames[frame].pixels.data() == pointers[frame]);
}
TEST(view_rotation_bad_frames_and_invalid_options_fail_before_changing_pixels) {
    auto image = rotation_sample();
    image.frames.back().pixels.pop_back();
    const auto original = image;
    THROWS(apply_view_rotation(image, 1));
    THROWS(apply_view_rotation(image, -1));
    THROWS(apply_view_rotation(image, 8));
    CHECK(image.width == original.width && image.height == original.height);
    for (size_t frame = 0; frame < image.frames.size(); ++frame)
        CHECK(image.frames[frame].pixels == original.frames[frame].pixels);
    Image empty;
    THROWS(apply_view_rotation(empty, 1));
}
TEST(view_rotation_preserves_packed_palette_numbers_and_does_not_mutate_shared_sources) {
    for (unsigned bits : {1, 2, 4, 8}) {
        Image source;
        source.width = 5;
        source.height = 3;
        auto indexed = std::make_shared<IndexedPixels>();
        indexed->width = source.width;
        indexed->height = source.height;
        indexed->bits = bits;
        for (unsigned i = 0; i < (1u << bits); ++i)
            indexed->palette.push_back({static_cast<uint8_t>(i), static_cast<uint8_t>(i * 37),
                                        static_cast<uint8_t>(i * 13), static_cast<uint8_t>(i * 17)});
        indexed->packed.resize(indexed->stride() * source.height);
        Frame frame;
        for (uint32_t y = 0; y < source.height; ++y)
            for (uint32_t x = 0; x < source.width; ++x) {
                const auto value = static_cast<uint8_t>((x + y * 3) % indexed->palette.size());
                indexed->set_index(x, y, value);
                frame.pixels.push_back(indexed->palette[value]);
            }
        frame.indexed = indexed;
        source.frames.push_back(frame);
        CHECK(indexed->valid() && indexed->matches(frame.pixels));
        const auto packed = indexed->packed;
        for (int mode : {1, 2, 7}) {
            auto image = source;
            apply_view_rotation(image, mode);
            const auto &transformed = image.frames[0].indexed;
            CHECK(transformed && transformed != indexed && transformed->valid());
            CHECK(transformed->width == image.width && transformed->height == image.height);
            CHECK(transformed->bits == bits && transformed->palette == indexed->palette);
            CHECK(transformed->matches(image.frames[0].pixels));
            CHECK(indexed->packed == packed && indexed->matches(source.frames[0].pixels));
        }
    }
}
TEST(view_rotation_discards_stale_palette_without_losing_valid_rgba) {
    auto image = rotation_sample();
    auto invalid = std::make_shared<IndexedPixels>();
    invalid->width = 2;
    invalid->height = 3;
    invalid->bits = 8;
    invalid->palette = {{255, 255, 255, 255}};
    invalid->packed.resize(6);
    image.frames[0].indexed = invalid;
    const auto original = image;
    apply_view_rotation(image, 7);
    CHECK(!image.frames[0].indexed && original.frames[0].indexed == invalid);
    for (size_t i = 0; i < 6; ++i)
        CHECK(image.frames[0].pixels[i] == original.frames[0].pixels[5 - i]);
}
TEST(view_rotation_supports_maximum_thin_axis_without_changing_pixel_count) {
    Image image;
    image.width = 65535;
    image.height = 1;
    image.frames.resize(1);
    for (uint32_t i = 0; i < image.width; ++i)
        image.frames[0].pixels.push_back({static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8), 37,
                                          static_cast<uint8_t>(i % 256)});
    const auto original = image.frames[0].pixels;
    apply_view_rotation(image, 2);
    CHECK(image.width == 1 && image.height == 65535 && image.frames[0].pixels == original);
    apply_view_rotation(image, 1);
    CHECK(image.width == 65535 && image.height == 1 && image.frames[0].pixels == original);
}

namespace {
void compare_exif_rotation_references(bool enabled) {
    FolderTestTree tree;
    const auto root = std::filesystem::path(GVN_FIXTURES) / L"view-rotation-exif";
    std::ifstream cases(root / L"cases.tsv");
    CHECK(cases);
    size_t compared{};
    for (std::string line; std::getline(cases, line);) {
        std::istringstream row(line);
        std::array<std::string, 5> columns;
        for (auto &column : columns)
            CHECK(std::getline(row, column, '\t'));
        if ((std::stoi(columns[0]) != 0) != enabled)
            continue;
        const auto ini = tree.root / L"exif-view.ini";
        {
            std::ofstream stream(ini);
            stream << "[Viewer]\nexif_orientation=" << enabled << "\nview_rotation=" << columns[2] << '\n';
        }
        Settings settings;
        settings.load(ini);
        CHECK(settings.exif_orientation == enabled && settings.view_rotation == std::stoi(columns[2]));
        std::promise<Loaded> result;
        auto future = result.get_future();
        Loader loader([&](Loaded loaded) { result.set_value(std::move(loaded)); }, 0);
        loader.configure(0, false);
        loader.request(root / columns[3], false, {}, false, ComicOptions{}, settings.loading_options());
        CHECK(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
        const auto loaded = future.get();
        CHECK(loaded.image && loaded.error.empty());
        const auto expected = load_source(root / columns[4]);
        const auto &actual = *loaded.image;
        if (actual.width != expected.width || actual.height != expected.height ||
            actual.frames[0].pixels != expected.frames[0].pixels)
            throw std::runtime_error("Exif/view reference mismatch: enabled=" + columns[0] +
                                     " tag=" + columns[1] + " mode=" + columns[2]);
        ++compared;
    }
    CHECK(compared == 64);
}
} // namespace
TEST(exif_disabled_view_rotation_matches_all_sixty_four_reference_copies) {
    compare_exif_rotation_references(false);
}
TEST(exif_enabled_runs_before_view_rotation_for_all_eight_tags_and_choices) {
    compare_exif_rotation_references(true);
}


#include "rotation_formats_fixture.h"
namespace {
void compare_rotation_formats(const std::string &kind, size_t expected_count) {
    FolderTestTree tree;
    size_t count{};
    for (const auto &item : gvn_test::rotation_format_cases()) {
        if (item.kind != kind)
            continue;
        const auto source = gvn_test::rotation_formats_root / item.source;
        const auto bytes = read_file(source);
        const auto decoded = load_source(source);
        const auto ini = tree.root / L"formats.ini";
        {
            std::ofstream stream(ini);
            stream << "[Viewer]\nview_rotation=" << item.mode << "\npreserve_alpha=1\n";
        }
        Settings settings;
        settings.load(ini);
        CHECK(settings.view_rotation == item.mode && settings.preserve_alpha);
        std::promise<Loaded> promise;
        auto future = promise.get_future();
        Loader loader([&](Loaded loaded) { promise.set_value(std::move(loaded)); }, 0);
        loader.configure(0, false);
        loader.request(source, false, {}, false, ComicOptions{}, settings.loading_options());
        CHECK(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
        const auto result = future.get();
        CHECK(result.image && result.error.empty());
        gvn_test::check_rotation_format(*result.image, decoded, item, source);
        CHECK(read_file(source) == bytes);
        ++count;
    }
    CHECK(count == expected_count);
}
}
TEST(rotation_partial_alpha_png_matches_all_reference_shapes_and_modes) {
    compare_rotation_formats("png-alpha", 24);
}
TEST(rotation_opaque_and_transparent_gif_preserve_alpha_palette_and_indices) {
    compare_rotation_formats("gif", 24);
    compare_rotation_formats("gif-transparent", 24);
}
TEST(rotation_two_frame_gif_preserves_each_rgba_frame_and_animation_metadata) {
    compare_rotation_formats("gif-animated", 24);
    compare_rotation_formats("gif-animated-transparent", 24);
}
