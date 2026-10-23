#pragma once
#include "core.h"
#include "floating_selection.h"
#include <cmath>
namespace gvn {
// The one image/client mapping used by pointer input, previews and final rendering.
// At 100%, both directions use the same snapped image origin.
struct ViewportTransform {
 double x{},y{},scale{1};
 explicit ViewportTransform(const View &v,double left=0,double top=0):scale(v.scale){const bool actual=std::abs(scale-1)<1e-9;x=left+(actual?std::floor(v.left()):v.left());y=top+(actual?std::floor(v.top()):v.top());}
 FloatPoint to_image(FloatPoint p) const{return {(p.x-x)/scale,(p.y-y)/scale};}
 FloatPoint to_client(FloatPoint p) const{return {x+p.x*scale,y+p.y*scale};}
};
enum class EditorActivity {Browse,Ready,Selecting,Pen,Text,Transform,Crop};
struct EditorSession {
 EditorActivity activity{};
 static EditorSession resolve(bool editing,bool selecting,bool pen,bool floating,bool text,bool crop){return {crop?EditorActivity::Crop:floating?(text?EditorActivity::Text:EditorActivity::Transform):pen?EditorActivity::Pen:selecting?EditorActivity::Selecting:editing?EditorActivity::Ready:EditorActivity::Browse};}
 bool pending() const{return activity==EditorActivity::Text||activity==EditorActivity::Transform||activity==EditorActivity::Crop;}
 const wchar_t *hint() const{switch(activity){case EditorActivity::Browse:return L"閲覧";case EditorActivity::Ready:return L"編集";case EditorActivity::Selecting:return L"範囲を選択中 · Escで取消";case EditorActivity::Pen:return L"描画中 · 離して確定 · Escで取消";case EditorActivity::Text:return L"文字を配置 · Enterで適用 · Escで取消";case EditorActivity::Transform:return L"自由変形 · Enterで適用 · Escで取消";case EditorActivity::Crop:return L"トリミング · Enterで適用 · Escで取消";}return L"";}
};
}
