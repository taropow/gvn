#pragma once
#include "resample.h"
namespace gvn {
// Public choices remain independent of legacy/native filter enum values.
inline constexpr int display_quality_index(int method) {return method==DisplaySimple?0:method==DisplayLinear?1:2;}
inline constexpr int display_quality_filter(int index) {return index==0?DisplaySimple:index==1?DisplayLinear:DisplayLanczos3;}
inline constexpr int canonical_display_filter(int method) {return display_quality_filter(display_quality_index(method));}
inline constexpr const wchar_t *display_quality_labels[]{L"無補正",L"高速",L"高画質"};
}
