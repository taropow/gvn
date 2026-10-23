#pragma once
#include "gvn/core.h"
#include "gvn/loading.h"
#include <optional>
#include <span>
namespace gvn {
// Called only after normal GIF RGBA decoding. Remaining budget accounts for all
// retained movie pixels; no source snapshot is retained when it will not fit.
std::shared_ptr<const IndexedSource> gif_index_source(std::span<const uint8_t>,uint32_t,uint32_t,
    Pixel clear,const LoadingOptions &,std::optional<Pixel> file_background,size_t remaining_budget);
}
