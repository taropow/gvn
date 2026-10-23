#include "gvn/edit.h"
#include <array>
#include <numeric>

namespace gvn {
Image cp_image(const Image &image,std::wstring_view input,Rect rect,size_t frame) {
    const auto &source=image.frames.at(frame);
    if(source.pixels.size()!=checked_image_pixels(image.width,image.height))
        throw std::runtime_error("Invalid CP source pixel count");
    const auto code=normalize_cp_code(input);
    if(rect.empty())rect={0,0,static_cast<int>(image.width),static_cast<int>(image.height)};
    rect=intersect(rect,{0,0,static_cast<int>(image.width),static_cast<int>(image.height)});
    const auto columns=static_cast<uint32_t>(std::max(0,rect.w)/8),rows=static_cast<uint32_t>(std::max(0,rect.h)/8);
    const uint32_t count=columns*rows;
    if(code.empty()||!count)return still_frame(image,frame);
    // Each coefficient is recovered independently from the actual A..Z 323-block results.
    // Provenance: tests/fixtures/cp-reference and docs/verification/cp-reference-0.45.
    constexpr uint32_t coefficients[]{16,23,19,21,9,8,10,20,6,5,22,2,13,3,1,4,25,12,15,14,18,7,11,24,17,26};
    std::vector<uint32_t> order(count);
    {
        // Successor/predecessor sets find the next free slot without scanning occupied runs.
        // These two workspaces are released before allocating the full output image.
        std::vector<uint32_t> next(static_cast<size_t>(count)+1),previous(next.size());
        std::iota(next.begin(),next.end(),0u);std::iota(previous.begin(),previous.end(),0u);
        const auto root=[](auto &parents,uint32_t position) {
            while(parents[position]!=position){parents[position]=parents[parents[position]];position=parents[position];}
            return position;
        };
        uint32_t cursor=count-1;
        for(uint32_t ticket=0;ticket<count;++ticket) {
            const auto step=coefficients[code[ticket%code.size()]-L'A']+code.size()+count%code.size()+ticket;
            const auto requested=static_cast<uint32_t>((cursor+static_cast<uint64_t>(step))%count);
            if(ticket&1) {
                auto found=root(previous,requested+1);if(!found)found=root(previous,count);cursor=found-1;
            }else {
                cursor=root(next,requested);if(cursor==count)cursor=root(next,0);
            }
            order[cursor]=ticket;
            next[cursor]=root(next,cursor+1);previous[cursor+1]=root(previous,cursor);
        }
    }
    auto out=still_frame(image,frame,false);
    const auto color=[](Pixel pixel){return Pixel{static_cast<uint8_t>(255-pixel.g),static_cast<uint8_t>(255-pixel.r),static_cast<uint8_t>(255-pixel.b),pixel.a};};
    std::shared_ptr<IndexedPixels> indexed;
    const bool full=rect.x==0&&rect.y==0&&columns*8==image.width&&rows*8==image.height;
    if(full&&source.indexed&&source.indexed->width==image.width&&source.indexed->height==image.height&&source.indexed->matches(source.pixels)) {
        indexed=std::make_shared<IndexedPixels>(*source.indexed);
        for(auto &entry:indexed->palette)entry=color(entry);
    }
    for(uint32_t position=0;position<count;++position) {
        const auto target=order[position],origin=order[count-position-1];const bool transpose=(target^origin)&1;
        const auto tx=rect.x+target%columns*8,ty=rect.y+target/columns*8;
        const auto sx=rect.x+origin%columns*8,sy=rect.y+origin/columns*8;
        for(uint32_t y=0;y<8;++y)for(uint32_t x=0;x<8;++x) {
            const auto xx=sx+(transpose?y:x),yy=sy+(transpose?x:y);
            out.frames[0].pixels[static_cast<size_t>(ty+y)*image.width+tx+x]=color(source.pixels[static_cast<size_t>(yy)*image.width+xx]);
            if(indexed)indexed->set_index(tx+x,ty+y,source.indexed->index(xx,yy));
        }
    }
    out.frames[0].indexed=std::move(indexed);return out;
}
namespace {
Image meko_mask(const Image &image, Rect rect, bool reverse) {
    const int columns=rect.w/16, rows=rect.h/16;
    const size_t count=static_cast<size_t>(columns)*rows;
    if (!count) return still_frame(image,0);
    if (count>9215) throw std::runtime_error("Meko mask supports at most 9215 complete 16x16 blocks");
    // Read-only mathematical permutation measured from the official9215-block result.
    // Filtering this order preserves its exact duplicate-random-value ordering at every size.
    static constexpr uint16_t order[]{
#include "meko_order.inc"
    };
    static_assert(std::size(order)==9215);
    std::vector<uint16_t> permutation;permutation.reserve(count);
    for (const auto index:order) if (index<count) permutation.push_back(index);
    auto out=still_frame(image,0,false);
    const auto &original=image.frames.at(0);
    std::shared_ptr<IndexedPixels> indexed;
    const auto invert=[](Pixel p) {p.r^=255;p.g^=255;p.b^=255;return p;};
    const bool full=rect.x==0 && rect.y==0 && columns*16==static_cast<int>(image.width) &&
                    rows*16==static_cast<int>(image.height);
    if (full && original.indexed && original.indexed->width==image.width &&
        original.indexed->height==image.height && original.indexed->matches(original.pixels)) {
        indexed=std::make_shared<IndexedPixels>(*original.indexed);
        for (auto &color:indexed->palette) color=invert(color);
    }
    for (size_t n=0;n<count;++n) {
        const auto from=reverse?n:permutation[n], to=reverse?permutation[n]:n;
        const auto sx=rect.x+from%columns*16, sy=rect.y+from/columns*16;
        const auto tx=rect.x+to%columns*16, ty=rect.y+to/columns*16;
        for (unsigned y=0;y<16;++y) for (unsigned x=0;x<16;++x) {
            out.frames[0].pixels[(ty+y)*image.width+tx+x]=invert(original.pixels[(sy+y)*image.width+sx+x]);
            if (indexed) indexed->set_index(static_cast<uint32_t>(tx+x),static_cast<uint32_t>(ty+y),
                original.indexed->index(static_cast<uint32_t>(sx+x),static_cast<uint32_t>(sy+y)));
        }
    }
    out.frames[0].indexed=std::move(indexed);return out;
}
Image fl_mask(const Image &image, Rect rect, bool negative) {
    const int columns = rect.w / 8, rows = rect.h / 8;
    if (!columns || !rows) return still_frame(image, 0);
    auto out = still_frame(image, 0, false);
    const auto &original = image.frames.at(0);
    std::shared_ptr<IndexedPixels> indexed;
    if (!negative && original.indexed && original.indexed->width == image.width &&
        original.indexed->height == image.height && original.indexed->matches(original.pixels))
        indexed = std::make_shared<IndexedPixels>(*original.indexed);
    // Store block origins only. A full-size intermediate image per operation is unnecessary.
    std::vector<size_t> order;
    order.reserve(static_cast<size_t>(columns) * rows);
    const auto append = [&](int x, int y) {
        order.push_back(static_cast<size_t>(rect.y + y * 8) * image.width + rect.x + x * 8);
    };
    int left = 0, right = columns - 1, top = 0, bottom = rows - 1;
    while (left <= right && top <= bottom) {
        for (int x = left; x <= right; ++x) append(x, bottom);
        --bottom;
        for (int y = bottom; y >= top; --y) append(right, y);
        --right;
        if (top <= bottom) {
            for (int x = right; x >= left; --x) append(x, top);
            ++top;
        }
        if (left <= right) {
            for (int y = top; y <= bottom; ++y) append(left, y);
            ++left;
        }
    }
    const auto color = [&](Pixel pixel) {
        if (negative) { pixel.r ^= 255; pixel.g ^= 255; pixel.b ^= 255; }
        return pixel;
    };
    for (size_t n = 0; n < order.size() / 2; ++n) {
        const auto a = order[n], b = order[order.size() - 1 - n];
        for (size_t y = 0; y < 8; ++y) for (size_t x = 0; x < 8; ++x) {
            const auto ai = a + y * image.width + x, bi = b + y * image.width + x;
            out.frames[0].pixels[ai] = color(original.pixels[bi]);
            out.frames[0].pixels[bi] = color(original.pixels[ai]);
            if (indexed) {
                const auto ax = static_cast<uint32_t>(ai % image.width), ay = static_cast<uint32_t>(ai / image.width);
                const auto bx = static_cast<uint32_t>(bi % image.width), by = static_cast<uint32_t>(bi / image.width);
                indexed->set_index(ax, ay, original.indexed->index(bx, by));
                indexed->set_index(bx, by, original.indexed->index(ax, ay));
            }
        }
    }
    // The middle block, incomplete edges and pixels outside the selection stay unchanged.
    out.frames[0].indexed = std::move(indexed);
    return out;
}
} // namespace
Image rainbow_image(const Image &image, int size, Rect rect, size_t frame) {
    if (size<1 || size>32 || (size&(size-1))) throw std::runtime_error("Invalid Rainbow block size");
    if (rect.empty()) rect={0,0,static_cast<int>(image.width),static_cast<int>(image.height)};
    rect=intersect(rect,{0,0,static_cast<int>(image.width),static_cast<int>(image.height)});
    const int width=rect.w/size*size,height=rect.h/size*size;
    if (width<=0 || height<=0) return still_frame(image,frame);
    static constexpr auto colors=[] {
        std::array<Pixel,48> result{};
        for (int i=0;i<48;++i) {
            const auto up=static_cast<uint8_t>((i%8)*32),down=static_cast<uint8_t>(std::min(255,(8-i%8)*32));
            switch (i/8) {
            case 0:result[i]={255,up,0,0};break;
            case 1:result[i]={down,255,0,0};break;
            case 2:result[i]={0,255,up,0};break;
            case 3:result[i]={0,down,255,0};break;
            case 4:result[i]={up,0,255,0};break;
            case 5:result[i]={255,0,down,0};break;
            }
        }
        return result;
    }();
    auto out=still_frame(image,frame,false);
    for (int by=0;by<height;by+=size) for (int bx=0;bx<width;bx+=size) {
        const auto color=colors[((bx/size+by/size)*std::min(size,8))%48];
        for (int y=0;y<size;++y) for (int x=0;x<size;++x) {
            auto &pixel=out.frames[0].pixels[static_cast<size_t>(rect.y+by+y)*image.width+rect.x+bx+x];
            pixel.r^=color.r;pixel.g^=color.g;pixel.b^=color.b;
        }
    }
    return out;
}
Image mask_image(const Image &image, Mask operation, const MaskOptions &options, Rect rect) {
    if (operation < Mask::Vertical || operation > Mask::Rainbow)
        throw std::runtime_error("Invalid mask operation");
    const bool q0 = operation == Mask::Q0;
    const bool vertical = operation == Mask::Vertical || (q0 && (options.q0_flags & 1));
    const bool horizontal = operation == Mask::Horizontal || (q0 && (options.q0_flags & 2));
    const bool flip_vertical = q0 && (options.q0_flags & 16),
               flip_horizontal = q0 && (options.q0_flags & 32);
    if ((vertical || horizontal || q0) && (options.glass_size < 1 || options.glass_size > 999))
        throw std::runtime_error("Invalid glass size");
    if ((q0 && options.q0_flags > 63) ||
        ((operation == Mask::Negative || q0) && options.negative_channels > 7) ||
        ((operation == Mask::Xor || q0) && options.xor_channels > 7) ||
        (operation == Mask::Swap && (options.swap < 0 || options.swap > 4)))
        throw std::runtime_error("Invalid mask options");
    if (rect.empty())
        rect = {0, 0, static_cast<int>(image.width), static_cast<int>(image.height)};
    rect = intersect(rect, {0, 0, static_cast<int>(image.width), static_cast<int>(image.height)});
    if (vertical && flip_horizontal) rect.w = rect.w / options.glass_size * options.glass_size;
    if (horizontal && flip_vertical) rect.h = rect.h / options.glass_size * options.glass_size;
    if (rect.empty()) return still_frame(image, 0);
    if (operation == Mask::FL) return fl_mask(image, rect, options.fl_negative);
    if (operation == Mask::MekoForward || operation == Mask::MekoReverse)
        return meko_mask(image,rect,operation==Mask::MekoReverse);
    if (operation == Mask::Rainbow) return rainbow_image(image,options.rainbow_size,rect);
    const unsigned negative = operation == Mask::Negative || (q0 && (options.q0_flags & 4))
                                  ? options.negative_channels : 0;
    const unsigned xor_channels = operation == Mask::Xor || (q0 && (options.q0_flags & 8))
                                      ? options.xor_channels : 0;
    const auto transform = [&](Pixel pixel) {
        if (operation == Mask::Swap) {
            const std::array<uint8_t, 3> values{pixel.r, pixel.g, pixel.b};
            constexpr int permutations[5][3]{{1,0,2},{2,1,0},{0,2,1},{2,0,1},{1,2,0}};
            const auto &p = permutations[options.swap];
            return Pixel{values[p[0]], values[p[1]], values[p[2]], pixel.a};
        }
        pixel.r ^= ((negative & 1) ? 255 : 0) ^ ((xor_channels & 1) ? 128 : 0);
        pixel.g ^= ((negative & 2) ? 255 : 0) ^ ((xor_channels & 2) ? 128 : 0);
        pixel.b ^= ((negative & 4) ? 255 : 0) ^ ((xor_channels & 4) ? 128 : 0);
        return pixel;
    };
    auto out = still_frame(image, 0, false);
    const auto &original = image.frames.at(0);
    std::shared_ptr<IndexedPixels> indexed;
    const bool color = negative || xor_channels || operation == Mask::Swap;
    const bool full = rect.x == 0 && rect.y == 0 && rect.w == static_cast<int>(image.width) &&
                      rect.h == static_cast<int>(image.height);
    // A partial color change needs both old and transformed colors, so it becomes RGBA.
    // Whole-image color changes keep source indices and transform the palette once.
    if ((!color || full) && original.indexed && original.indexed->width == image.width &&
        original.indexed->height == image.height && original.indexed->matches(original.pixels)) {
        indexed = std::make_shared<IndexedPixels>(*original.indexed);
        if (color) for (auto &entry : indexed->palette) entry = transform(entry);
    }
    const auto mirror = [&](int position, int length) {
        const int start = position / options.glass_size * options.glass_size;
        const int span = std::min(options.glass_size, length - start);
        return span == options.glass_size || options.glass_remainder
                   ? start + span - 1 - (position - start) : position;
    };
    constexpr int win[]{12,8,6,15,9,13,2,11,1,4,14,7,0,5,10,3};
    for (int y = 0; y < rect.h; ++y)
        for (int x = 0; x < rect.w; ++x) {
            int sx = flip_horizontal ? rect.w - 1 - x : x;
            int sy = flip_vertical ? rect.h - 1 - y : y;
            if (vertical) sx = mirror(sx, rect.w);
            if (horizontal) sy = mirror(sy, rect.h);
            if (operation == Mask::Win && x / 16 * 16 + 16 <= rect.w)
                sx = x / 16 * 16 + win[x % 16];
            const auto source = static_cast<size_t>(sy + rect.y) * image.width + sx + rect.x;
            const auto target = static_cast<size_t>(y + rect.y) * image.width + x + rect.x;
            out.frames[0].pixels[target] = transform(original.pixels[source]);
            if (indexed) indexed->set_index(x + rect.x, y + rect.y,
                                           original.indexed->index(sx + rect.x, sy + rect.y));
        }
    out.frames[0].indexed = std::move(indexed);
    return out;
}
} // namespace gvn
