#include "gvn/archive.h"
#include "test.h"
using namespace gvn;
const auto fixtures = std::filesystem::path(GVN_FIXTURES);
TEST(zip_entries_and_pixels) {
    Archive archive(read_file(fixtures / L"images.zip"));
    const auto entries = archive.entries();
    CHECK(entries.size() == 3);
    CHECK(entries[0].name == L"画像/2.png");
    CHECK(archive.read(entries[0].index).frames[0].pixels ==
          load_image(fixtures / L"alpha.png").frames[0].pixels);
    CHECK(archive.read(entries[1].index).width == 3);
    CHECK(archive.read(entries[2].index).height == 2);
    THROWS(archive.read(999));
    THROWS(archive.read(2));
}
TEST(zip_source_and_corruption) {
    auto path = fixtures / L"images.zip";
    const std::filesystem::path virtual_path(path.wstring() + L"|0");
    CHECK(source_file(virtual_path) == path);
    CHECK(load_source(virtual_path).width == 3);
    THROWS(load_source(path.wstring() + L"|invalid"));
    THROWS(Archive(std::vector<uint8_t>{1, 2, 3}).entries());
    auto bytes = read_file(path);
    bytes.resize(30);
    THROWS(Archive(bytes).entries());
}
int main() {
    return run_tests();
}
