#pragma once
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace gvn::detail {
// Keep the Windows INI parser/encoding rules, but read each section once per load.
class ProfileReader {
    struct Less {
        bool operator()(const std::wstring &a, const std::wstring &b) const {
            return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
                                        static_cast<int>(b.size()), TRUE) == CSTR_LESS_THAN;
        }
    };
    struct Section {
        std::map<std::wstring, std::wstring, Less> values;
        bool fallback{};
    };
    std::filesystem::path path;
    std::map<std::wstring, Section, Less> sections;

  public:
    explicit ProfileReader(std::filesystem::path source) : path(std::move(source)) {}
    std::wstring read(const wchar_t *section, const std::wstring &key, const std::wstring &initial) {
        auto [entry, fresh] = sections.try_emplace(section);
        auto &cached = entry->second;
        if (fresh) {
            std::vector<wchar_t> buffer(4096);
            for (;;) {
                const auto length = GetPrivateProfileSectionW(
                    section, buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
                if (length < buffer.size() - 2) {
                    for (const wchar_t *row = buffer.data(); *row; row += wcslen(row) + 1) {
                        const std::wstring text(row);
                        const auto equal = text.find(L'=');
                        if (equal == std::wstring::npos)
                            continue;
                        auto value = text.substr(equal + 1);
                        if (value.size() >= 2 && (value.front() == L'\'' || value.front() == L'"') &&
                            value.front() == value.back())
                            value = value.substr(1, value.size() - 2);
                        value.resize(std::min<size_t>(value.size(), 32767));
                        // The profile API uses the first occurrence of a duplicate key.
                        cached.values.try_emplace(text.substr(0, equal), std::move(value));
                    }
                    break;
                }
                // Unusually large hand-edited sections retain the old bounded per-key path.
                if (buffer.size() >= 4 * 1024 * 1024) {
                    cached.fallback = true;
                    break;
                }
                buffer.resize(buffer.size() * 2);
            }
        }
        if (cached.fallback) {
            std::vector<wchar_t> buffer(32768);
            GetPrivateProfileStringW(section, key.c_str(), initial.c_str(), buffer.data(),
                                     static_cast<DWORD>(buffer.size()), path.c_str());
            return buffer.data();
        }
        const auto value = cached.values.find(key);
        if (value != cached.values.end())
            return value->second;
        auto fallback = initial;
        while (!fallback.empty() && fallback.back() == L' ')
            fallback.pop_back();
        fallback.resize(std::min<size_t>(fallback.size(), 32767));
        return fallback;
    }
};
} // namespace gvn::detail
