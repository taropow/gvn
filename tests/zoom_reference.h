#pragma once
#include "test.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
namespace gvn_test {
struct ZoomReference {
    int mode{}, below{}, above{};
    double before{}, after{};
    bool enlarge{};
    std::string label;
};
inline std::vector<ZoomReference> zoom_references() {
    std::ifstream stream(std::filesystem::path(GVN_FIXTURES)/L"zoom-step-reference/cases.tsv"); CHECK(stream);
    std::vector<ZoomReference> result;
    for(std::string line;std::getline(stream,line);) {
        std::istringstream row(line);std::array<std::string,7> f;
        for(auto &field:f) CHECK(std::getline(row,field,'\t'));
        result.push_back({std::stoi(f[0]),std::stoi(f[1]),std::stoi(f[2]),std::stod(f[3])/100,
                          std::stod(f[5])/100,std::stoi(f[4])!=0,f[6]});
    }
    CHECK(result.size()==474); return result;
}
}
