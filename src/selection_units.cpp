#include "gvn/selection_units.h"
namespace gvn {
bool valid_selection_units(const SelectionUnits &u) {
    return u.start_x>=1&&u.start_x<=999&&u.start_y>=1&&u.start_y<=999&&u.size_x>=1&&u.size_x<=999&&u.size_y>=1&&u.size_y<=999;
}
Rect selection_rectangle(int x1,int y1,int x2,int y2,int width,int height,const SelectionUnits &u) {
    if(!valid_selection_units(u))throw std::invalid_argument("Selection units must be between1and999");
    if(width<=0||height<=0)return {};
    const auto axis=[](int a,int b,int extent,int start,int size) {
        a=std::clamp(a,0,extent-1);b=std::clamp(b,0,extent-1);
        const int origin=std::min(a,b)/start*start;
        const int64_t span=int64_t(std::max(a,b))-origin+1;
        const int length=int(std::min((span+size-1)/size*size,int64_t((extent-origin)/size)*size));
        return std::pair{origin,length};
    };
    const auto x=axis(x1,x2,width,u.start_x,u.size_x),y=axis(y1,y2,height,u.start_y,u.size_y);
    return x.second&&y.second?Rect{x.first,y.first,x.second,y.second}:Rect{};
}
}
