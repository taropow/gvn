// Generated from actual reference control values and RGB outputs.
#pragma once
#include "gvn/color_adjustment.h"
namespace gvn_test {
struct ColorReference {const wchar_t *file;gvn::ColorAdjustment choice;};
inline const ColorReference color_references[]{
    {L"identity.png",{0,0,0,false,gvn::GrayMethod::Luma}},
    {L"hue90.png",{90,0,0,false,gvn::GrayMethod::Luma}},
    {L"saturation50.png",{0,50,0,false,gvn::GrayMethod::Luma}},
    {L"light25.png",{0,0,25,false,gvn::GrayMethod::Luma}},
    {L"light-minus25.png",{0,0,-25,false,gvn::GrayMethod::Luma}},
    {L"gray-middle.png",{0,-100,0,false,gvn::GrayMethod::Middle}},
    {L"gray-mean.png",{0,-100,0,false,gvn::GrayMethod::Average}},
    {L"gray-luma.png",{0,-100,0,false,gvn::GrayMethod::Luma}},
    {L"unify-red.png",{0,50,0,true,gvn::GrayMethod::Luma}},
    {L"unify-blue.png",{240,100,0,true,gvn::GrayMethod::Luma}},
    {L"combined.png",{-45,35,-20,false,gvn::GrayMethod::Average}},
    {L"unify-gray.png",{120,0,0,true,gvn::GrayMethod::Middle}},
    {L"reset-buttons.png",{0,0,0,false,gvn::GrayMethod::Luma}},
    {L"persist-gray-mean.png",{0,-100,0,false,gvn::GrayMethod::Average}},
    {L"combined-desaturate.png",{75,-40,15,false,gvn::GrayMethod::Luma}},
    {L"unify-light.png",{45,60,20,true,gvn::GrayMethod::Average}},
};
} // namespace gvn_test
