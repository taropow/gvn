#include "gvn/color_count.h"
#include <queue>
namespace gvn {
std::optional<uint64_t> count_image_colors(std::span<const Pixel> pixels,std::stop_token stop,const ColorCountMemory &memory) {
    constexpr size_t words=(1u<<24)/64,bitmap_bytes=words*sizeof(uint64_t),chunk=65536;
    if(stop.stop_requested())return {};
    if(pixels.empty())return 0;
    const auto allocate=[&](size_t bytes,uint64_t total){if(total>memory.bytes)throw std::runtime_error("Color count exceeds the memory limit");if(memory.before_allocate)memory.before_allocate(bytes);};
    allocate(bitmap_bytes,bitmap_bytes);std::vector<uint64_t> bitmap(words);uint64_t count{};size_t partial{};bool transparent{};
    for(size_t base=0;base<pixels.size();base+=4096) {
        if(stop.stop_requested())return {};
        for(size_t at=base;at<std::min(base+4096,pixels.size());++at) {
            const auto p=pixels[at];if(!p.a){transparent=true;continue;}if(p.a!=255){++partial;continue;}
            const auto key=(uint32_t(p.r)<<16)|(uint32_t(p.g)<<8)|p.b;auto &word=bitmap[key/64];const auto bit=uint64_t(1)<<(key%64);
            if(!(word&bit)){word|=bit;++count;}
        }
    }
    count+=transparent;if(!partial)return count;
    if(partial>SIZE_MAX/sizeof(uint32_t))throw std::bad_alloc();
    const auto chunks=(partial+chunk-1)/chunk;
    struct Node {uint32_t value;size_t at,end;bool operator<(const Node &other)const{return value>other.value;}};
    const uint64_t total=bitmap_bytes+uint64_t(partial)*sizeof(uint32_t)+uint64_t(chunks)*sizeof(Node);
    allocate(partial*sizeof(uint32_t),total);std::vector<uint32_t> keys(partial);size_t used{};
    for(size_t base=0;base<pixels.size();base+=4096) {
        if(stop.stop_requested())return {};
        for(size_t at=base;at<std::min(base+4096,pixels.size());++at) {
            const auto p=pixels[at];if(!p.a||p.a==255)continue;
            const auto premultiply=[&](uint8_t v){return (unsigned(v)*p.a+127)/255;};
            keys[used++]=(uint32_t(p.a)<<24)|(premultiply(p.r)<<16)|(premultiply(p.g)<<8)|premultiply(p.b);
        }
    }
    std::vector<Node> nodes;allocate(chunks*sizeof(Node),total);nodes.reserve(chunks);
    for(size_t base=0;base<keys.size();base+=chunk) {
        if(stop.stop_requested())return {};
        const auto end=std::min(base+chunk,keys.size());std::sort(keys.begin()+base,keys.begin()+end);nodes.push_back({keys[base],base,end});
    }
    std::priority_queue<Node> queue(std::less<Node>{},std::move(nodes));std::optional<uint32_t> previous;size_t merged{};
    while(!queue.empty()) {
        if(!(merged++%4096)&&stop.stop_requested())return {};
        auto node=queue.top();queue.pop();if(!previous||*previous!=node.value){++count;previous=node.value;}
        if(++node.at<node.end){node.value=keys[node.at];queue.push(node);}
    }
    return count;
}
}
