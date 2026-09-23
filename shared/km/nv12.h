#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace km {
struct Nv12View {
    std::span<const uint8_t> y, uv;
    size_t strideY=0, strideUV=0;
    int width=0, height=0;
};
struct MutableNv12View {
    std::span<uint8_t> y, uv;
    size_t strideY=0, strideUV=0;
    int width=0, height=0;
};
inline bool PlaneFits(size_t bytes, size_t stride, int width, int rows) {
    if (width <= 0 || rows <= 0 || stride < size_t(width) || bytes < size_t(width)) return false;
    return size_t(rows-1) <= (bytes-size_t(width))/stride;
}
template<class View> bool ValidNv12(const View& v) {
    return v.width > 0 && v.height > 0 && v.width <= 8192 && v.height <= 8192 &&
        !(v.width&1) && !(v.height&1) &&
        PlaneFits(v.y.size(),v.strideY,v.width,v.height) &&
        PlaneFits(v.uv.size(),v.strideUV,v.width,v.height/2);
}
inline bool FillBlack(MutableNv12View d) {
    if (!ValidNv12(d)) return false;
    for (int y=0;y<d.height;++y) std::fill_n(d.y.data()+y*d.strideY,d.width,uint8_t(16));
    for (int y=0;y<d.height/2;++y) std::fill_n(d.uv.data()+y*d.strideUV,d.width,uint8_t(128));
    return true;
}
// Reference nearest-neighbor transform. Non-overlapping source and destination.
// Video-range NV12 only. This does not convert color matrix, primaries or range.
inline bool Letterbox(Nv12View s, MutableNv12View d, int rotation) {
    if (!ValidNv12(s) || !ValidNv12(d) ||
        (rotation!=0 && rotation!=90 && rotation!=180 && rotation!=270)) return false;
    const int rw=(rotation%180)?s.height:s.width, rh=(rotation%180)?s.width:s.height;
    int fw=d.width, fh=d.height;
    if (int64_t(rw)*d.height > int64_t(rh)*d.width) fh=int(int64_t(d.width)*rh/rw)&~1;
    else fw=int(int64_t(d.height)*rw/rh)&~1;
    if (fw<2 || fh<2) return false;
    const int ox=((d.width-fw)/2)&~1, oy=((d.height-fh)/2)&~1;
    FillBlack(d);
    for (int plane=0;plane<2;++plane) {
        const int unit=plane?2:1, sw=s.width/unit, sh=s.height/unit;
        const int w=fw/unit, h=fh/unit;
        const int rotatedW=rw/unit, rotatedH=rh/unit;
        const auto src=plane?s.uv:s.y; const auto dst=plane?d.uv:d.y;
        const size_t ss=plane?s.strideUV:s.strideY, ds=plane?d.strideUV:d.strideY;
        for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
            const int rx=int(int64_t(x)*rotatedW/w), ry=int(int64_t(y)*rotatedH/h);
            int sx=rx,sy=ry;
            if (rotation==90) { sx=ry;sy=sh-1-rx; }
            else if (rotation==180) { sx=sw-1-rx;sy=sh-1-ry; }
            else if (rotation==270) { sx=sw-1-ry;sy=rx; }
            for (int c=0;c<unit;++c)
                dst[(oy/unit+y)*ds+(ox/unit+x)*unit+c]=src[sy*ss+sx*unit+c];
        }
    }
    return true;
}
} // namespace km
