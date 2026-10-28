#include "gvn/loader.h"
#include "gvn/archive.h"
#include "gvn/edit.h"
#include "test.h"
#include "folder_test_tree.h"
#include <array>
#include <future>
using namespace gvn;
TEST(open_preparation_orders_metadata_respects_visibility_and_keeps_explicit_hidden_file) {
    FolderTestTree tree;
    const auto two = tree.file(L"pages/2.png"), ten = tree.file(L"pages/10.png");
    const auto hidden = tree.file(L"pages/hidden.png", FILE_ATTRIBUTE_HIDDEN);
    tree.file(L"pages/ignored.txt");
    OpenQuery query{two.parent_path()};
    auto result = prepare_open(query);
    CHECK(!result.cancelled && result.files.size() == 2 && result.path == two && result.index == 0);
    CHECK(result.folder == two.parent_path() && result.archive.empty() && result.entries.empty());
    CHECK(result.files[0].bytes == std::filesystem::file_size(two));
    CHECK(result.files[0].time == std::filesystem::last_write_time(two).time_since_epoch().count());
    query.last = true;
    CHECK(prepare_open(query).path == ten);
    query.last = false;
    query.reverse = true;
    CHECK(prepare_open(query).path == ten);
    query.path = hidden;
    result = prepare_open(query);
    CHECK(result.path == hidden && result.files.size() == 2 && result.index == 0);
    query.visibility.hidden = true;
    result = prepare_open(query);
    CHECK(result.files.size() == 3 && result.files[result.index].path == hidden);
    query.path = tree.directory(L"empty.zip");
    result = prepare_open(query);
    CHECK(result.path.empty() && result.folder == query.path && result.archive.empty());
}
TEST(open_preparation_matches_all_sort_and_visibility_options_and_cancels_collection) {
    FolderTestTree tree;
    const auto a = tree.file(L"pages/2.png"), b = tree.file(L"pages/10.bmp");
    tree.file(L"pages/hidden.gif", FILE_ATTRIBUTE_HIDDEN);
    tree.file(L"pages/system.apng", FILE_ATTRIBUTE_SYSTEM);
    tree.file(L"pages/protected.psd", FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    OpenQuery query{a.parent_path()};
    query.seed = 273;
    for (unsigned visibility = 0; visibility < 8; ++visibility) {
        query.visibility = {bool(visibility & 1), bool(visibility & 2), bool(visibility & 4)};
        for (unsigned sort = 0; sort < 5; ++sort)
            for (bool reverse : {false, true})
                for (bool natural : {false, true}) {
                    query.sort = static_cast<Sort>(sort);
                    query.reverse = reverse;
                    query.natural = natural;
                    auto expected = list_files(query.path, query.visibility);
                    sort_files(expected, query.sort, reverse, natural, query.seed);
                    const auto actual = prepare_open(query);
                    CHECK(!actual.cancelled && actual.files.size() == expected.size());
                    for (size_t i = 0; i < expected.size(); ++i)
                        CHECK(actual.files[i].path == expected[i].path &&
                              actual.files[i].bytes == expected[i].bytes &&
                              actual.files[i].time == expected[i].time);
                }
    }
    size_t checks{};
    const auto cancelled = prepare_open(query, [&] { return ++checks >= 5; });
    CHECK(cancelled.cancelled && cancelled.path.empty() && cancelled.files.empty() && checks >= 5);
    (void)b;
}
TEST(open_preparation_zip_member_order_and_cancellation_do_not_decode) {
    OpenQuery query{std::filesystem::path(GVN_FIXTURES) / L"images.zip"};
    auto result = prepare_open(query);
    CHECK(result.files.size() == 3 && result.entries.size() == 3 && !result.archive.empty());
    CHECK(result.path == result.files.front().path && result.index == 0);
    query.path = result.files.back().path;
    result = prepare_open(query);
    CHECK(result.files[result.index].path == query.path && !result.files[result.index].display_name.empty());
    query.path = std::filesystem::path(GVN_FIXTURES) / L"images.zip";
    size_t checks{};
    result = prepare_open(query, [&] { return ++checks >= 4; });
    CHECK(result.cancelled && result.path.empty() && result.files.empty() && result.entries.empty());
    Archive zip(read_file(query.path));
    CHECK(zip.entries([] { return true; }).empty());
    query.path += L"|999999";
    THROWS(prepare_open(query));
}
TEST(startup_missing_fallback_is_opt_in_and_uses_current_order_and_visibility) {
    FolderTestTree tree;
    const auto a = tree.file(L"saved/2.png"), b = tree.file(L"saved/10.bmp");
    tree.file(L"saved/0.png", FILE_ATTRIBUTE_HIDDEN);
    OpenQuery query{a.parent_path() / L"gone.png"};
    CHECK(prepare_open(query).path == query.path);
    query.missing_fallback = true;
    CHECK(prepare_open(query).path == a);
    query.reverse = true;
    CHECK(prepare_open(query).path == b);
    query.reverse = false;
    query.visibility.hidden = true;
    CHECK(prepare_open(query).path.filename() == L"0.png");
    query.path = tree.directory(L"empty") / L"gone.png";
    CHECK(prepare_open(query).path.empty());
    query.path = tree.root / L"missing-folder/gone.png";
    CHECK(prepare_open(query).path.empty());
    query.path = a;
    CHECK(prepare_open(query).path == a);
}
TEST(startup_zip_uses_persisted_entry_name_and_falls_back_when_entry_or_archive_disappears) {
    FolderTestTree tree;
    const auto archive = tree.root / L"book.zip";
    std::filesystem::copy_file(std::filesystem::path(GVN_FIXTURES) / L"images.zip", archive);
    OpenQuery query{archive};
    const auto entries = prepare_open(query).files;
    CHECK(entries.size() == 3);
    query.path = entries.front().path;
    query.missing_fallback = true;
    query.archive_entry = entries.back().display_name;
    CHECK(prepare_open(query).path == entries.back().path);
    query.archive_entry = L"removed-entry.png";
    CHECK(prepare_open(query).path == entries.front().path);
    query.archive_entry.clear();
    query.path = entries.back().path;
    CHECK(prepare_open(query).path == entries.back().path);
    query.path = archive.wstring() + L"|999999";
    CHECK(prepare_open(query).path == entries.front().path);
    const auto neighbor = tree.file(L"01.png");
    std::filesystem::remove(archive);
    CHECK(prepare_open(query).path == neighbor);
}
TEST(loader_open_preparation_uses_image_worker_and_discards_cancelled_snapshot) {
    FolderTestTree tree;
    const auto path = tree.file(L"pages/1.png");
    std::promise<void> entered, release;
    auto started = entered.get_future();
    auto gate = release.get_future().share();
    std::promise<Loaded> promise;
    auto future = promise.get_future();
    std::atomic<size_t> callbacks{}, decoded{};
    std::thread::id listing_thread{}, decode_thread{};
    Loader loader(
        [&](Loaded value) {
            ++callbacks;
            promise.set_value(std::move(value));
        },
        0,
        [&](const auto &) {
            ++decoded;
            decode_thread = std::this_thread::get_id();
            Image image;
            image.width = image.height = 1;
            image.frames.push_back({{{1, 2, 3, 127}}, 100});
            return image;
        });
    struct Release {
        std::promise<void> &release;
        bool done{};
        ~Release() {
            if (!done)
                release.set_value();
        }
    } cleanup{release};
    const auto old = loader.request_open(OpenQuery{path.parent_path()},
                                         [&](const auto &folder, auto visibility, const auto &) {
                                             listing_thread = std::this_thread::get_id();
                                             entered.set_value();
                                             gate.wait();
                                             return list_files(folder, visibility);
                                         });
    CHECK(started.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    CHECK(listing_thread != std::this_thread::get_id() && decoded == 0 && !loader.stats().idle);
    const auto current = loader.request(path);
    release.set_value();
    cleanup.done = true;
    CHECK(future.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    const auto result = future.get();
    CHECK(loader.wait_idle(std::chrono::seconds(3)));
    CHECK(result.sequence == current && current > old && result.image && !result.open_match);
    CHECK(callbacks == 1 && decoded == 1 && listing_thread == decode_thread);
}
TEST(loader_open_preparation_returns_snapshot_without_decoding_or_filling_cache) {
    const auto path = std::filesystem::path(GVN_FIXTURES) / L"alpha.png";
    std::promise<Loaded> promise;
    auto future = promise.get_future();
    std::atomic<size_t> decoded{};
    Loader loader([&](Loaded value) { promise.set_value(std::move(value)); }, 128,
                  [&](const auto &) -> Image {
                      ++decoded;
                      throw std::runtime_error("Unexpected decode");
                  });
    const auto sequence = loader.request_open(OpenQuery{path});
    CHECK(future.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    const auto result = future.get();
    CHECK(loader.wait_idle(std::chrono::seconds(3)));
    CHECK(result.sequence == sequence && result.open_match && result.path == path && !result.image &&
          result.error.empty());
    CHECK(result.open_match->files[result.open_match->index].path == path);
    CHECK(decoded == 0 && loader.stats().cache_bytes == 0 && loader.stats().foreground_loads == 0);
}
TEST(cache_protects_foreground_and_first_prefetch_within_one_budget) {
    const auto make = [](size_t pixels) {
        auto image = std::make_shared<Image>();
        image->width = static_cast<uint32_t>(pixels);
        image->height = 1;
        image->frames.push_back({std::vector<Pixel>(pixels), 100});
        return image;
    };
    ImageCache cache(128);
    const auto a = make(16), b = make(16), c = make(16);
    cache.activate(L"a", a);
    CHECK(cache.prefetch_limit() == 64);
    CHECK(cache.put_prefetch(L"b", b));
    CHECK(cache.prefetch_limit() == 0 && !cache.put_prefetch(L"c", c));
    CHECK(cache.get(L"a") == a && cache.get(L"b") == b && !cache.get(L"c"));
    cache.begin_prefetch();
    CHECK(cache.put_prefetch(L"c", c));
    CHECK(cache.get(L"a") == a && !cache.get(L"b") && cache.get(L"c") == c);
    cache.activate(L"large", make(48));
    CHECK(cache.bytes() == 0 && cache.foreground_bytes() == 192 && cache.prefetch_limit() == 0);
    cache.set_limit(256);
    CHECK(cache.prefetch_limit() == 64 && cache.put_prefetch(L"b", b));
    CHECK(cache.bytes() == 64 && cache.prefetch_limit() == 0);
    cache.clear();
    CHECK(cache.bytes() == 0 && cache.foreground_bytes() == 0 && cache.prefetch_limit() == 256);
    cache.begin_prefetch(192); // A saved edited/clipboard image lives outside this cache.
    CHECK(cache.prefetch_limit() == 64 && cache.put_prefetch(L"b", b));
    CHECK(cache.bytes() == 64 && cache.foreground_bytes() == 192);
    cache.set_limit(128);
    CHECK(cache.bytes() == 0 && cache.prefetch_limit() == 0);
    cache.clear();
    CHECK(cache.foreground_bytes() == 0);
}
TEST(loader_limits_prefetch_without_evicting_display_and_reconfigures_without_reloading_it) {
    std::array<std::promise<Loaded>, 4> promises;
    std::vector<std::future<Loaded>> futures;
    for (auto &p : promises)
        futures.push_back(p.get_future());
    std::atomic<size_t> callbacks{}, decoded{};
    Loader loader([&](Loaded value) { promises.at(callbacks++).set_value(std::move(value)); }, 128,
                  [&](const auto &) {
                      ++decoded;
                      Image image;
                      image.width = image.height = 4;
                      image.frames.push_back({std::vector<Pixel>(16, {1, 2, 3, 127}), 100});
                      return image;
                  });
    const auto root = std::filesystem::path(GVN_FIXTURES), a = root / L"alpha.png", b = root / L"rgb.bmp",
               c = root / L"disposal.gif";
    const auto receive = [&](size_t index) {
        CHECK(futures[index].wait_for(std::chrono::seconds(3)) == std::future_status::ready);
        auto value = futures[index].get();
        CHECK(value.image && value.error.empty());
        CHECK(loader.wait_idle(std::chrono::seconds(3)));
        return value.image;
    };
    loader.request(a);
    const auto original = receive(0);
    loader.prefetch({b, c});
    CHECK(loader.wait_idle(std::chrono::seconds(3)));
    CHECK(decoded == 2 && loader.stats().cache_bytes == 128);
    loader.request(a);
    CHECK(receive(1) == original && decoded == 2);
    loader.request(b);
    const auto next = receive(2);
    CHECK(decoded == 2 && next != original);
    loader.configure(64, true);
    CHECK(loader.wait_idle(std::chrono::seconds(3)) && loader.stats().cache_bytes == 64);
    loader.prefetch({a});
    CHECK(loader.wait_idle(std::chrono::seconds(3)) && decoded == 2);
    loader.configure(0, false);
    CHECK(loader.wait_idle(std::chrono::seconds(3)) && loader.stats().cache_bytes == 0);
    loader.request(c);
    CHECK(receive(3)->bytes() == 64 && decoded == 3 && loader.stats().cache_bytes == 0);
    loader.prefetch({a, b});
    CHECK(loader.wait_idle(std::chrono::seconds(3)) && decoded == 3);
    loader.cancel(true);
    CHECK(loader.wait_idle(std::chrono::seconds(3)) && loader.stats().foreground_bytes == 0);
}
TEST(releasing_cache_during_prefetch_prevents_late_result_from_repopulating_it) {
    std::promise<void> started, release;
    const auto gate = release.get_future().share();
    std::atomic<int> callbacks{};
    Loader loader([&](Loaded) { ++callbacks; }, 128,
                  [&](const auto &) {
                      started.set_value();
                      if (gate.wait_for(std::chrono::seconds(3)) != std::future_status::ready)
                          throw std::runtime_error("Test gate timed out");
                      Image image;
                      image.width = image.height = 4;
                      image.frames.push_back({std::vector<Pixel>(16), 100});
                      return image;
                  });
    loader.prefetch({std::filesystem::path(GVN_FIXTURES) / L"alpha.png"});
    CHECK(started.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    loader.cancel(true);
    release.set_value();
    CHECK(loader.wait_idle(std::chrono::seconds(3)));
    CHECK(callbacks == 0 && loader.stats().cache_bytes == 0 && loader.stats().foreground_bytes == 0);
    CHECK(loader.stats().discarded == 1);
}
TEST(background_decode_budgets_do_not_reduce_foreground_format_support) {
    const auto root = std::filesystem::path(GVN_FIXTURES);
    for (const auto name : {L"alpha.png", L"rgb.bmp", L"psd-16-3.psd", L"disposal.gif", L"blend.apng"}) {
        THROWS(load_source(root / name, {}, 8, kSourceLimit));
        CHECK(!load_source(root / name).frames.empty());
        THROWS(read_file(root / name, 1));
    }
    Archive zip(read_file(root / L"images.zip"));
    const auto entries = zip.entries();
    CHECK(!entries.empty());
    THROWS(zip.read(entries[0].index, {}, 4));
    CHECK(!zip.read(entries[0].index).frames.empty());
}
TEST(cache_lru_limit_and_replace) {
    ImageCache cache(128);
    auto image = std::make_shared<Image>();
    image->width = image->height = 4;
    image->frames.resize(1);
    image->frames[0].pixels.resize(16);
    cache.put(L"a", image);
    cache.put(L"b", image);
    CHECK(cache.get(L"a"));
    cache.put(L"c", image);
    CHECK(!cache.get(L"b"));
    CHECK(cache.bytes() == 128);
    cache.put(L"a", image);
    CHECK(cache.bytes() == 128);
    auto exact = std::make_shared<Image>(*image);
    exact->width = 8;
    exact->frames[0].pixels.resize(32);
    cache.put(L"exact", exact);
    CHECK(cache.get(L"exact"));
    CHECK(cache.bytes() == 128);
    auto huge = std::make_shared<Image>(*image);
    huge->width = 16;
    huge->frames[0].pixels.resize(64);
    cache.put(L"too big", huge);
    CHECK(!cache.get(L"too big"));
    CHECK(cache.bytes() == 128);
}
TEST(stale_result_never_published) {
    std::promise<void> started, release;
    auto gate = release.get_future().share();
    std::promise<Loaded> completed;
    std::atomic<int> callbacks{};
    {
        Loader loader(
            [&](Loaded result) {
                ++callbacks;
                completed.set_value(std::move(result));
            },
            128,
            [&](const auto &path) {
                if (path.filename() == L"alpha.png") {
                    started.set_value();
                    gate.wait();
                }
                Image image;
                image.width = image.height = 1;
                image.frames = {{{{1, 2, 3, 255}}, 100}};
                return image;
            });
        loader.request(std::filesystem::path(GVN_FIXTURES) / L"alpha.png");
        CHECK(started.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready);
        const auto id = loader.request(std::filesystem::path(GVN_FIXTURES) / L"rgb.bmp");
        release.set_value();
        auto future = completed.get_future();
        CHECK(future.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
        const auto result = future.get();
        CHECK(result.sequence == id);
        CHECK(result.path.filename() == L"rgb.bmp");
        CHECK(result.image);
    }
    CHECK(callbacks == 1);
}
TEST(worker_recovers_after_failure) {
    std::promise<Loaded> failure, success;
    Loader loader([&](Loaded value) {
        if (value.error.empty())
            success.set_value(std::move(value));
        else
            failure.set_value(std::move(value));
    });
    loader.request(std::filesystem::path(GVN_FIXTURES) / L"invalid.bin");
    auto failed = failure.get_future();
    CHECK(failed.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    CHECK(!failed.get().error.empty());
    loader.request(std::filesystem::path(GVN_FIXTURES) / L"alpha.png");
    auto loaded = success.get_future();
    CHECK(loaded.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    CHECK(loaded.get().image->width == 3);
}
TEST(prefetched_single_image_is_reused_across_comic_flags_and_force_reload) {
    for (const bool customized : {false, true}) {
        LoadingOptions options;
        if (customized) {
            options.preserve_alpha = false;
            options.exif_orientation = false;
            options.background = 0x123456;
        }
        std::promise<void> started, release;
        auto warm_started = started.get_future();
        auto gate = release.get_future().share();
        std::array<std::promise<Loaded>, 6> completed;
        std::vector<std::future<Loaded>> futures;
        for (auto &event : completed)
            futures.push_back(event.get_future());
        std::atomic<size_t> callbacks{}, decoded{};
        Loader loader([&](Loaded result) { completed.at(callbacks++).set_value(std::move(result)); }, 128,
                      [&](const auto &) {
                          const auto number = ++decoded;
                          if (number == 1) {
                              started.set_value();
                              if (gate.wait_for(std::chrono::seconds(3)) != std::future_status::ready)
                                  throw std::runtime_error("Prefetch gate timed out");
                          }
                          Image image;
                          image.width = image.height = 1;
                          image.frames = {{{{static_cast<uint8_t>(number), 20, 30, 255}}, 100}};
                          return image;
                      });
        const auto path = std::filesystem::path(GVN_FIXTURES) / L"alpha.png";
        loader.prefetch({path}, options);
        CHECK(warm_started.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
        loader.request(path, false, {}, true, true, options);
        release.set_value();
        const auto receive = [&](size_t index) {
            CHECK(futures[index].wait_for(std::chrono::seconds(3)) == std::future_status::ready);
            auto result = futures[index].get();
            CHECK(result.error.empty() && result.image);
            return result.image;
        };
        const auto warm = receive(0);
        CHECK(decoded == 1);
        size_t index = 1;
        for (const bool rtl : {false, true})
            for (const bool height : {false, true}) {
                loader.request(path, false, {}, rtl, height, options);
                CHECK(receive(index++) == warm && decoded == 1);
            }
        loader.request(path, true, {}, false, false, options);
        const auto fresh = receive(5);
        CHECK(decoded == 2 && fresh != warm && fresh->frames[0].pixels[0].r == 2);
    }
}
TEST(comic_pair_cache_keeps_reading_direction_independent) {
    std::array<std::promise<Loaded>, 2> completed;
    auto first = completed[0].get_future(), second = completed[1].get_future();
    std::atomic<size_t> count{};
    Loader loader([&](Loaded result) { completed.at(count++).set_value(std::move(result)); });
    const auto root = std::filesystem::path(GVN_FIXTURES);
    loader.request(root / L"page1.png", false, root / L"page2.png", true, true);
    CHECK(first.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    const auto rtl = first.get();
    loader.request(root / L"page1.png", false, root / L"page2.png", false, true);
    CHECK(second.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    const auto ltr = second.get();
    CHECK(rtl.error.empty() && ltr.error.empty() && rtl.image != ltr.image);
    CHECK(rtl.image->frames[0].pixels[0] == Pixel{250, 80, 35, 170});
    CHECK(ltr.image->frames[0].pixels[0] == Pixel{20, 100, 210, 255});
}
TEST(comic_pair_cache_tracks_active_modes_colors_and_methods_without_inactive_misses) {
    std::array<std::promise<Loaded>, 7> replies;
    std::vector<std::future<Loaded>> results;
    for (auto &reply : replies)
        results.push_back(reply.get_future());
    std::atomic<size_t> count{};
    Loader loader([&](Loaded value) { replies.at(count++).set_value(std::move(value)); });
    const auto root = std::filesystem::path(GVN_FIXTURES) / L"comic";
    const auto run = [&](size_t index, ComicOptions options) {
        loader.request(root / L"first.png", false, root / L"second-17.png", true, options);
        CHECK(results[index].wait_for(std::chrono::seconds(3)) == std::future_status::ready);
        auto value = results[index].get();
        CHECK(value.image && value.error.empty() && loader.wait_idle(std::chrono::seconds(3)));
        return value.image;
    };
    const auto padding = run(0, {0});
    CHECK(run(1, {0, 10, 9}) == padding);
    const auto colored = run(2, {0, 2, 1, 0x123456});
    CHECK(colored != padding && colored->frames[0].pixels[5] == Pixel{0x12, 0x34, 0x56, 255});
    const auto simple = run(3, {1, 0});
    const auto linear = run(4, {1, 1});
    CHECK(simple != linear && simple->width == 10 && linear->height == 11);
    const auto enlarged = run(5, {2, 2, 0});
    CHECK(enlarged->width == 16 && enlarged->height == 17);
    CHECK(run(6, {2, 10, 0, 0xffffff}) == enlarged);
    CHECK(loader.stats().foreground_loads == 5 && loader.stats().cache_hits == 2);
}
TEST(loading_options_are_part_of_cache_identity) {
    std::array<std::promise<Loaded>, 4> completed;
    std::vector<std::future<Loaded>> futures;
    for (auto &event : completed)
        futures.push_back(event.get_future());
    std::atomic<size_t> count{};
    Loader loader([&](Loaded result) { completed.at(count++).set_value(std::move(result)); });
    const auto path = std::filesystem::path(GVN_FIXTURES) / L"alpha.png";
    const auto receive = [&](size_t i) {
        CHECK(futures[i].wait_for(std::chrono::seconds(3)) == std::future_status::ready);
        auto result = futures[i].get();
        CHECK(result.image && result.error.empty());
        return result.image;
    };
    loader.request(path);
    const auto raw = receive(0);
    LoadingOptions options;
    options.preserve_alpha = false;
    options.background = 0x123456;
    loader.request(path, false, {}, false, false, options);
    const auto first = receive(1);
    CHECK(first != raw && first->frames[0].pixels[2] == Pixel{18, 52, 86, 255});
    options.background = 0xfedcba;
    loader.request(path, false, {}, false, false, options);
    CHECK(receive(2)->frames[0].pixels[2] == Pixel{254, 220, 186, 255});
    loader.request(path, false, {}, true, true);
    CHECK(receive(3) == raw && raw->frames[0].pixels[2] == Pixel{0, 0, 255, 0});
}
TEST(loader_folder_queries_reuse_worker_preserve_image_cache_and_do_not_decode) {
    FolderTestTree tree;
    const auto image_path = tree.file(L"a/page.png");
    tree.directory(L"b-empty");
    std::array<std::promise<Loaded>, 6> replies;
    std::vector<std::future<Loaded>> futures;
    for (auto &reply : replies)
        futures.push_back(reply.get_future());
    std::atomic<size_t> callbacks{}, decodes{};
    std::atomic<bool> wrong_thread{};
    const auto caller = std::this_thread::get_id();
    Loader loader(
        [&](Loaded value) {
            if (std::this_thread::get_id() == caller)
                wrong_thread = true;
            replies.at(callbacks++).set_value(std::move(value));
        },
        128,
        [&](const auto &) {
            ++decodes;
            Image image;
            image.width = image.height = 4;
            image.frames.push_back({std::vector<Pixel>(16, {12, 34, 56, 127}), 100});
            return image;
        });
    const auto receive = [&](size_t index) {
        CHECK(futures[index].wait_for(std::chrono::seconds(3)) == std::future_status::ready);
        auto result = futures[index].get();
        CHECK(loader.wait_idle(std::chrono::seconds(3)));
        return result;
    };
    auto sequence = loader.request_folder({tree.root, FolderMove::Next, {}, true, tree.root});
    auto result = receive(0);
    CHECK(result.sequence == sequence && result.folder_match && !result.image && result.error.empty());
    CHECK(result.path == tree.root / L"a" && result.folder_match->directory == result.path);
    CHECK(!decodes && !wrong_thread && loader.stats().foreground_loads == 0);
    loader.request(image_path);
    const auto image = receive(1).image;
    CHECK(image && decodes == 1 && loader.stats().cache_bytes == image->bytes());
    loader.request_folder({tree.root / L"a", FolderMove::AdjacentNext, {}, true, tree.root});
    result = receive(2);
    CHECK(result.folder_match && result.path == tree.root / L"b-empty" && !result.image);
    CHECK(decodes == 1 && loader.stats().cache_bytes == image->bytes());
    loader.request_folder({tree.root, FolderMove::Previous, {}, true, tree.root});
    result = receive(3);
    CHECK(result.folder_match && result.path.empty() && !result.folder_match->cancelled);
    CHECK(!result.image && result.error.empty());
    loader.request_folder({tree.root / L"missing", FolderMove::Next, {}, true, tree.root});
    result = receive(4);
    CHECK(!result.error.empty() && !result.image && !result.folder_match);
    loader.request(image_path);
    CHECK(receive(5).image == image && decodes == 1 && loader.stats().cache_hits == 1);
    CHECK(callbacks == 6 && !wrong_thread);
}
TEST(loader_queued_folder_cancellation_and_image_supersession_are_sequence_guarded) {
    FolderTestTree tree;
    const auto a = tree.file(L"a/page.png"), b = tree.file(L"b/page.png");
    std::promise<void> entered, release;
    auto entered_future = entered.get_future(), release_future = release.get_future();
    std::promise<Loaded> reply;
    auto future = reply.get_future();
    std::atomic<size_t> decodes{}, callbacks{};
    Loader loader(
        [&](Loaded value) {
            ++callbacks;
            reply.set_value(std::move(value));
        },
        128,
        [&](const auto &) {
            if (++decodes == 1) {
                entered.set_value();
                release_future.wait();
            }
            Image image;
            image.width = image.height = 4;
            image.frames.push_back({std::vector<Pixel>(16, {1, 2, 3, 127}), 100});
            return image;
        });
    loader.request(a);
    const bool started = entered_future.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    if (!started)
        release.set_value();
    CHECK(started);
    const auto cancelled = loader.request_folder({tree.root / L"a", FolderMove::Next, {}, true, tree.root});
    loader.cancel();
    const auto replacement = loader.request(b);
    release.set_value();
    CHECK(future.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    const auto result = future.get();
    CHECK(loader.wait_idle(std::chrono::seconds(3)));
    CHECK(result.sequence == replacement && replacement > cancelled && result.path == b);
    CHECK(result.image && !result.folder_match && callbacks == 1 && decodes == 2);
}
TEST(loader_active_folder_cancellation_discards_result_before_replacement_image) {
    FolderTestTree tree;
    const auto path = tree.file(L"a/page.png");
    std::promise<void> entered, release;
    auto entered_future = entered.get_future(), release_future = release.get_future();
    std::promise<Loaded> reply;
    auto future = reply.get_future();
    std::atomic<size_t> callbacks{}, decodes{};
    std::atomic<bool> cancelled_discovery{};
    Loader loader(
        [&](Loaded value) {
            ++callbacks;
            reply.set_value(std::move(value));
        },
        128,
        [&](const auto &) {
            ++decodes;
            Image image;
            image.width = image.height = 4;
            image.frames.push_back({std::vector<Pixel>(16), 100});
            return image;
        },
        [&](const FolderQuery &query, const std::function<bool()> &cancelled) {
            bool paused{};
            auto result = find_folder(query, [&] {
                if (!paused) {
                    paused = true;
                    entered.set_value();
                    release_future.wait();
                }
                return cancelled();
            });
            cancelled_discovery = result.cancelled;
            return result;
        });
    const auto old = loader.request_folder({tree.root, FolderMove::Next, {}, true, tree.root});
    const bool started = entered_future.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    if (!started)
        release.set_value();
    CHECK(started);
    loader.cancel();
    const auto replacement = loader.request(path);
    release.set_value();
    CHECK(future.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    const auto result = future.get();
    CHECK(loader.wait_idle(std::chrono::seconds(3)));
    CHECK(cancelled_discovery && callbacks == 1 && decodes == 1 && replacement > old);
    CHECK(result.sequence == replacement && result.path == path && result.image && !result.folder_match);
}
TEST(loader_destruction_stops_active_folder_discovery_without_callback) {
    FolderTestTree tree;
    std::promise<void> entered;
    auto future = entered.get_future();
    std::atomic<bool> observed_stop{};
    std::atomic<size_t> callbacks{};
    auto loader = std::make_unique<Loader>(
        [&](Loaded) { ++callbacks; }, 128, std::function<Image(const std::filesystem::path &)>{},
        [&](const FolderQuery &query, const std::function<bool()> &cancelled) {
            entered.set_value();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!cancelled() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            auto result = find_folder(query, cancelled);
            observed_stop = result.cancelled;
            return result;
        });
    loader->request_folder({tree.root, FolderMove::Next, {}, true, tree.root});
    CHECK(future.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    loader.reset();
    CHECK(observed_stop && callbacks == 0);
}
TEST(loader_collects_ordered_folder_metadata_on_worker_without_decoding) {
    FolderTestTree tree;
    tree.file(L"target/2.png");
    tree.file(L"target/10.png");
    std::promise<Loaded> ready;
    auto reply = ready.get_future();
    const auto caller = std::this_thread::get_id();
    std::atomic<bool> worker{};
    std::atomic<size_t> decodes{};
    Loader loader(
        [&](Loaded result) {
            worker = std::this_thread::get_id() != caller;
            ready.set_value(std::move(result));
        },
        128,
        [&](const auto &) {
            ++decodes;
            return Image{};
        });
    FolderQuery query{tree.root, FolderMove::Next, {}, true, tree.root};
    query.collect_files = true;
    query.reverse = true;
    loader.request_folder(query);
    CHECK(reply.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    const auto result = reply.get();
    CHECK(loader.wait_idle(std::chrono::seconds(3)));
    CHECK(worker && !decodes && !result.image && result.error.empty());
    CHECK(result.folder_match && result.folder_match->files && result.folder_match->files->size() == 2);
    CHECK(result.folder_match->files->front().path.filename() == L"10.png");
    CHECK(loader.stats().cache_bytes == 0 && loader.stats().foreground_loads == 0);
}

TEST(gif_style_is_part_of_foreground_cache_and_prefetch_identity) {
    std::array<std::promise<Loaded>, 11> replies;
    std::vector<std::future<Loaded>> futures;
    for (auto &reply : replies)
        futures.push_back(reply.get_future());
    std::atomic<size_t> count{};
    Loader loader([&](Loaded value) { replies.at(count++).set_value(std::move(value)); });
    const auto file = std::filesystem::path(GVN_FIXTURES) /
                      L"../../docs/verification/gif-reference-0.45/timing/assets/timing.gif";
    const auto receive = [&](size_t index) {
        CHECK(futures[index].wait_for(std::chrono::seconds(3)) == std::future_status::ready);
        auto result = futures[index].get();
        CHECK(result.image && result.error.empty());
        return result.image;
    };
    std::array<std::shared_ptr<const Image>, 5> images;
    for (int style = 0; style < 5; ++style) {
        LoadingOptions options;
        options.gif_speed = style;
        loader.request(file, false, {}, false, false, options);
        images[style] = receive(style);
        CHECK(images[style]->frames[2].duration_ms == (style ? 100 : 10));
        for (int old = 0; old < style; ++old)
            CHECK(images[old] != images[style]);
    }
    for (int style = 0; style < 5; ++style) {
        LoadingOptions options;
        options.gif_speed = style;
        loader.request(file, false, {}, false, false, options);
        CHECK(receive(5 + style) == images[style]);
    }
    LoadingOptions warm;
    warm.gif_speed = 2;
    loader.prefetch({file}, warm);
    LoadingOptions foreground;
    foreground.gif_speed = 4;
    loader.request(file, false, {}, false, false, foreground);
    CHECK(receive(10) == images[4]);
}

int main() {
    return run_tests();
}
TEST(comic_pair_order_height_and_source_protection) {
    std::promise<Loaded> promise;
    Loader loader([&](Loaded result) { promise.set_value(std::move(result)); });
    const auto root = std::filesystem::path(GVN_FIXTURES);
    loader.request(root / L"page1.png", false, root / L"page2.png", true, true);
    auto result = promise.get_future().get();
    CHECK(result.error.empty());
    CHECK(result.image->width == 210 && result.image->height == 180);
    CHECK(result.image->path.empty());
    CHECK(result.image->frames[0].pixels[0] == Pixel{250, 80, 35, 170});
    CHECK(result.image->frames[0].pixels[100] == Pixel{20, 100, 210, 255});
}

TEST(view_rotation_prefetch_and_foreground_cache_share_the_same_source_identity) {
    std::array<std::promise<Loaded>, 17> completed;
    std::vector<std::future<Loaded>> futures;
    for (auto &promise : completed)
        futures.push_back(promise.get_future());
    std::atomic<size_t> callbacks{};
    Loader loader([&](Loaded result) { completed.at(callbacks++).set_value(std::move(result)); });
    const auto file = std::filesystem::path(GVN_FIXTURES) / L"view-rotation/single/portrait.png";
    const auto receive = [&](size_t index) {
        CHECK(futures[index].wait_for(std::chrono::seconds(3)) == std::future_status::ready);
        auto result = futures[index].get();
        CHECK(result.image && result.error.empty());
        return result.image;
    };
    LoadingOptions warm;
    warm.view_rotation = 1;
    loader.prefetch({file}, warm);
    CHECK(loader.wait_idle(std::chrono::seconds(3)));
    loader.request(file, false, {}, false, false, warm);
    const auto warmed = receive(0);
    CHECK(warmed->width == 7 && warmed->height == 5);
    CHECK(loader.wait_idle(std::chrono::seconds(3)));
    CHECK(loader.stats().foreground_loads == 0 && loader.stats().cache_hits == 1);
    std::array<std::shared_ptr<const Image>, 8> images;
    for (int mode = 0; mode < 8; ++mode) {
        LoadingOptions options;
        options.view_rotation = mode;
        loader.request(file, false, {}, false, false, options);
        images[mode] = receive(1 + mode);
        for (int previous = 0; previous < mode; ++previous)
            CHECK(images[previous] != images[mode]);
    }
    CHECK(images[1] == warmed);
    for (int mode = 0; mode < 8; ++mode) {
        LoadingOptions options;
        options.view_rotation = mode;
        loader.request(file, false, {}, false, false, options);
        CHECK(receive(9 + mode) == images[mode]);
    }
    CHECK(loader.wait_idle(std::chrono::seconds(3)));
    CHECK(loader.stats().foreground_loads == 7);
}
TEST(comic_rotation_stage_and_wide_policy_have_independent_cached_results) {
    std::array<std::promise<Loaded>, 8> completed;
    std::vector<std::future<Loaded>> futures;
    for (auto &promise : completed)
        futures.push_back(promise.get_future());
    std::atomic<size_t> callbacks{};
    Loader loader([&](Loaded result) { completed.at(callbacks++).set_value(std::move(result)); });
    const auto folder = std::filesystem::path(GVN_FIXTURES) / L"view-rotation/comic";
    const auto receive = [&](size_t index) {
        CHECK(futures[index].wait_for(std::chrono::seconds(3)) == std::future_status::ready);
        auto result = futures[index].get();
        CHECK(result.image && result.error.empty());
        return result.image;
    };
    LoadingOptions options;
    options.view_rotation = 1;
    std::array<std::shared_ptr<const Image>, 4> images;
    const std::array<std::pair<uint32_t, uint32_t>, 4> sizes{{{9, 8}, {9, 8}, {16, 5}, {7, 5}}};
    for (int index = 0; index < 8; ++index) {
        const int variant = index % 4;
        ComicOptions comic;
        comic.rotate_before = (variant & 2) != 0;
        comic.single_wide = (variant & 1) != 0;
        loader.request(folder / L"PP-1.png", false, folder / L"PP-2.png", false, comic, options);
        const auto result = receive(index);
        CHECK(result->width == sizes[variant].first && result->height == sizes[variant].second);
        if (index < 4) {
            images[variant] = result;
            for (int previous = 0; previous < variant; ++previous)
                CHECK(images[previous] != result);
        } else
            CHECK(images[variant] == result);
    }
    CHECK(loader.wait_idle(std::chrono::seconds(3)));
    CHECK(loader.stats().foreground_loads == 4 && loader.stats().cache_hits == 4);
}
TEST(loader_interrupts_obsolete_prefetch_but_promotes_the_requested_same_image) {
    const auto root=std::filesystem::path(GVN_FIXTURES),a=root/L"alpha.png",b=root/L"rgb.bmp",c=root/L"disposal.gif";
    for(bool promote:{false,true}){
        std::promise<void> started;auto entered=started.get_future();std::atomic<bool> release{};
        std::atomic<unsigned> callbacks{},slow_decodes{};std::promise<Loaded> first,last;
        auto initial=first.get_future(),final=last.get_future();
        Loader loader([&](Loaded value){if(callbacks++==0)first.set_value(std::move(value));else last.set_value(std::move(value));},1024*1024,{}, {},
            [&](const auto &path,const CancelCheck &cancel){
                if(path==b){if(slow_decodes++==0)started.set_value();while(!release&&!cancel())Sleep(1);check_cancelled(cancel);}
                return new_image(4,4);
            });
        loader.request(a);CHECK(initial.wait_for(std::chrono::seconds(3))==std::future_status::ready);
        CHECK(initial.get().image&&loader.wait_idle(std::chrono::seconds(3)));
        loader.prefetch({b});CHECK(entered.wait_for(std::chrono::seconds(3))==std::future_status::ready);
        const auto start=std::chrono::steady_clock::now();loader.request(promote?b:c);
        if(promote)release=true;
        CHECK(final.wait_for(std::chrono::seconds(3))==std::future_status::ready);
        const auto value=final.get();CHECK(value.image&&value.error.empty()&&value.path==(promote?b:c));
        CHECK(loader.wait_idle(std::chrono::seconds(3))&&slow_decodes==1);
        CHECK(promote?loader.stats().cancelled==0:loader.stats().cancelled==1);
        std::cout<<"PREFETCH_PRIORITY {\"same_image\":"<<promote<<",\"foreground_ms\":"
            <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<"}\n";
    }
}
TEST(cancellable_decode_preserves_pixels_and_stops_at_safe_read_and_frame_boundaries) {
    auto source=new_image(1024,768);uint32_t seed=7;
    for(auto &p:source.frames[0].pixels){seed=seed*1664525u+1013904223u;p={uint8_t(seed>>24),uint8_t(seed>>16),uint8_t(seed>>8),uint8_t(seed)};}
    for(const auto *extension:{L".png",L".jpg",L".bmp"}){
        const auto encoded=encode(source,0,extension);const auto start=std::chrono::steady_clock::now();
        const auto expected=decode(encoded);const auto middle=std::chrono::steady_clock::now();unsigned checks{};
        const auto result=decode(encoded,L"",kDecodeLimit,{},[&]{++checks;return false;});
        const auto end=std::chrono::steady_clock::now();CHECK(checks>=6&&result.width==expected.width&&result.height==expected.height);
        CHECK(result.frames[0].pixels==expected.frames[0].pixels);
        unsigned calls{};bool stopped{};try{decode(encoded,L"",kDecodeLimit,{},[&]{return ++calls>=6;});}catch(const OperationCancelled &){stopped=true;}
        CHECK(stopped&&calls==6);
        std::cout<<"DECODE_CONTROL {\"normal_ms\":"<<std::chrono::duration<double,std::milli>(middle-start).count()
            <<",\"cancellable_ms\":"<<std::chrono::duration<double,std::milli>(end-middle).count()<<"}\n";
    }
    const auto root=std::filesystem::path(GVN_FIXTURES);
    const auto tiff=read_file(root/L"alpha.tiff");CHECK(decode(tiff).frames[0].pixels==decode(tiff,L"",kDecodeLimit,{},[]{return false;}).frames[0].pixels);
    for(const auto *name:{L"disposal.gif",L"blend.apng"}){
        const auto bytes=read_file(root/name);unsigned calls{};bool stopped{};
        try{decode_animation(bytes,kDecodeLimit,{},[&]{return ++calls>=3;});}catch(const OperationCancelled &){stopped=true;}
        CHECK(stopped&&calls==3);
        const auto plain=decode(bytes),controlled=decode(bytes,L"",kDecodeLimit,{},[]{return false;});
        CHECK(plain.frames.size()==controlled.frames.size());
        for(size_t n=0;n<plain.frames.size();++n)CHECK(plain.frames[n].pixels==controlled.frames[n].pixels);
    }
    bool stopped{};try{read_file(root/L"alpha.png",kSourceLimit,[]{return true;});}catch(const OperationCancelled &){stopped=true;}
    CHECK(stopped);
    for(const auto *name:{L"psd-8-1.psd",L"psd-8-2.psd"}){
        const auto bytes=read_file(root/name);const auto plain=decode_psd(bytes,kDecodeLimit);
        CHECK(decode_psd(bytes,kDecodeLimit,[]{return false;}).frames[0].pixels==plain.frames[0].pixels);
        unsigned calls{};bool cancelled{};
        try{decode_psd(bytes,kDecodeLimit,[&]{return ++calls>=3;});}catch(const OperationCancelled &){cancelled=true;}
        CHECK(cancelled&&calls==3);
    }
    Archive zip(read_file(root/L"images.zip"));
    for(const auto &entry:zip.entries()){
        const auto plain=zip.read(entry.index),controlled=zip.read(entry.index,{},kDecodeLimit,kSourceLimit,[]{return false;});
        CHECK(plain.width==controlled.width&&plain.height==controlled.height&&plain.frames.size()==controlled.frames.size());
        for(size_t n=0;n<plain.frames.size();++n)CHECK(plain.frames[n].pixels==controlled.frames[n].pixels);
    }
    unsigned reads{};stopped=false;
    try{zip.read(zip.entries().front().index,{},kDecodeLimit,kSourceLimit,[&]{return ++reads>=2;});}catch(const OperationCancelled &){stopped=true;}
    CHECK(stopped&&reads==2);
}
