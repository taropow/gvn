#include "gvn/folder_navigation.h"
#include "folder_test_tree.h"
#include <algorithm>
#include <cwctype>
#include <chrono>
using namespace gvn;
namespace fs = std::filesystem;

TEST(folder_depth_first_forward_reverse_and_bounded_endpoints) {
    FolderTestTree tree;
    for (const auto name : {L"root.png", L"a/1.png", L"a/child/1.PSD", L"b/child/1.apng", L"c/1.gif"})
        tree.file(name);
    tree.directory(L"a/empty");
    tree.directory(L"b/before-child");
    FolderQuery query{tree.root, FolderMove::Next, {}, true, tree.root};
    for (const auto folder : {L"a", L"a/child", L"b/child", L"c"}) {
        const auto match = find_folder(query);
        CHECK(match.directory == tree.root / folder && match.examined && !match.cancelled);
        query.directory = match.directory;
    }
    CHECK(find_folder(query).directory.empty());
    query.move = FolderMove::Previous;
    for (const auto folder : {L"b/child", L"a/child", L"a", L""}) {
        const auto match = find_folder(query);
        CHECK(match.directory == (*folder ? tree.root / folder : tree.root) && !match.cancelled);
        query.directory = match.directory;
    }
    CHECK(find_folder(query).directory.empty());
}
TEST(folder_adjacent_selects_empty_and_parent_does_not_skip_it) {
    FolderTestTree tree;
    tree.file(L"a/child/page.png");
    tree.directory(L"b-empty");
    tree.file(L"c/page.bmp");
    const auto find = [&](const wchar_t *from, FolderMove move) {
        return find_folder({tree.root / from, move, {}, true, tree.root});
    };
    CHECK(find(L"a", FolderMove::AdjacentNext).directory == tree.root / L"b-empty");
    CHECK(find(L"c", FolderMove::AdjacentPrevious).directory == tree.root / L"b-empty");
    CHECK(find(L"a/child", FolderMove::Parent).directory == tree.root / L"a");
    CHECK(find(L"a", FolderMove::Parent).directory == tree.root);
    CHECK(find(L"", FolderMove::Parent).directory.empty());
    CHECK(find(L"a", FolderMove::AdjacentPrevious).directory.empty());
    CHECK(find(L"c", FolderMove::AdjacentNext).directory.empty());
    CHECK(find(L"b-empty", FolderMove::Next).directory == tree.root / L"c");
    CHECK(find(L"b-empty", FolderMove::Previous).directory == tree.root / L"a/child");
}
TEST(folder_natural_order_unicode_case_and_existing_excluded_anchor) {
    FolderTestTree tree;
    tree.file(L"第2巻/画像.png");
    tree.file(L"第10巻/画像.png");
    FolderQuery query{tree.root, FolderMove::Next, {}, true, tree.root};
    CHECK(find_folder(query).directory == tree.root / L"第2巻");
    query.natural = false;
    CHECK(find_folder(query).directory == tree.root / L"第10巻");
    query.directory = tree.root / L"第2巻";
    query.move = FolderMove::Previous;
    CHECK(find_folder(query).directory == tree.root / L"第10巻");
    CHECK(SetFileAttributesW((tree.root / L"第10巻").c_str(), FILE_ATTRIBUTE_HIDDEN));
    query.move = FolderMove::AdjacentNext;
    query.natural = true;
    CHECK(find_folder(query).directory.empty());
    query.directory = tree.root / L"第10巻"; // Explicitly opened hidden folders can still be left.
    query.move = FolderMove::AdjacentPrevious;
    CHECK(find_folder(query).directory == tree.root / L"第2巻");
    auto uppercase = tree.root.native();
    std::transform(uppercase.begin(), uppercase.end(), uppercase.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(towupper(ch)); });
    query.root = uppercase;
    CHECK(find_folder(query).directory == tree.root / L"第2巻");
}
TEST(folder_directory_visibility_uses_independent_hidden_system_protected_flags) {
    FolderTestTree tree;
    tree.file(L"anchor/page.png");
    for (const auto folder : {L"b-hidden", L"c-system", L"d-protected", L"z-visible"})
        tree.file(fs::path(folder) / L"page.png");
    CHECK(SetFileAttributesW((tree.root / L"b-hidden").c_str(), FILE_ATTRIBUTE_HIDDEN));
    CHECK(SetFileAttributesW((tree.root / L"c-system").c_str(), FILE_ATTRIBUTE_SYSTEM));
    CHECK(SetFileAttributesW((tree.root / L"d-protected").c_str(),
                             FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
    for (int mask = 0; mask < 8; ++mask) {
        const FileVisibility policy{(mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0};
        FolderQuery query{tree.root / L"anchor", FolderMove::Next, policy, true, tree.root};
        const auto expected = policy.hidden             ? L"b-hidden"
                              : policy.system           ? L"c-system"
                              : policy.protected_system ? L"d-protected"
                                                        : L"z-visible";
        CHECK(find_folder(query).directory == tree.root / expected);
    }
}
TEST(folder_image_visibility_and_supported_extension_discovery_do_not_decode) {
    FolderTestTree tree;
    tree.directory(L"a-empty/folder.png"); // A directory named like an image is not an image.
    tree.file(L"a-empty/readme.txt");
    tree.file(L"b-hidden/page.png", FILE_ATTRIBUTE_HIDDEN);
    tree.file(L"c-system/page.bmp", FILE_ATTRIBUTE_SYSTEM);
    tree.file(L"d-protected/page.gif", FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    tree.file(L"z-visible/page.PSD");
    for (int mask = 0; mask < 8; ++mask) {
        const FileVisibility policy{(mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0};
        const auto expected = policy.hidden             ? L"b-hidden"
                              : policy.system           ? L"c-system"
                              : policy.protected_system ? L"d-protected"
                                                        : L"z-visible";
        CHECK(find_folder({tree.root, FolderMove::Next, policy, true, tree.root}).directory ==
              tree.root / expected);
    }
}
TEST(folder_reparse_cycle_is_skipped_by_recursive_search_but_adjacent_can_select_it) {
    FolderTestTree tree;
    tree.file(L"a/page.png");
    const auto junction = tree.junction(L"b-cycle", tree.root);
    tree.file(L"c/page.png");
    FolderQuery query{tree.root / L"a", FolderMove::Next, {}, true, tree.root};
    CHECK(find_folder(query).directory == tree.root / L"c");
    query.directory = tree.root / L"c";
    query.move = FolderMove::Previous;
    CHECK(find_folder(query).directory == tree.root / L"a");
    query.directory = tree.root / L"a";
    query.move = FolderMove::AdjacentNext;
    CHECK(find_folder(query).directory == junction);
}
TEST(folder_cancellation_before_io_and_during_discovery_has_no_destination) {
    FolderTestTree tree;
    FolderQuery query{tree.root, FolderMove::Next, {}, true, tree.root};
    auto result = find_folder(query, [] { return true; });
    CHECK(result.cancelled && result.directory.empty() && result.examined == 0);
    tree.directory(L"00-anchor");
    for (int i = 0; i < 128; ++i)
        tree.directory(L"empty-" + std::to_wstring(i));
    tree.file(L"zz-target/page.png");
    query.directory = tree.root / L"00-anchor";
    size_t polls{};
    result = find_folder(query, [&] { return ++polls >= 300; });
    CHECK(result.cancelled && result.directory.empty() && result.examined > 0 && result.examined < 129);
    result = find_folder(query);
    CHECK(!result.cancelled && result.directory == tree.root / L"zz-target" && result.examined == 129);
}
TEST(folder_invalid_or_escaped_roots_and_missing_start_are_rejected) {
    FolderTestTree tree;
    tree.directory(L"inside");
    THROWS(find_folder({{}, FolderMove::Next}));
    THROWS(find_folder({tree.root / L"missing", FolderMove::Next, {}, true, tree.root}));
    THROWS(find_folder({tree.root, FolderMove::Next, {}, true, tree.root / L"inside"}));
    THROWS(find_folder({tree.root, static_cast<FolderMove>(99), {}, true, tree.root}));
    THROWS(find_folder({tree.root, FolderMove::Next, {}, true, tree.root / L"missing"}));
}
TEST(folder_default_volume_root_and_normalized_relative_or_trailing_paths) {
    FolderTestTree tree;
    tree.file(L"a/page.png");
    const auto default_root = find_folder({tree.root, FolderMove::Next});
    CHECK(default_root.directory == tree.root / L"a" && !default_root.cancelled);
    const auto root = tree.root / L".";
    auto query = FolderQuery{root / L"a/..", FolderMove::Next, {}, true, root};
    CHECK(find_folder(query).directory == tree.root / L"a");
    {
        struct RestoreDirectory {
            fs::path path = fs::current_path();
            ~RestoreDirectory() {
                fs::current_path(path);
            }
        } restore;
        fs::current_path(tree.root.parent_path());
        query.directory = tree.root.filename();
        CHECK(find_folder(query).directory == tree.root / L"a");
    }
    query.directory = tree.root / L"a/";
    query.move = FolderMove::Parent;
    CHECK(find_folder(query).directory == tree.root);
}
TEST(folder_collected_metadata_visibility_and_all_sort_modes_match_directory_contents) {
    FolderTestTree tree;
    const auto first_image = tree.file(L"target/2.png"), second_image = tree.file(L"target/10.bmp");
    tree.file(L"target/hidden.gif", FILE_ATTRIBUTE_HIDDEN);
    tree.file(L"target/readme.txt");
    tree.directory(L"target/not-a-file.png");
    {
        std::ofstream append(second_image, std::ios::app);
        append << "extra bytes";
    }
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(first_image, now - std::chrono::hours(2));
    fs::last_write_time(second_image, now - std::chrono::hours(1));
    FolderQuery query{tree.root, FolderMove::Next, {}, true, tree.root};
    query.collect_files = true;
    query.seed = 12345;
    for (const auto sort : {Sort::Name, Sort::Time, Sort::Size, Sort::Extension, Sort::Random}) {
        query.sort = sort;
        for (bool reverse : {false, true}) {
            query.reverse = reverse;
            const auto result = find_folder(query);
            CHECK(result.files && result.files->size() == 2 && !result.cancelled);
            auto expected = list_files(tree.root / L"target", query.visibility);
            sort_files(expected, sort, reverse, query.natural, query.seed);
            for (size_t i = 0; i < expected.size(); ++i) {
                const auto &file = result.files->at(i);
                CHECK(file.path == expected[i].path && file.bytes == fs::file_size(file.path));
                CHECK(file.time == fs::last_write_time(file.path).time_since_epoch().count());
            }
        }
    }
    query.sort = Sort::Name;
    query.reverse = false;
    CHECK(find_folder(query).files->front().path == first_image);
    query.natural = false;
    CHECK(find_folder(query).files->front().path == second_image);
    query.visibility.hidden = true;
    CHECK(find_folder(query).files->size() == 3);
    query.sort = static_cast<Sort>(99);
    THROWS(find_folder(query));
}
TEST(folder_collection_distinguishes_empty_visits_and_discards_cancelled_partial_lists) {
    FolderTestTree tree;
    tree.directory(L"a-empty");
    for (int i = 0; i < 256; ++i)
        tree.file(L"b-images/" + std::to_wstring(i) + L".png");
    FolderQuery query{tree.root / L"a-empty", FolderMove::AdjacentNext, {}, true, tree.root};
    query.collect_files = true;
    auto result = find_folder(query);
    CHECK(result.files && result.files->size() == 256);
    query.directory = tree.root / L"b-images";
    query.move = FolderMove::AdjacentPrevious;
    result = find_folder(query);
    CHECK(result.directory == tree.root / L"a-empty" && result.files && result.files->empty());
    query.move = FolderMove::Parent;
    result = find_folder(query);
    CHECK(result.directory == tree.root && result.files && result.files->empty());
    query.directory = tree.root / L"a-empty";
    query.move = FolderMove::Next;
    size_t checkpoints{};
    result = find_folder(query, [&] { return ++checkpoints >= 80; });
    CHECK(result.cancelled && result.directory.empty() && !result.files && result.examined == 1);
    result = find_folder(query);
    CHECK(result.files && result.files->size() == 256 && !result.cancelled);
}
TEST(folder_wrap_starts_at_bounded_root_and_skips_empty_root_without_escaping) {
    FolderTestTree tree;
    tree.file(L"root.png"); tree.file(L"a/first.png"); tree.file(L"b/last.png");
    FolderQuery query{tree.root/L"b",FolderMove::Next,{},true,tree.root};
    query.collect_files=true; query.wrap=true;
    auto match=find_folder(query);
    CHECK(match.directory==tree.root && match.files && match.files->size()==1 &&
          match.files->front().path==tree.root/L"root.png");
    CHECK(fs::remove(tree.root/L"root.png"));
    match=find_folder(query); CHECK(match.directory==tree.root/L"a" && match.files && match.files->size()==1);
    query.wrap=false; CHECK(find_folder(query).directory.empty());
    query.wrap=true; size_t checks{};
    match=find_folder(query,[&]{return ++checks>=3;});
    CHECK(match.cancelled && match.directory.empty() && !match.files);
}
TEST(folder_wrap_empty_tree_terminates_and_nested_root_does_not_visit_parent_or_siblings) {
    FolderTestTree tree; tree.file(L"parent.png"); tree.file(L"sibling/page.png");
    tree.directory(L"start/empty");
    FolderQuery query{tree.root/L"start/empty",FolderMove::Next,{},true,tree.root/L"start"};
    query.collect_files=true; query.wrap=true;
    auto match=find_folder(query); CHECK(match.directory.empty() && !match.cancelled && !match.files);
    tree.file(L"start/child/page.png");
    match=find_folder(query); CHECK(match.directory==tree.root/L"start/child");
    query.directory=match.directory; match=find_folder(query);
    CHECK(match.directory==tree.root/L"start/child" && match.files && match.files->size()==1);
}
int main() {
    return run_tests();
}
