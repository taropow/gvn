#include "gvn/core.h"
#include "test.h"
#define NOMINMAX
#include <windows.h>
#include <fstream>
#include <cmath>
using namespace gvn;
TEST(dimensions_and_overflow) {
    CHECK(checked_pixels(3, 2, 2) == 12);
    THROWS(checked_pixels(0, 2));
    THROWS(checked_pixels(1, 0));
    THROWS(checked_pixels(1, 1, 0));
    THROWS(checked_pixels(UINT64_MAX, 2));
    THROWS(checked_pixels(65535, 65535));
    THROWS(checked_pixels(4, 4, 10, 639));
    CHECK(checked_pixels(4, 4, 10, 640) == 160);
}
TEST(alpha_composition) {
    CHECK(over({255, 0, 0, 128}, {0, 0, 255, 255}) == Pixel{128, 0, 127, 255});
    CHECK(over({1, 2, 3, 0}, {4, 5, 6, 128}) == Pixel{4, 5, 6, 128});
    CHECK(over({0, 0, 0, 0}, {255, 255, 255, 0}) == Pixel{});
    CHECK(over({10, 20, 30, 255}, {4, 5, 6, 255}) == Pixel{10, 20, 30, 255});
}
TEST(view_fit_and_zoom_anchor) {
    View v;
    v.resize(800, 600);
    v.set_image(1600, 400);
    CHECK(v.scale == 0.5);
    CHECK(v.top() == 200);
    const double x = 210, y = 150, ix = (x - v.left()) / v.scale, iy = (y - v.top()) / v.scale;
    v.zoom_at(2, x, y);
    CHECK(std::abs((x - v.left()) / v.scale - ix) < 0.001);
    CHECK(std::abs((y - v.top()) / v.scale - iy) < 0.001);
    v.apply_fit(Fit::Height);
    CHECK(v.scale == 1.5);
    v.apply_fit(Fit::Actual);
    CHECK(v.scale == 1);
    v.zoom_at(100000, 0, 0);
    CHECK(v.scale == 50);
    v.zoom_at(0, 0, 0);
    CHECK(v.scale == 50);
}
TEST(view_alignment_fit_limit_and_stepped_zoom) {
    View v;
    v.resize(800, 600);
    v.set_image(100, 50);
    v.fit_limit = 1;
    v.apply_fit(Fit::Contain);
    CHECK(v.scale == 1 && v.left() == 350 && v.top() == 275);
    v.centered = false;
    CHECK(v.left() == 0 && v.top() == 0);
    v.zoom_at(2, 75, 25);
    CHECK(v.scale == 2 && v.left() == -75 && v.top() == -25);
    CHECK((75 - v.left()) / v.scale == 75);
    v.apply_fit(Fit::Actual);
    CHECK(v.scale == 1 && !v.manual);
    v.fit_limit = .1;
    v.apply_fit(Fit::Actual);
    CHECK(v.scale == 1);
    v.set_image(1600, 1200);
    v.apply_fit(Fit::Contain);
    CHECK(v.scale == .1);
    v.zoom_at(10, 0, 0);
    CHECK(v.scale == 1 && v.x_range() == std::pair{-800.0, 0.0});
    CHECK(v.y_range() == std::pair{-600.0, 0.0});
    CHECK(stepped_zoom(.95, true, 1, 25, 10, 25) == 1);
    CHECK(stepped_zoom(1, true, 1, 25, 10, 25) == 1.25);
    CHECK(stepped_zoom(1, false, 1, 25, 10, 25) == .9);
    CHECK(stepped_zoom(1.1, false, 1, 25, 10, 25) == 1);
    CHECK(stepped_zoom(1.3, true, 2, 25, 10, 25) == 1.5);
    CHECK(stepped_zoom(1.3, false, 2, 25, 10, 25) == 1.25);
    // Legacy relative mode retains sub-1%; reference modes clamp command results to 1%.
    CHECK(stepped_zoom(.00001, false, 0, 25, 10, 25) == .00001);
    CHECK(stepped_zoom(50, true, 2, 25, 10, 25) == 50);
}
TEST(window_placement_negative_monitors_and_small_images) {
    const Rect work{-1920, 40, 1920, 1040};
    auto p = place_window({-3000, -1000, 800, 600}, work, false);
    CHECK(p.x == -1920 && p.y == 40 && p.w == 800 && p.h == 600);
    p = place_window({5000, 5000, 800, 600}, work, false);
    CHECK(p.x == -800 && p.y == 480);
    p = place_window({-1700, 80, 800, 600}, work, true);
    CHECK(p.x == -1360 && p.y == 260);
    p = place_window({0, 0, 8000, 6000}, work, true);
    CHECK(p.x == -1920 && p.y == 40 && p.w == 1920 && p.h == 1040);
    THROWS(place_window({0, 0, 0, 5}, work, false));
    View v;
    v.resize(800, 600);
    v.set_image(100, 50);
    v.apply_fit(Fit::Contain, true);
    CHECK(v.scale == 1 && v.shrink_only);
    v.resize(1600, 900);
    CHECK(v.scale == 1);
    v.apply_fit(Fit::Contain);
    CHECK(v.scale == 16 && !v.shrink_only);
}
TEST(canvas_scrollbars_resolve_fit_and_coupled_axes) {
    View v;
    v.resize(800, 600);
    v.set_image(795, 1000);
    v.apply_fit(Fit::Actual);
    auto bars = arrange_scrollbars(v, 800, 600, 17, true);
    CHECK(bars.horizontal && bars.vertical && v.width == 783 && v.height == 583);
    v.apply_fit(Fit::Contain);
    bars = arrange_scrollbars(v, 800, 600, 17, true);
    CHECK(!bars.horizontal && !bars.vertical && v.width == 800 && v.height == 600);
    v.apply_fit(Fit::Width);
    bars = arrange_scrollbars(v, 800, 600, 17, true);
    CHECK(!bars.horizontal && bars.vertical && v.width == 783 && v.height == 600);
    CHECK(std::abs(v.canvas_w * v.scale - 783) < .001);
    v.apply_fit(Fit::Actual);
    v.zoom_at(50, 400, 300);
    v.pan_x = 1e6;
    v.pan_y = -1e6;
    bars = arrange_scrollbars(v, 800, 600, 17, true);
    CHECK(v.pan_x == v.x_range().second && v.pan_y == v.y_range().first);
    CHECK(v.left() == 0 && v.top() == v.height - v.canvas_h * v.scale);
    bars = arrange_scrollbars(v, 800, 600, 17, false);
    CHECK(!bars.horizontal && !bars.vertical && v.width == 800 && v.height == 600);
    v.centered = false;
    v.pan_x = 1e6;
    v.pan_y = -1e6;
    arrange_scrollbars(v, 800, 600, 25, true);
    CHECK(v.left() == 0 && v.top() == v.height - v.canvas_h * v.scale);
    v.apply_fit(Fit::Contain);
    bars = arrange_scrollbars(v, 1, 1, 25, true);
    CHECK(v.width == 1 && v.height == 1 && !bars.horizontal && !bars.vertical);
}
TEST(caption_overlay_nine_positions_clip_and_tiny_viewport) {
    const Rect area{0, 30, 800, 600};
    for (int position = 0; position < 9; ++position) {
        const auto box = place_overlay(area, 200, 40, position, 4);
        CHECK(box.w == 200 && box.h == 40 && box.x >= 4 && box.x + box.w <= 796);
        CHECK(box.y >= 34 && box.y + box.h <= 626);
        CHECK(box.x == (position % 3 == 0 ? 4 : position % 3 == 1 ? 300 : 596));
        CHECK(box.y == (position / 3 == 0 ? 34 : position / 3 == 1 ? 310 : 586));
    }
    const auto large = place_overlay(area, 100000, 100000, 8, 4);
    CHECK(large.x == 4 && large.y == 34 && large.w == 792 && large.h == 592);
    const auto tiny = place_overlay({5, 7, 1, 1}, 200, 80, 8, 50);
    CHECK(tiny.x == 5 && tiny.y == 7 && tiny.w == 1 && tiny.h == 1);
    CHECK(place_overlay({}, 100, 20, 0, 4).empty());
}
TEST(player_timing_seek_and_stop) {
    Image image;
    image.frames = {{{}, 40}, {{}, 100}, {{}, 60}};
    Player p;
    p.reset(image);
    CHECK(!p.advance(39));
    CHECK(p.frame == 0);
    CHECK(p.advance(1));
    CHECK(p.frame == 1);
    CHECK(p.advance(100));
    CHECK(p.frame == 2);
    CHECK(p.advance(60));
    CHECK(p.frame == 0);
    p.loop = false;
    p.seek(2);
    p.playing = true;
    p.advance(60);
    CHECK(!p.playing);
    CHECK(p.frame == 2);
    p.step(1);
    CHECK(p.frame == 0);
    p.step(-1);
    CHECK(p.frame == 2);
    p.seek(100);
    CHECK(p.frame == 2);
    p.loop = true;
    p.playing = true;
    p.advance(UINT32_MAX);
    CHECK(p.frame < 3);
}
TEST(rectangles_and_extensions) {
    const auto r = intersect({-2, 1, 8, 8}, {0, 0, 4, 4});
    CHECK(r.x == 0 && r.y == 1 && r.w == 4 && r.h == 3);
    CHECK(intersect({10, 0, 1, 1}, {0, 0, 2, 2}).empty());
    CHECK(supported(L"IMAGE.PSD"));
    CHECK(supported(L"animation.APnG"));
    CHECK(!supported(L"file.txt"));
}
TEST(file_sorting) {
    std::vector<FileEntry> files = {{{L"10.png"}, 100, 3}, {{L"2.png"}, 20, 2}, {{L"1.jpg"}, 10, 1}};
    sort_files(files, Sort::Name, false, true);
    CHECK(files[0].path == L"1.jpg");
    CHECK(files[1].path == L"2.png");
    sort_files(files, Sort::Name, false, false);
    CHECK(files[1].path == L"10.png");
    sort_files(files, Sort::Size, true, true);
    CHECK(files[0].bytes == 100);
    sort_files(files, Sort::Time, false, true);
    CHECK(files[0].time == 1);
}
TEST(file_visibility_attributes_are_independent) {
    const auto directory = std::filesystem::temp_directory_path() /
                           (L"GVN-visibility-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                            std::to_wstring(GetTickCount64()));
    CHECK(std::filesystem::create_directory(directory));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{directory};
    const DWORD attributes[]{FILE_ATTRIBUTE_NORMAL, FILE_ATTRIBUTE_HIDDEN, FILE_ATTRIBUTE_SYSTEM,
                             FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM};
    for (size_t i = 0; i < 4; ++i) {
        const auto path = directory / (std::to_wstring(i) + L".png");
        {
            std::ofstream output(path);
            output << "fixture";
        }
        CHECK(SetFileAttributesW(path.c_str(), attributes[i]));
    }
    for (unsigned mask = 0; mask < 8; ++mask) {
        const auto entries =
            list_files(directory, FileVisibility{bool(mask & 1), bool(mask & 2), bool(mask & 4)});
        CHECK(entries.size() == 1 + bool(mask & 1) + bool(mask & 2) + bool(mask & 4));
        for (const auto &entry : entries) {
            const int value = std::stoi(entry.path.stem().wstring());
            CHECK(value == 0 || (mask & (1u << (value - 1))));
        }
    }
    CHECK(list_files(directory, true).size() == 4 && list_files(directory, false).size() == 1);
}
int main() {
    return run_tests();
}
TEST(selection_integer_limits) {
    CHECK(intersect({INT_MAX - 2, 0, 20, 1}, {0, 0, 100, 100}).empty());
    CHECK(intersect({-10, -10, 20, 20}, {0, 0, 5, 5}).w == 5);
}
TEST(keep_zoom_also_locks_fit_scale) {
    View view;
    view.resize(800, 600);
    view.set_image(1600, 400);
    CHECK(view.scale == .5 && !view.manual);
    view.set_image(100, 200, true);
    CHECK(view.scale == .5 && view.manual);
    view.resize(400, 300);
    CHECK(view.scale == .5);
    view.set_image(100, 200, false);
    CHECK(view.scale == 1.5 && !view.manual);
}

#include "zoom_reference.h"
TEST(absolute_zoom_keeps_decimal_boundary_and_pointer_anchor) {
    View view;
    view.resize(801, 601); view.set_image(1600, 1200);
    view.zoom_to(.36, 123, 78);
    const auto x = (123 - view.left()) / view.scale, y = (78 - view.top()) / view.scale;
    view.zoom_to(.245, 123, 78);
    CHECK(view.scale == .245 && view.manual);
    CHECK(std::abs((123 - view.left()) / view.scale - x) < 1e-9);
    CHECK(std::abs((78 - view.top()) / view.scale - y) < 1e-9);
    CHECK(stepped_zoom(view.scale, true, 1, 25, 3, 17) == .28);
    view.zoom_to(0, 0, 0); CHECK(view.scale == .245);
    view.zoom_to(100, 0, 0); CHECK(view.scale == 50);
    view.zoom_to(.000001, 0, 0); CHECK(view.scale == .00001);
}
TEST(zoom_steps_match_all_official_native_reference_transitions) {
    size_t mismatches{};
    for(const auto &item:gvn_test::zoom_references()) {
        const auto actual=stepped_zoom(item.before,item.enlarge,item.mode,25,item.below,item.above);
        if(std::abs(actual-item.after)>1e-9) {
            if(mismatches<12) std::cout<<"ZOOM_REFERENCE_MISMATCH "<<item.label<<" actual="<<actual*100<<" expected="<<item.after*100<<'\n';
            ++mismatches;
        }
    }
    std::cout<<"ZOOM_REFERENCE cases=474 mismatches="<<mismatches<<'\n';
    CHECK(mismatches==0);
}

TEST(zoom_actual_stop_in_all_step_modes_and_continuous_drag) {
    for(const auto mode:{0,1,2}){
        CHECK(stepped_zoom(.95,true,mode,25,10,25)==1);
        CHECK(stepped_zoom(1.05,false,mode,25,10,25)==1);
        CHECK(stepped_zoom(1,true,mode,25,10,25)>1);
        CHECK(stepped_zoom(1,false,mode,25,10,25)<1);
    }
    CHECK(stepped_zoom(.8,true,0,25,10,25)==1);
    CHECK(stepped_zoom(1.25,false,0,25,10,25)==1);
    CHECK(stepped_zoom(.5,true,0,25,10,25)==.625);
    CHECK(stepped_zoom(2,false,0,25,10,25)==1.6);
    for(const auto before:{.9,1.1}){
        View view;view.resize(800,600);view.set_image(1600,1200);view.zoom_to(before,210,150);
        const auto x=(210-view.left())/view.scale,y=(150-view.top())/view.scale;
        const auto factor=before<1?2.0:.5;
        view.zoom_at(factor,210,150);CHECK(view.scale==1&&view.manual);
        CHECK(std::abs((210-view.left())/view.scale-x)<1e-9&&std::abs((150-view.top())/view.scale-y)<1e-9);
        view.zoom_at(factor,210,150);CHECK(view.scale==factor);
    }
    View direct;direct.zoom_to(1.1,0,0);CHECK(direct.scale==1.1);
    direct.zoom_to(.9,0,0);CHECK(direct.scale==.9); // Explicit numeric zoom remains exact.
}
