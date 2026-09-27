#include "countrymesh.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void require(bool valid,const char* message){if(!valid)throw std::runtime_error(message);}
std::uint32_t u32(const QByteArray& data,std::size_t at) {
    require(at<=std::size_t(data.size())&&std::size_t(data.size())-at>=4,"CMG truncated word");
    const auto* p=reinterpret_cast<const unsigned char*>(data.constData()+at);
    return std::uint32_t(p[0])|std::uint32_t(p[1])<<8|std::uint32_t(p[2])<<16|std::uint32_t(p[3])<<24;
}
std::uint16_t u16(const QByteArray& data,std::size_t at) {
    require(at<=std::size_t(data.size())&&std::size_t(data.size())-at>=2,"CMG truncated country index");
    const auto* p=reinterpret_cast<const unsigned char*>(data.constData()+at);
    return std::uint16_t(p[0])|std::uint16_t(p[1])<<8;
}
}
std::shared_ptr<const CountryBaseMesh> decodeCountryBaseMesh(const QByteArray& bytes,bool preview) {
    require(bytes.size()>=48&&u32(bytes,0)==0x434d4731&&u32(bytes,4)==2&&
            u32(bytes,8)==258&&u32(bytes,28)==3,"Invalid CMG2 header");
    const auto vertices=u32(bytes,12),triangles=u32(bytes,16),lines=u32(bytes,20);
    const auto coordinates=u32(bytes,24);
    require(coordinates==(preview?105884U:548454U)&&vertices>0&&triangles%3==0&&lines%2==0&&
            u32(bytes,32)==516&&u32(bytes,36)==516&&u32(bytes,40)==1032&&u32(bytes,44)==258,
            "Wrong CMG2 country mesh envelope");
    const std::uint64_t paddedCountries=(std::uint64_t(vertices)*2+3)&~std::uint64_t(3);
    const std::uint64_t expected=48+std::uint64_t(vertices)*8+paddedCountries+
        std::uint64_t(triangles+lines+516+516+1032+258)*4;
    require(expected==std::uint64_t(bytes.size()),"CMG2 byte length is inconsistent");
    auto result=std::make_shared<CountryBaseMesh>();result->preview=preview;
    result->sourceCoordinateCount=coordinates;
    std::size_t at=48;
    result->positionsMicrodegrees.reserve(std::size_t(vertices)*2);
    for(std::size_t i=0;i<std::size_t(vertices)*2;++i,at+=4) {
        const auto raw=std::int32_t(u32(bytes,at));
        // Preview triangles carry unwrapped display-only longitudes. Canonical
        // mesh and PCG1 geometry remain within the geographic longitude range.
        require((i%2?std::int64_t(std::abs(std::int64_t(raw)))<=90000000:
            std::int64_t(std::abs(std::int64_t(raw)))<=(preview?540000000:180000000)),
            "CMG2 position outside supported geographic range");
        result->positionsMicrodegrees.push_back(raw);
    }
    result->countryIndices.reserve(vertices);
    for(std::uint32_t i=0;i<vertices;++i,at+=2) {
        const auto owner=u16(bytes,at);
        require(owner<258,"CMG2 vertex owner is invalid");
        result->countryIndices.push_back(owner);
    }
    at=(at+3)&~std::size_t(3);
    auto readIndices=[&](std::vector<std::uint32_t>& out,std::uint32_t count) {
        out.reserve(count);
        for(std::uint32_t i=0;i<count;++i,at+=4) {
            const auto index=u32(bytes,at);
            require(index<vertices,"CMG2 index outside vertex range");
            out.push_back(index);
        }
    };
    readIndices(result->triangleIndices,triangles);
    readIndices(result->lineIndices,lines);
    auto readU32=[&](std::vector<std::uint32_t>& out,std::uint32_t count) {
        out.reserve(count);
        for(std::uint32_t i=0;i<count;++i,at+=4)out.push_back(u32(bytes,at));
    };
    readU32(result->countryTriangleRanges,516);
    readU32(result->countryBoundaryRanges,516);
    result->countryBounds.reserve(1032);
    for(int i=0;i<1032;++i,at+=4)result->countryBounds.push_back(std::int32_t(u32(bytes,at)));
    readU32(result->countryBoundsFlags,258);
    require(at==std::size_t(bytes.size()),"CMG2 decoder did not consume entire mesh");
    std::uint32_t triangleEnd=0,boundaryEnd=0;
    for(int i=0;i<258;++i) {
        const auto triStart=result->countryTriangleRanges[i*2],triCount=result->countryTriangleRanges[i*2+1];
        const auto lineStart=result->countryBoundaryRanges[i*2],lineCount=result->countryBoundaryRanges[i*2+1];
        require(triStart==triangleEnd&&lineStart==boundaryEnd&&triCount%3==0&&lineCount%2==0&&
            std::uint64_t(triStart)+triCount<=triangles&&
            std::uint64_t(lineStart)+lineCount<=lines,"CMG2 country ranges are invalid");
        triangleEnd=triStart+triCount;boundaryEnd=lineStart+lineCount;
        for(std::uint32_t j=triStart;j<triangleEnd;++j)
            require(result->countryIndices[result->triangleIndices[j]]==i,"CMG2 triangle owner mismatch");
        for(std::uint32_t j=lineStart;j<boundaryEnd;++j)
            require(result->countryIndices[result->lineIndices[j]]==i,"CMG2 boundary owner mismatch");
    }
    require(triangleEnd==triangles&&boundaryEnd==lines,"CMG2 country ranges do not cover mesh");
    return result;
}
