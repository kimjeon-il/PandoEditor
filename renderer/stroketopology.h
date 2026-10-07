#pragma once
#include <array>
#include <vector>
#include <cmath>
#include <cstddef>

namespace stroke {
using Point=std::array<float,2>;
struct Segment {Point previous,start,end,next;float phase=0;unsigned flags=0;std::size_t input=0;};
// Fixed Web gpu-stroke-geometry.js at ebcfae4. Each native draw owns one
// logical object; world batches call this separately for every country slice.
inline bool equal(Point a,Point b,double epsilon=1e-9) {
    return std::abs(double(a[0])-b[0])<=epsilon&&std::abs(double(a[1])-b[1])<=epsilon;
}
inline std::vector<Segment> chains(const std::vector<float>& values) {
    std::vector<Segment> valid;double phase=0;
    for(std::size_t i=0;i+3<values.size();i+=4) {
        Point a{values[i],values[i+1]},b{values[i+2],values[i+3]};
        if(!std::isfinite(a[0])||!std::isfinite(a[1])||!std::isfinite(b[0])||!std::isfinite(b[1])||equal(a,b,1e-12))continue;
        if(valid.empty()||!equal(valid.back().end,a))phase=0;
        valid.push_back({a,a,b,b,float(phase),0,i/4});
        phase+=std::hypot(double(b[0])-a[0],double(b[1])-a[1]);
    }
    for(std::size_t begin=0;begin<valid.size();) {
        std::size_t end=begin+1;while(end<valid.size()&&equal(valid[end-1].end,valid[end].start))++end;
        const bool closed=end-begin>1&&equal(valid[begin].start,valid[end-1].end);
        for(std::size_t i=begin;i<end;++i) {
            auto& s=valid[i];const bool prev=i>begin||closed,next=i+1<end||closed;
            s.previous=prev?valid[i>begin?i-1:end-1].start:s.start;
            s.next=next?valid[i+1<end?i+1:begin].end:s.end;
            s.flags=(prev?1:4)|(next?2:8)|(closed?16:0);
        }
        begin=end;
    }
    return valid;
}
}
