#pragma once
#include "core.h"
#include <span>
namespace gvn {
struct PromptField {
    std::wstring group, label, value;
    bool muted{};
};
enum class PromptPartsState { Absent, Supported, Invalid, Unsupported };
struct PromptDocument {
    PromptPartsState state{PromptPartsState::Absent};
    std::wstring status, source_name, positive, negative;
    std::vector<PromptField> fields;
};
// Bounded text extraction on the existing decoder input; no file IO or pixel copies.
std::shared_ptr<const PromptSource> read_png_prompt_source(std::span<const uint8_t>);
// JSON is interpreted only when the prompt window is requested.
PromptDocument describe_prompts(const std::shared_ptr<const PromptSource> &);
}
