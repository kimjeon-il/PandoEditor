#include <pandoeditor/hydroformat.h>
#include <limits>
#include <stdexcept>
#include <unordered_set>

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
        if(!logical||!count||!index.logicalPacks.emplace(logical,packList(reader,count)).second)
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
}
