#pragma once
#include "commands.h"
#include <span>
namespace gvn {
constexpr unsigned ContextSeparator=0,ContextRotate=7001,ContextExternal=7002,ContextFile=7003;
const std::vector<CommandInfo> &context_menu_choices();
std::vector<unsigned> default_context_items();
std::vector<unsigned> validated_context_items(std::span<const unsigned>);
const wchar_t *context_item_name(unsigned);
}
