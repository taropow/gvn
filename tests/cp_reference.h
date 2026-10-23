#pragma once
#include "fl_reference.h"
namespace gvn_test {
struct CpReference {FlReference pixels;std::wstring code;};
inline std::vector<CpReference> cp_references() {
    const auto references=fl_references(L"cp-reference",107);
    std::ifstream codes(std::filesystem::path(GVN_FIXTURES)/L"cp-reference/codes.tsv");CHECK(codes);
    std::vector<CpReference> result;
    for(const auto &item:references) {
        std::string code;CHECK(std::getline(codes,code));
        result.push_back({item,std::wstring(code.begin(),code.end())});
    }
    CHECK(codes.peek()==std::char_traits<char>::eof());return result;
}
}
