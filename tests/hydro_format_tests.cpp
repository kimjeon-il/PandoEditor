#include <pandoeditor/hydroformat.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <vector>
#include <zlib.h>

#ifndef WEB_HYDRO_FIXTURE
#error missing fixture path
#endif

namespace {
std::vector<std::uint8_t> fixture() {
    std::ifstream file(std::string(WEB_HYDRO_FIXTURE)+"/v0.13.0/index.bin.gz",std::ios::binary);
    assert(file);
    std::vector<std::uint8_t> compressed(std::istreambuf_iterator<char>{file},{});
    z_stream stream{};
    stream.next_in=compressed.data();stream.avail_in=static_cast<uInt>(compressed.size());
    assert(inflateInit2(&stream,MAX_WBITS+16)==Z_OK);
    std::vector<std::uint8_t> bytes(4096);
    stream.next_out=bytes.data();stream.avail_out=static_cast<uInt>(bytes.size());
    assert(inflate(&stream,Z_FINISH)==Z_STREAM_END);
    bytes.resize(stream.total_out);inflateEnd(&stream);
    return bytes;
}
std::vector<std::uint8_t> pack(const pandoeditor::HydroPackSpec& spec) {
    std::ifstream file(std::string(WEB_HYDRO_FIXTURE)+"/v0.13.0/shards/s0.bin",std::ios::binary);
    assert(file);file.seekg(spec.offset);
    std::vector<std::uint8_t> compressed(spec.length);
    file.read(reinterpret_cast<char*>(compressed.data()),spec.length);
    assert(file.gcount()==spec.length);
    z_stream stream{};
    stream.next_in=compressed.data();stream.avail_in=static_cast<uInt>(compressed.size());
    assert(inflateInit2(&stream,MAX_WBITS+16)==Z_OK);
    std::vector<std::uint8_t> bytes(4096);
    stream.next_out=bytes.data();stream.avail_out=static_cast<uInt>(bytes.size());
    assert(inflate(&stream,Z_FINISH)==Z_STREAM_END);
    bytes.resize(stream.total_out);inflateEnd(&stream);
    return bytes;
}
pandoeditor::HydroIndex decode(const std::vector<std::uint8_t>& bytes) {
    return pandoeditor::decodeHydroIndex({bytes.data(),bytes.size()},{507});
}
bool rejected(const std::vector<std::uint8_t>& bytes) {
    try {decode(bytes);return false;}catch(const std::runtime_error&){return true;}
}
}
int main() {
    // Exact IEEE-754 output of the pinned worker's integer / 1e6 decode.
    // Node 24.19.0 DataView.getBigUint64; positive/negative, antimeridian,
    // near-polar, and microdegree values. A reciprocal multiply differs by ULPs.
    const std::vector<std::pair<std::int32_t,std::uint64_t>> coordinateBits{
        {15566667,0x402f22222d5171e3ULL},{-15566667,0xc02f22222d5171e3ULL},
        {179999999,0x40667ffffde7210cULL},{-179999999,0xc0667ffffde7210cULL},
        {180000000,0x4066800000000000ULL},{-180000000,0xc066800000000000ULL},
        {89999999,0x40567ffffbce4218ULL},{-89999999,0xc0567ffffbce4218ULL},
        {1,0x3eb0c6f7a0b5ed8dULL},{-1,0xbeb0c6f7a0b5ed8dULL},
        {44856250,0x40466d999999999aULL},{-44856250,0xc0466d999999999aULL}};
    const auto bits=[](double value){std::uint64_t result;static_assert(sizeof(result)==sizeof(value));std::memcpy(&result,&value,sizeof(value));return result;};
    pandoeditor::HydroPhysicalFeature exact;exact.kind=1;exact.fragmentCount=1;exact.geometry.lines.emplace_back();
    for(const auto& row:coordinateBits)exact.geometry.lines[0].push_back({row.first,row.first});
    const auto exactLine=pandoeditor::mergeHydroLogicalFragments({exact});
    for(std::size_t i=0;i<coordinateBits.size();++i){assert(bits(exactLine.lines[0][i].x)==coordinateBits[i].second);assert(bits(exactLine.lines[0][i].y)==coordinateBits[i].second);}
    exact.kind=2;exact.geometry.polygons={{exact.geometry.lines[0]}};exact.geometry.lines.clear();
    const auto exactPolygon=pandoeditor::mergeHydroLogicalFragments({exact});
    for(std::size_t i=0;i<coordinateBits.size();++i){assert(bits(exactPolygon.polygons[0][0][i].x)==coordinateBits[i].second);assert(bits(exactPolygon.polygons[0][0][i].y)==coordinateBits[i].second);}
    auto bytes=fixture();
    auto index=decode(bytes);
    assert(index.tilePacks.size()==5);
    assert(index.logicalPacks.size()==5);
    assert(index.packSpecs.size()==6);
    assert(index.logicalPacks.at(5)==(std::vector<std::uint32_t>{4,5}));
    assert(index.packSpecs.at(5).offset==423);
    {
        auto zeroBased=bytes;
        std::size_t offset=20;
        for(std::size_t i=0;i<index.tilePacks.size();i++){
            const auto count=std::size_t(zeroBased[offset+5])|(std::size_t(zeroBased[offset+6])<<8);
            offset+=7+4*count;
        }
        assert(zeroBased[offset]==1);zeroBased[offset]=0;
        assert(decode(zeroBased).logicalPacks.count(0)==1);
    }
    for(std::size_t cut=0;cut<bytes.size();cut++){
        auto truncated=bytes;truncated.resize(cut);assert(rejected(truncated));
    }
    bytes[0]^=1;assert(rejected(bytes));bytes[0]^=1;
    bytes[4]=3;assert(rejected(bytes));bytes[4]=4;
    bytes.push_back(0);assert(rejected(bytes));bytes.pop_back();
    bytes[8]=255;assert(rejected(bytes));bytes[8]=5;
    bytes[20]=9;assert(rejected(bytes));bytes[20]=0;
    assert(rejected(std::vector<std::uint8_t>{}));
    const std::map<std::uint32_t,std::uint32_t> metadata{{1,1},{2,2},{3,3},{4,4},{5,5},{6,5}};
    const auto first=pack(index.packSpecs.at(0));
    const auto river=pandoeditor::decodeHydroPack({first.data(),first.size()},0,metadata);
    {
        auto zeroBased=first;zeroBased[12]=0;zeroBased[16]=0;
        const auto decoded=pandoeditor::decodeHydroPack({zeroBased.data(),zeroBased.size()},0,{{0,0}});
        assert(decoded.features.front().fid==0&&decoded.features.front().logicalFid==0);
        const auto merged=pandoeditor::mergeHydroLogicalFragments(decoded.features);
        assert(merged.type=="LineString"&&merged.lines.size()==1);
    }
    assert(river.features.size()==1);
    assert(river.features[0].geometry.lines.size()==1);
    assert(river.features[0].widths[0]==(std::vector<double>{0.8,1.2,1.6}));
    const auto riverPacket=pandoeditor::buildHydroRenderPacket(river);
    assert(riverPacket.rivers.size()==2);
    assert(riverPacket.rivers[0].startWidth==0.8 && riverPacket.rivers[0].endWidth==1.2);
    const auto hole=pack(index.packSpecs.at(3));
    const auto lake=pandoeditor::decodeHydroPack({hole.data(),hole.size()},3,metadata);
    assert(lake.features[0].geometry.polygons[0].size()==2);
    assert(pandoeditor::buildHydroRenderPacket(lake).lakes[0].polygons[0].size()==2);
    const auto border=pack(index.packSpecs.at(1));
    const auto borderRiver=pandoeditor::decodeHydroPack({border.data(),border.size()},1,metadata);
    assert(borderRiver.features[0].flags&1);
    assert(borderRiver.features[0].geometry.kind==2);
    assert(borderRiver.features[0].widths.size()==2);
    assert(borderRiver.features[0].widths[1][1]==1.3);
    assert(pandoeditor::buildHydroRenderPacket(borderRiver).rivers[0].borderAligned);
    for(std::size_t id=0;id<6;id++){
        const auto data=pack(index.packSpecs.at(id));
        const auto decoded=pandoeditor::decodeHydroPack({data.data(),data.size()},static_cast<std::uint32_t>(id),metadata);
        assert(decoded.features.size()==1);
        assert(decoded.features[0].logicalFid==(id==5?5:id+1));
    }
    auto corrupt=first;corrupt[0]^=1;
    try {pandoeditor::decodeHydroPack({corrupt.data(),corrupt.size()},0,metadata);assert(false);}
    catch(const std::runtime_error&){}
    const auto packRejected=[&metadata](const std::vector<std::uint8_t>& data){
        try {pandoeditor::decodeHydroPack({data.data(),data.size()},0,metadata);return false;}
        catch(const std::runtime_error&){return true;}
    };
    for(std::size_t cut=0;cut<first.size();cut++){
        auto truncated=first;truncated.resize(cut);assert(packRejected(truncated));
    }
    corrupt=first;corrupt[4]=3;assert(packRejected(corrupt));
    corrupt=first;corrupt[22]=9;assert(packRejected(corrupt)); // geometry kind
    corrupt=first;corrupt[48]=255;assert(packRejected(corrupt)); // geometryLength
    corrupt=first;corrupt[52]=255;assert(packRejected(corrupt)); // widthLength
    corrupt=first;corrupt.push_back(0);assert(packRejected(corrupt));
    corrupt=first;corrupt[16]=9;assert(packRejected(corrupt)); // logicalFid
}
