#include "gvn/edit.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace gvn {
uint64_t physical_memory_bytes() {
    MEMORYSTATUSEX status{sizeof(status)};
    if (!GlobalMemoryStatusEx(&status) || !status.ullTotalPhys)
        throw std::runtime_error("Cannot determine total physical memory");
    return status.ullTotalPhys;
}
size_t EditMemory::pixels(uint64_t width, uint64_t height) const {
    const auto count = checked_image_pixels(width, height);
    if (!allow_physical_overflow && (!physical_bytes || count > physical_bytes / sizeof(Pixel)))
        throw std::runtime_error("Output exceeds physical memory; enable the General option to allow it");
    return count;
}
std::vector<Pixel> EditMemory::allocate(uint64_t width, uint64_t height) const {
    const auto count = pixels(width, height);
    if (before_allocate)
        before_allocate(count * sizeof(Pixel));
    return std::vector<Pixel>(count);
}
} // namespace gvn
