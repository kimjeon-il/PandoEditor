#include <pandoeditor/hydroformat.h>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace pandoeditor {
namespace {
class Reader {
public:
    explicit Reader(HydroByteView bytes):bytes_(bytes){}
    std::uint32_t number(std::size_t length) {
        if(length>4||position_>bytes_.size||length>bytes_.size-position_)
            throw std::runtime_error("truncated hydro index");
        std::uint32_t value=0;
        for(std::size_t i=0;i<length;i++)value|=std::uint32_t(bytes_.data[position_++])<<(8*i);
        return value;
    }
    std::size_t remaining() const {return bytes_.size-position_;}
    HydroByteView take(std::size_t length) {
        if(length>remaining())throw std::runtime_error("truncated hydro record");
        const auto result=HydroByteView{bytes_.data+position_,length};
        position_+=length;return result;
    }
private:
    HydroByteView bytes_;
    std::size_t position_=0;
};
void checkCount(std::uint32_t count,std::size_t bytesEach,std::size_t remaining) {
    if(count>remaining/bytesEach)throw std::runtime_error("invalid hydro index count");
}
std::vector<std::uint32_t> packList(Reader& reader,std::uint32_t count) {
    checkCount(count,4,reader.remaining());
    std::vector<std::uint32_t> ids;
    ids.reserve(count);
    std::unordered_set<std::uint32_t> seen;
    for(std::uint32_t i=0;i<count;i++){
        auto id=reader.number(4);
        if(!seen.insert(id).second)throw std::runtime_error("duplicate hydro pack reference");
        ids.push_back(id);
    }
    return ids;
}
std::uint32_t unsignedVarint(Reader& reader) {
    std::uint32_t result=0;
    for(int shift=0;shift<=28;shift+=7){
        const auto value=reader.number(1);
        if(shift==28 && (value&0xf0))throw std::runtime_error("hydro varint overflow");
        result|=(value&0x7f)<<shift;
        if(!(value&0x80))return result;
    }
    throw std::runtime_error("hydro varint continuation overflow");
}
std::int32_t signedVarint(Reader& reader) {
    const auto value=unsignedVarint(reader);
    return static_cast<std::int32_t>(value>>1) ^ -static_cast<std::int32_t>(value&1);
}
HydroLine readLine(Reader& reader) {
    const auto count=unsignedVarint(reader);
    if(count>reader.remaining()/2)throw std::runtime_error("invalid hydro vertex count");
    HydroLine line;line.reserve(count);
    std::int64_t x=0,y=0;
    for(std::uint32_t i=0;i<count;i++){
        const auto dx=signedVarint(reader),dy=signedVarint(reader);
        x=i?x+dx:dx;y=i?y+dy:dy;
        if(x<std::numeric_limits<std::int32_t>::min()||x>std::numeric_limits<std::int32_t>::max()||
           y<std::numeric_limits<std::int32_t>::min()||y>std::numeric_limits<std::int32_t>::max())
            throw std::runtime_error("hydro coordinate overflow");
        line.push_back({static_cast<std::int32_t>(x),static_cast<std::int32_t>(y)});
    }
    return line;
}
HydroDecodedGeometry readGeometry(HydroByteView bytes,std::uint8_t kind) {
    if(kind<1||kind>4)throw std::runtime_error("invalid hydro geometry kind");
    Reader reader(bytes);HydroDecodedGeometry geometry;geometry.kind=kind;
    const auto count=unsignedVarint(reader);
    if(count==0||count>reader.remaining())throw std::runtime_error("invalid hydro geometry count");
    if((kind==1||kind==3)&&count!=1)throw std::runtime_error("invalid single hydro geometry");
    if(kind<=2){
        for(std::uint32_t i=0;i<count;i++)geometry.lines.push_back(readLine(reader));
    }else{
        for(std::uint32_t i=0;i<count;i++){
            const auto rings=unsignedVarint(reader);
            if(rings==0||rings>reader.remaining())throw std::runtime_error("invalid hydro ring count");
            HydroPolygon polygon;
            for(std::uint32_t j=0;j<rings;j++)polygon.push_back(readLine(reader));
            geometry.polygons.push_back(std::move(polygon));
        }
    }
    if(reader.remaining())throw std::runtime_error("trailing hydro geometry bytes");
    return geometry;
}
std::vector<std::vector<double>> readWidths(HydroByteView bytes,const HydroDecodedGeometry& geometry) {
    if(!bytes.size)return {};
    Reader reader(bytes);std::vector<std::vector<double>> profiles;
    const auto count=unsignedVarint(reader);
    if(count!=geometry.lines.size())throw std::runtime_error("hydro width part mismatch");
    for(const auto& line:geometry.lines){
        const auto vertices=unsignedVarint(reader);
        if(vertices!=line.size())throw std::runtime_error("hydro width vertex mismatch");
        std::vector<double> widths; widths.reserve(vertices);
        std::int64_t width=vertices?unsignedVarint(reader):0;
        for(std::uint32_t i=0;i<vertices;i++){
            if(i)width+=signedVarint(reader);
            if(width<0||width>std::numeric_limits<std::int32_t>::max())
                throw std::runtime_error("invalid hydro width");
            widths.push_back(static_cast<double>(width)/1000.0);
        }
        profiles.push_back(std::move(widths));
    }
    if(reader.remaining())throw std::runtime_error("trailing hydro width bytes");
    return profiles;
}
}
HydroIndex decodeHydroIndex(HydroByteView bytes,
                            const std::vector<std::uint64_t>& shardLengths) {
    Reader reader(bytes);
    if(reader.number(4)!=0x34495741||reader.number(2)!=4||reader.number(2)!=0)
        throw std::runtime_error("invalid hydro index header");
    const auto tileCount=reader.number(4),logicalCount=reader.number(4),packCount=reader.number(4);
    HydroIndex index;
    checkCount(tileCount,7,reader.remaining());
    for(std::uint32_t i=0;i<tileCount;i++){
        HydroTileKey key;
        key.stage=static_cast<std::uint8_t>(reader.number(1));
        key.x=static_cast<std::uint16_t>(reader.number(2));
        key.y=static_cast<std::uint16_t>(reader.number(2));
        const auto count=reader.number(2);
        if(key.stage>=4||count==0||!index.tilePacks.emplace(key,packList(reader,count)).second)
            throw std::runtime_error("invalid hydro tile");
    }
    checkCount(logicalCount,6,reader.remaining());
    for(std::uint32_t i=0;i<logicalCount;i++){
        const auto logical=reader.number(4),count=reader.number(2);
        if(!count||!index.logicalPacks.emplace(logical,packList(reader,count)).second)
            throw std::runtime_error("invalid hydro logical feature");
    }
    checkCount(packCount,15,reader.remaining());
    for(std::uint32_t i=0;i<packCount;i++){
        HydroPackSpec spec;
        spec.id=reader.number(4);
        spec.shard=static_cast<std::uint16_t>(reader.number(2));
        spec.offset=reader.number(4);
        spec.length=reader.number(4);
        spec.stage=static_cast<std::uint8_t>(reader.number(1));
        if(spec.shard>=shardLengths.size()||spec.stage>=4||!spec.length||
           std::uint64_t(spec.offset)+spec.length>shardLengths[spec.shard]||
           !index.packSpecs.emplace(spec.id,spec).second)
            throw std::runtime_error("invalid hydro pack specification");
    }
    if(reader.remaining())throw std::runtime_error("trailing hydro index bytes");
    for(const auto& [tile,ids]:index.tilePacks)for(const auto id:ids){
        const auto pack=index.packSpecs.find(id);
        if(pack==index.packSpecs.end()||pack->second.stage!=tile.stage)
            throw std::runtime_error("invalid hydro tile pack reference");
    }
    for(const auto& [logical,ids]:index.logicalPacks)for(const auto id:ids)
        if(!index.packSpecs.count(id))throw std::runtime_error("invalid hydro logical pack reference");
    return index;
}
HydroPack decodeHydroPack(HydroByteView bytes,std::uint32_t packId,
                          const std::map<std::uint32_t,std::uint32_t>& metadataLogicalIds) {
    Reader reader(bytes);
    if(reader.number(4)!=0x46485741||reader.number(2)!=4)
        throw std::runtime_error("invalid hydro pack header");
    HydroPack pack;pack.stage=static_cast<std::uint8_t>(reader.number(2));
    if(pack.stage>=4)throw std::runtime_error("invalid hydro pack stage");
    const auto count=reader.number(4);
    checkCount(count,44,reader.remaining());
    pack.features.reserve(count);
    std::unordered_set<std::uint32_t> seen;
    for(std::uint32_t i=0;i<count;i++){
        HydroPhysicalFeature feature;
        feature.fid=reader.number(4);feature.logicalFid=reader.number(4);
        feature.kind=static_cast<std::uint8_t>(reader.number(1));
        feature.stage=static_cast<std::uint8_t>(reader.number(1));
        const auto geometryKind=static_cast<std::uint8_t>(reader.number(1));
        feature.flags=static_cast<std::uint8_t>(reader.number(1));
        feature.fragmentIndex=static_cast<std::uint16_t>(reader.number(2));
        feature.fragmentCount=static_cast<std::uint16_t>(reader.number(2));
        const auto bits=reader.number(4);
        std::memcpy(&feature.strokeWidth,&bits,sizeof(bits));
        for(auto& coordinate:feature.bounds)coordinate=static_cast<std::int32_t>(reader.number(4));
        const auto geometryLength=reader.number(4),widthLength=reader.number(4);
        const auto logical=metadataLogicalIds.find(feature.fid);
        if(logical==metadataLogicalIds.end()||logical->second!=feature.logicalFid||
           !seen.insert(feature.fid).second||
           feature.stage!=pack.stage||(feature.kind!=1&&feature.kind!=2)||
           !feature.fragmentCount||feature.fragmentIndex>=feature.fragmentCount||
           !std::isfinite(feature.strokeWidth)||feature.strokeWidth<0||
           geometryLength>reader.remaining())throw std::runtime_error("invalid hydro feature record");
        feature.geometry=readGeometry(reader.take(geometryLength),geometryKind);
        if(widthLength>reader.remaining()||
           (feature.kind==1 && geometryKind>2)||
           (feature.kind==2 && geometryKind<3)||
           (feature.kind==2 && widthLength))throw std::runtime_error("invalid hydro feature geometry");
        const auto widthBytes=reader.take(widthLength);
        if(feature.kind==1)feature.widths=readWidths(widthBytes,feature.geometry);
        pack.features.push_back(std::move(feature));
    }
    if(reader.remaining())throw std::runtime_error("trailing hydro pack bytes "+std::to_string(packId));
    return pack;
}
HydroRenderPacket buildHydroRenderPacket(const HydroPack& pack) {
    HydroRenderPacket result;
    for(const auto& feature:pack.features){
        if(feature.kind==2){
            result.lakes.push_back({feature.fid,feature.logicalFid,feature.geometry.polygons});
            continue;
        }
        for(std::size_t part=0;part<feature.geometry.lines.size();part++){
            const auto& line=feature.geometry.lines[part];
            const auto* widths=part<feature.widths.size()?&feature.widths[part]:nullptr;
            for(std::size_t vertex=0;vertex+1<line.size();vertex++){
                const double start=widths&&vertex<widths->size()?(*widths)[vertex]:feature.strokeWidth;
                const double end=widths&&vertex+1<widths->size()?(*widths)[vertex+1]:start;
                result.rivers.push_back({feature.fid,feature.logicalFid,line[vertex],line[vertex+1],
                    start,end,bool(feature.flags&1)});
            }
        }
    }
    return result;
}
Geometry mergeHydroLogicalFragments(std::vector<HydroPhysicalFeature> fragments) {
    if(fragments.empty())throw std::runtime_error("empty hydro logical feature");
    std::sort(fragments.begin(),fragments.end(),[](const auto& a,const auto& b){return a.fragmentIndex<b.fragmentIndex;});
    const auto logical=fragments.front().logicalFid;
    const auto count=fragments.front().fragmentCount;
    if(count!=fragments.size())throw std::runtime_error("incomplete hydro logical feature");
    Geometry merged;
    const auto kind=fragments.front().kind;
    // Match the web decoder operation, not reciprocal multiplication: the latter
    // differs by an ULP for many microdegrees and changes river graph identities.
    auto point=[](HydroPoint p){return Point{p.longitude/1000000.0,p.latitude/1000000.0};};
    for(std::size_t i=0;i<fragments.size();i++){
        const auto& fragment=fragments[i];
        if(fragment.logicalFid!=logical||fragment.fragmentCount!=count||fragment.fragmentIndex!=i||fragment.kind!=kind)
            throw std::runtime_error("inconsistent hydro logical fragments");
        for(const auto& source:fragment.geometry.lines){
            Ring line;line.reserve(source.size());for(const auto p:source)line.push_back(point(p));
            // The web merge keeps each physical fragment as a separate part,
            // even when adjacent endpoints coincide.
            merged.lines.push_back(std::move(line));
        }
        for(const auto& source:fragment.geometry.polygons){
            Polygon polygon;
            for(const auto& sourceRing:source){
                Ring ring;ring.reserve(sourceRing.size());
                for(const auto p:sourceRing)ring.push_back(point(p));
                polygon.push_back(std::move(ring));
            }
            merged.polygons.push_back(std::move(polygon));
        }
    }
    merged.type=kind==1?(merged.lines.size()==1?"LineString":"MultiLineString"):
        (merged.polygons.size()==1?"Polygon":"MultiPolygon");
    return merged;
}
}
