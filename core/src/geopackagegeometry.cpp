#include <pandoeditor/geopackagegeometry.h>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace pandoeditor {
namespace {
void require(bool okay,const char* message) {
    if(!okay)throw std::invalid_argument(message);
}
struct Reader {
    const std::vector<std::uint8_t>& bytes;
    std::size_t pos=0;
    void need(std::size_t length) const {
        require(length<=bytes.size()-pos,"INVALID_GPKG_GEOMETRY: truncated");
    }
    std::uint8_t byte(){need(1);return bytes[pos++];}
    std::uint32_t u32(bool little) {
        need(4);std::uint32_t result=0;
        for(int i=0;i<4;++i)result|=std::uint32_t(bytes[pos++])<<(little?8*i:8*(3-i));
        return result;
    }
    double number(bool little) {
        need(8);std::uint64_t bits=0;
        for(int i=0;i<8;++i)bits|=std::uint64_t(bytes[pos++])<<(little?8*i:8*(7-i));
        double value=0;std::memcpy(&value,&bits,sizeof(value));
        require(std::isfinite(value),"INVALID_GPKG_GEOMETRY: nonfinite coordinate");
        return value;
    }
    std::uint32_t count(std::size_t minimumBytes,bool little) {
        const auto n=u32(little);
        require(n<=1000000 && n<=(bytes.size()-pos)/minimumBytes,
                "INVALID_GPKG_GEOMETRY: count out of bounds");
        return n;
    }
};
Point point(Reader& r,bool little) {return {r.number(little),r.number(little)};}
Ring ring(Reader& r,bool little) {
    Ring result;const auto n=r.count(16,little);result.reserve(n);
    for(std::uint32_t i=0;i<n;++i)result.push_back(point(r,little));
    return result;
}
Geometry wkb(Reader& r,unsigned depth=0) {
    require(depth<=2,"INVALID_GPKG_GEOMETRY: nesting");
    const auto order=r.byte();require(order<=1,"INVALID_GPKG_GEOMETRY: byte order");
    const bool little=order==1;
    const auto type=r.u32(little);
    Geometry result;
    if(type==1){result.type="Point";result.points.push_back(point(r,little));}
    else if(type==2){result.type="LineString";result.lines.push_back(ring(r,little));}
    else if(type==3) {
        result.type="Polygon";Polygon polygon;
        const auto n=r.count(4,little);polygon.reserve(n);
        for(std::uint32_t i=0;i<n;++i)polygon.push_back(ring(r,little));
        result.polygons.push_back(std::move(polygon));
    } else if(type==4||type==5||type==6) {
        result.type=type==4?"MultiPoint":type==5?"MultiLineString":"MultiPolygon";
        const auto n=r.count(5,little);
        for(std::uint32_t i=0;i<n;++i) {
            auto part=wkb(r,depth+1);
            if(type==4&&part.type=="Point")result.points.push_back(part.points.front());
            else if(type==5&&part.type=="LineString")result.lines.push_back(std::move(part.lines.front()));
            else if(type==6&&part.type=="Polygon")result.polygons.push_back(std::move(part.polygons.front()));
            else throw std::invalid_argument("INVALID_GPKG_GEOMETRY: nested type");
        }
    } else throw std::invalid_argument("UNSUPPORTED_GPKG_GEOMETRY_TYPE");
    return result;
}
struct Writer {
    std::vector<std::uint8_t> bytes;
    void byte(std::uint8_t value){bytes.push_back(value);}
    void u32(std::uint32_t value) {
        for(int i=0;i<4;++i)byte(std::uint8_t(value>>(8*i)));
    }
    void count(std::size_t n) {
        require(n<=1000000&&n<=std::numeric_limits<std::uint32_t>::max(),
                "INVALID_GPKG_GEOMETRY: too many items");
        u32(std::uint32_t(n));
    }
    void number(double value) {
        require(std::isfinite(value),"INVALID_GPKG_GEOMETRY: nonfinite coordinate");
        std::uint64_t bits=0;std::memcpy(&bits,&value,sizeof(bits));
        for(int i=0;i<8;++i)byte(std::uint8_t(bits>>(8*i)));
    }
    void point(Point p){number(p.x);number(p.y);}
    void ring(const Ring& points) {
        count(points.size());for(const auto& p:points)point(p);
    }
    void polygon(const Polygon& rings) {
        count(rings.size());for(const auto& item:rings)ring(item);
    }
    void wkb(const Geometry& geometry) {
        byte(1);
        if(geometry.type=="Point") {u32(1);point(geometry.points.front());}
        else if(geometry.type=="LineString") {u32(2);ring(geometry.lines.front());}
        else if(geometry.type=="Polygon") {u32(3);polygon(geometry.polygons.front());}
        else if(geometry.type=="MultiPoint") {
            u32(4);count(geometry.points.size());
            for(const auto& p:geometry.points){byte(1);u32(1);point(p);}
        } else if(geometry.type=="MultiLineString") {
            u32(5);count(geometry.lines.size());
            for(const auto& line:geometry.lines){byte(1);u32(2);ring(line);}
        } else if(geometry.type=="MultiPolygon") {
            u32(6);count(geometry.polygons.size());
            for(const auto& poly:geometry.polygons){byte(1);u32(3);polygon(poly);}
        } else throw std::invalid_argument("UNSUPPORTED_GPKG_GEOMETRY_TYPE");
    }
};
}
std::vector<std::uint8_t> encodeGeoPackageGeometry(const Geometry& geometry) {
    GeometryStore validation;validation.insert({"gpkg-export",1},geometry);
    Writer w;w.byte('G');w.byte('P');w.byte(0);w.byte(1);w.u32(4326);
    w.wkb(geometry);return std::move(w.bytes);
}
Geometry decodeGeoPackageGeometry(const std::vector<std::uint8_t>& bytes) {
    Reader r{bytes};r.need(8);
    require(r.byte()=='G'&&r.byte()=='P'&&r.byte()==0,"INVALID_GPKG_GEOMETRY: header");
    const auto flags=r.byte();
    require((flags&0xf0)==0,"UNSUPPORTED_GPKG_GEOMETRY_FLAGS");
    const auto envelope=(flags>>1)&7;
    require(envelope<=1,"UNSUPPORTED_GPKG_GEOMETRY_ENVELOPE");
    const bool little=(flags&1)!=0;
    require(r.u32(little)==4326,"UNSUPPORTED_GPKG_CRS");
    if(envelope)for(int i=0;i<4;++i)(void)r.number(little);
    auto result=wkb(r);
    require(r.pos==bytes.size(),"INVALID_GPKG_GEOMETRY: trailing bytes");
    GeometryStore validation;validation.insert({"gpkg-import",1},result);
    return result;
}
}
