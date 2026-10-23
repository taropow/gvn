#include "gvn/selection_region.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace gvn {
std::vector<SelectionRegion::Span> SelectionRegion::row(int py) const {
    std::vector<Span> spans;
    if(py<y||py>=y+h)return spans;
    const auto bounds=shape_?shape_->bounds:inverse_?inverse_rect_:static_cast<const Rect &>(*this);
    if(py>=bounds.y&&py<bounds.y+bounds.h){
        if(shape_)spans=shape_->rows[size_t(py-bounds.y)];
        else if(!bounds.empty())spans.push_back({bounds.x,bounds.x+bounds.w});
    }
    if(inverse_){
        std::vector<Span> result;int start=0;
        for(auto s:spans){if(start<s.first)result.push_back({start,s.first});start=s.last;}
        if(start<canvas_width_)result.push_back({start,canvas_width_});
        spans=std::move(result);
    }
    return spans;
}
bool SelectionRegion::contains(int px,int py) const {
    if(px<x||py<y||px>=x+w||py>=y+h)return false;
    const auto bounds=shape_?shape_->bounds:inverse_?inverse_rect_:static_cast<const Rect &>(*this);
    bool inside=px>=bounds.x&&py>=bounds.y&&px<bounds.x+bounds.w&&py<bounds.y+bounds.h;
    if(inside&&shape_){const auto &spans=shape_->rows[size_t(py-bounds.y)];
        const auto it=std::upper_bound(spans.begin(),spans.end(),px,[](int value,Span s){return value<s.first;});
        inside=it!=spans.begin()&&px<(it-1)->last;}
    return inverse_?!inside:inside;
}
size_t SelectionRegion::storage_bytes() const {
    size_t result=outline_?outline_->capacity()*sizeof(SelectionLine):0;
    if(shape_){result+=shape_->rows.capacity()*sizeof(std::vector<Span>);for(const auto &r:shape_->rows)result+=r.capacity()*sizeof(Span);}
    return result;
}
void SelectionRegion::build_outline(){
    auto lines=std::make_shared<std::vector<SelectionLine>>();
    std::vector<Span> previous;std::map<int,int> vertical;
    for(int py=y;py<=y+h;++py){
        auto current=py==y+h?std::vector<Span>{}:row(py);
        std::vector<int> events;events.reserve((previous.size()+current.size())*2);
        for(const auto &spans:{&previous,&current})for(auto s:*spans){events.push_back(s.first);events.push_back(s.last);}
        std::sort(events.begin(),events.end());bool inside=false;int start{};
        for(size_t n=0;n<events.size();){const int px=events[n];size_t end=n;while(end<events.size()&&events[end]==px)++end;
            if((end-n)&1){if(inside)lines->push_back({{double(start),double(py)},{double(px),double(py)}});else start=px;inside=!inside;}n=end;}
        std::map<int,int> next;
        for(auto s:current)for(int px:{s.first,s.last}){const auto found=vertical.find(px);next[px]=found==vertical.end()?py:found->second;}
        for(auto [px,start_y]:vertical)if(!next.contains(px))lines->push_back({{double(px),double(start_y)},{double(px),double(py)}});
        vertical=std::move(next);previous=std::move(current);
        if(lines->size()>1000000)throw std::runtime_error("Selection outline exceeds limit");
    }
    outline_=std::move(lines);
}
SelectionRegion SelectionRegion::polygon(std::span<const FloatPoint> points,int width,int height){
    if(width<1||height<1||points.size()>65536)throw std::invalid_argument("Invalid selection polygon");
    if(points.size()<3)return {};
    struct Edge{int first,last;double x,step;};std::vector<Edge> edges;
    int first=height,last=0;
    for(size_t n=0;n<points.size();++n){auto a=points[n],b=points[(n+1)%points.size()];
        if(!std::isfinite(a.x)||!std::isfinite(a.y)||std::abs(a.x)>1e9||std::abs(a.y)>1e9)throw std::invalid_argument("Invalid polygon point");
        if(a.y>b.y)std::swap(a,b);if(a.y==b.y)continue;
        const int begin=std::clamp(int(std::ceil(a.y-.5)),0,height),end=std::clamp(int(std::ceil(b.y-.5)),0,height);
        if(begin>=end)continue;const double step=(b.x-a.x)/(b.y-a.y);
        edges.push_back({begin,end,a.x+(begin+.5-a.y)*step,step});first=std::min(first,begin);last=std::max(last,end);}
    if(edges.empty())return {};
    std::sort(edges.begin(),edges.end(),[](auto a,auto b){return a.first<b.first;});
    auto shape=std::make_shared<Shape>();shape->bounds={width,first,0,last-first};shape->rows.resize(size_t(last-first));
    std::vector<Edge> active;size_t next{};int min_x=width,max_x=0,min_y=height,max_y=0;size_t runs{};
    for(int py=first;py<last;++py){
        std::erase_if(active,[&](auto e){return e.last<=py;});while(next<edges.size()&&edges[next].first==py)active.push_back(edges[next++]);
        std::sort(active.begin(),active.end(),[](auto a,auto b){return a.x<b.x;});auto &spans=shape->rows[size_t(py-first)];
        for(size_t n=0;n+1<active.size();n+=2){const int left=std::clamp(int(std::ceil(active[n].x-.5)),0,width),right=std::clamp(int(std::ceil(active[n+1].x-.5)),0,width);
            if(left<right){if(!spans.empty()&&spans.back().last>=left)spans.back().last=std::max(spans.back().last,right);else spans.push_back({left,right});min_x=std::min(min_x,left);max_x=std::max(max_x,right);min_y=std::min(min_y,py);max_y=std::max(max_y,py+1);}}
        runs+=spans.size();if(runs>1000000)throw std::runtime_error("Selection complexity exceeds limit");
        for(auto &e:active)e.x+=e.step;
    }
    if(max_x<=min_x)return {};
    shape->bounds.x=min_x;shape->bounds.w=max_x-min_x;
    // Keep the scanline origin, including empty leading/trailing rows.
    SelectionRegion out{min_x,min_y,max_x-min_x,max_y-min_y};out.shape_=std::move(shape);out.build_outline();return out;
}
SelectionRegion SelectionRegion::inverted(int width,int height) const {
    if(width<1||height<1)throw std::invalid_argument("Invalid selection canvas");
    if(empty())return {0,0,width,height};
    if(rectangular()&&x==0&&y==0&&w==width&&h==height)return {};
    SelectionRegion result=*this;
    if(inverse_){result.inverse_=false;const auto bounds=shape_?shape_->bounds:inverse_rect_;static_cast<Rect &>(result)=intersect(bounds,{0,0,width,height});}
    else {result.inverse_rect_=*this;result.inverse_=true;static_cast<Rect &>(result)={0,0,width,height};}
    result.canvas_width_=width;result.canvas_height_=height;result.build_outline();return result;
}
SelectionRegion SelectionRegion::combined(const SelectionRegion &other,SelectionCombine mode,int width,int height) const {
    if(mode==SelectionCombine::Replace)return other.clipped(width,height);
    auto shape=std::make_shared<Shape>();shape->bounds={0,0,width,height};shape->rows.resize(size_t(height));
    int min_x=width,max_x=0,min_y=height,max_y=0;size_t count{};
    for(int py=0;py<height;++py){const auto a=row(py),b=other.row(py);std::vector<std::pair<int,int>> events;
        for(auto s:a){events.emplace_back(s.first,1);events.emplace_back(s.last,1);}for(auto s:b){events.emplace_back(s.first,2);events.emplace_back(s.last,2);}
        std::sort(events.begin(),events.end());int state{},start{};bool on{};auto &spans=shape->rows[size_t(py)];
        for(size_t n=0;n<events.size();){const int px=events[n].first;do{state^=events[n++].second;}while(n<events.size()&&events[n].first==px);
            const bool next=mode==SelectionCombine::Add?state!=0:mode==SelectionCombine::Subtract?state==1:state==3;
            if(on&&!next){spans.push_back({start,px});min_x=std::min(min_x,start);max_x=std::max(max_x,px);min_y=std::min(min_y,py);max_y=std::max(max_y,py+1);}else if(!on&&next)start=px;on=next;}
        count+=spans.size();if(count>1000000)throw std::runtime_error("Selection complexity exceeds limit");}
    if(max_x<=min_x)return {};
    SelectionRegion result{min_x,min_y,max_x-min_x,max_y-min_y};result.shape_=std::move(shape);result.build_outline();return result;
}
SelectionRegion SelectionRegion::clipped(int width,int height) const {
    if(rectangular())return intersect(*this,{0,0,width,height});
    if(x>=0&&y>=0&&x+w<=width&&y+h<=height&&(!inverse_||(canvas_width_==width&&canvas_height_==height)))return *this;
    return combined({0,0,width,height},SelectionCombine::Intersect,width,height);
}
Image selection_image(const Image &source,const SelectionRegion &region,const EditMemory &memory){
    const auto area=region.clipped(source.width,source.height);if(area.empty())throw std::invalid_argument("Empty selection");
    if(memory.before_allocate)memory.before_allocate(memory.pixels(area.w,area.h)*sizeof(Pixel));
    auto out=crop(source,area);
    if(!area.rectangular()){for(int py=0;py<area.h;++py)for(int px=0;px<area.w;++px)if(!area.contains(px+area.x,py+area.y))out.frames[0].pixels[size_t(py)*out.width+px]={};out.frames[0].indexed.reset();out.rgb16.reset();}
    return out;
}
Image erase_selection(const Image &source,const SelectionRegion &region,Pixel fill,const EditMemory &memory){
    const auto count=memory.pixels(source.width,source.height);if(memory.before_allocate)memory.before_allocate(count*sizeof(Pixel));auto out=source;
    const auto area=intersect(region,{0,0,int(source.width),int(source.height)});for(int py=area.y;py<area.y+area.h;++py)for(int px=area.x;px<area.x+area.w;++px)if(region.contains(px,py))out.frames[0].pixels[size_t(py)*out.width+px]=fill;
    out.frames[0].indexed.reset();out.rgb16.reset();return out;
}
void restrict_to_selection(Image &edited,const Image &original,const SelectionRegion &region,size_t original_frame){
    if(region.empty()||region.rectangular()||edited.width!=original.width||edited.height!=original.height)return;
    const auto &source=original.frames.at(original_frame).pixels;
    auto &pixels=edited.frames[0].pixels;for(int py=0;py<int(edited.height);++py)for(int px=0;px<int(edited.width);++px)if(!region.contains(px,py))pixels[size_t(py)*edited.width+px]=source[size_t(py)*edited.width+px];
    edited.frames[0].indexed.reset();edited.rgb16.reset();
}
} // namespace gvn
