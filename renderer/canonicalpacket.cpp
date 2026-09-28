#include "canonicalpacket.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <stdexcept>

namespace {
void require(bool valid,const char* message) {if(!valid)throw std::runtime_error(message);}
std::uint32_t u32(const QByteArray& bytes,std::size_t offset) {
    require(offset<=std::size_t(bytes.size())&&std::size_t(bytes.size())-offset>=4,"PCG1 truncated word");
    const auto* p=reinterpret_cast<const unsigned char*>(bytes.constData()+offset);
    return std::uint32_t(p[0])|std::uint32_t(p[1])<<8|std::uint32_t(p[2])<<16|std::uint32_t(p[3])<<24;
}
std::vector<std::uint32_t> table(const QByteArray& bytes,std::uint32_t offset,
                                  std::uint32_t count,std::uint32_t expectedEnd) {
    require(std::uint64_t(offset)+std::uint64_t(count)*4<=std::uint64_t(bytes.size()),"PCG1 truncated offsets");
    std::vector<std::uint32_t> result;result.reserve(count);
    for(std::uint32_t i=0;i<count;++i) {
        const auto value=u32(bytes,std::size_t(offset)+std::size_t(i)*4);
        require(value<=expectedEnd&&(result.empty()?value==0:value>=result.back()),
            "PCG1 offset table is not monotonic");
        result.push_back(value);
    }
    require(result.back()==expectedEnd,"PCG1 offset table has wrong end");
    return result;
}
}

CanonicalCountryStore::CanonicalCountryStore(QByteArray decompressed):bytes_(std::move(decompressed)) {
    require(bytes_.size()>=80&&u32(bytes_,0)==0x31474350&&u32(bytes_,4)==1,
        "Invalid PCG1 header");
    summary_={u32(bytes_,8),u32(bytes_,12),u32(bytes_,16),u32(bytes_,20)};
    require(summary_.featureCount==258&&summary_.positionCount==548454&&
        u32(bytes_,76)==std::uint32_t(bytes_.size())&&
        u32(bytes_,28)==summary_.featureCount&&
        u32(bytes_,32)==summary_.featureCount+1&&
        u32(bytes_,36)==summary_.polygonCount+1&&
        u32(bytes_,40)==summary_.ringCount+1&&
        u32(bytes_,44)==summary_.featureCount*2,"Wrong PCG1 country envelope");
    const auto metaBegin=u32(bytes_,48),metaSize=u32(bytes_,24),typeBegin=u32(bytes_,52);
    const auto fpBegin=u32(bytes_,68),positionBegin=u32(bytes_,72);
    require(metaBegin>=80&&std::uint64_t(metaBegin)+metaSize<=typeBegin&&
        std::uint64_t(typeBegin)+summary_.featureCount<=u32(bytes_,56)&&
        u32(bytes_,56)%4==0&&u32(bytes_,60)%4==0&&u32(bytes_,64)%4==0&&
        fpBegin%4==0&&positionBegin%8==0&&
        std::uint64_t(u32(bytes_,56))+std::uint64_t(summary_.featureCount+1)*4<=u32(bytes_,60)&&
        std::uint64_t(u32(bytes_,60))+std::uint64_t(summary_.polygonCount+1)*4<=u32(bytes_,64)&&
        std::uint64_t(u32(bytes_,64))+std::uint64_t(summary_.ringCount+1)*4<=fpBegin&&
        std::uint64_t(fpBegin)+std::uint64_t(summary_.featureCount)*8<=positionBegin&&
        std::uint64_t(positionBegin)+std::uint64_t(summary_.positionCount)*16==std::uint64_t(bytes_.size()),
        "Invalid PCG1 section offsets");
    QJsonParseError error;
    const auto parsed=QJsonDocument::fromJson(bytes_.mid(int(metaBegin),int(metaSize)),&error);
    require(error.error==QJsonParseError::NoError&&parsed.isArray(),"Invalid PCG1 metadata JSON");
    const auto array=parsed.array();
    require(array.size()==int(summary_.featureCount),"Wrong PCG1 metadata count");
    std::set<QString> unique;
    for(const auto& value:array) {
        const auto object=value.toObject();
        const auto id=object.value("id").toString();
        require(!id.isEmpty()&&unique.insert(id).second,"Invalid PCG1 country ID");
        metadata_.push_back(object);
    }
    for(std::uint32_t i=0;i<summary_.featureCount;++i) {
        const auto type=std::uint8_t(bytes_.at(int(typeBegin+i)));
        require(type==1||type==2,"Invalid PCG1 geometry type");
        types_.push_back(type);
    }
    featurePolygons_=table(bytes_,u32(bytes_,56),summary_.featureCount+1,summary_.polygonCount);
    polygonRings_=table(bytes_,u32(bytes_,60),summary_.polygonCount+1,summary_.ringCount);
    ringPositions_=table(bytes_,u32(bytes_,64),summary_.ringCount+1,summary_.positionCount);
    positionsOffset_=positionBegin;
    for(std::size_t i=0;i<summary_.positionCount*2ULL;++i)
        require(std::isfinite(coordinate(i)),"PCG1 contains non-finite coordinates");
    for(std::size_t i=0;i<summary_.featureCount;++i) {
        require(featurePolygons_[i]<featurePolygons_[i+1]&&
            (types_[i]==2||featurePolygons_[i+1]-featurePolygons_[i]==1),
            "PCG1 country polygon hierarchy is invalid");
    }
    for(std::size_t i=0;i<summary_.polygonCount;++i)
        require(polygonRings_[i]<polygonRings_[i+1],"PCG1 polygon has no outer ring");
    for(std::size_t i=0;i<summary_.ringCount;++i)
        require(ringPositions_[i+1]-ringPositions_[i]>=4,"PCG1 ring is too short");
}
double CanonicalCountryStore::coordinate(std::size_t index) const {
    const auto offset=positionsOffset_+index*8;
    std::uint64_t bits=std::uint64_t(u32(bytes_,offset))|
        (std::uint64_t(u32(bytes_,offset+4))<<32);
    double value;
    std::memcpy(&value,&bits,sizeof(value));return value;
}
std::vector<std::string> CanonicalCountryStore::ids() const {
    std::vector<std::string> result;result.reserve(metadata_.size());
    for(const auto& object:metadata_)result.push_back(object.value("id").toString().toStdString());
    return result;
}
pandoeditor::TerritorialUnit CanonicalCountryStore::materializeUnit(std::size_t index) const {
    if(index>=metadata_.size())throw std::out_of_range("PCG1 country index");
    const auto& entry=metadata_[index];
    pandoeditor::TerritorialUnit unit;
    unit.id=entry.value("id").toString().toStdString();
    unit.baseName=entry.value("properties").toObject().value("name").toString().toStdString();
    unit.name=unit.baseName.empty()?unit.id:unit.baseName;
    unit.kind=pandoeditor::UnitKind::Country;
    unit.geometry={"world-country-"+unit.id,1};
    return unit;
}
pandoeditor::Geometry CanonicalCountryStore::materializeGeometry(std::size_t index) const {
    if(index>=metadata_.size())throw std::out_of_range("PCG1 geometry index");
    pandoeditor::Geometry geometry;
    geometry.type=types_[index]==1?"Polygon":"MultiPolygon";
    for(auto polygon=featurePolygons_[index];polygon<featurePolygons_[index+1];++polygon) {
        pandoeditor::Polygon rings;
        for(auto ring=polygonRings_[polygon];ring<polygonRings_[polygon+1];++ring) {
            pandoeditor::Ring points;
            points.reserve(ringPositions_[ring+1]-ringPositions_[ring]);
            for(auto position=ringPositions_[ring];position<ringPositions_[ring+1];++position)
                points.push_back({coordinate(std::size_t(position)*2),
                                  coordinate(std::size_t(position)*2+1)});
            rings.push_back(std::move(points));
        }
        geometry.polygons.push_back(std::move(rings));
    }
    return geometry;
}
bool CanonicalCountryStore::geometryEquals(std::size_t index,const pandoeditor::Geometry& geometry) const {
    if(index>=metadata_.size()||geometry.type!=(types_[index]==1?"Polygon":"MultiPolygon")||
       geometry.polygons.size()!=featurePolygons_[index+1]-featurePolygons_[index])return false;
    for(std::size_t p=0;p<geometry.polygons.size();++p) {
        const auto polygon=featurePolygons_[index]+p;
        if(geometry.polygons[p].size()!=polygonRings_[polygon+1]-polygonRings_[polygon])return false;
        for(std::size_t r=0;r<geometry.polygons[p].size();++r) {
            const auto ring=polygonRings_[polygon]+r;
            if(geometry.polygons[p][r].size()!=ringPositions_[ring+1]-ringPositions_[ring])return false;
            for(std::size_t k=0;k<geometry.polygons[p][r].size();++k) {
                const auto pos=std::size_t(ringPositions_[ring])+k;
                if(geometry.polygons[p][r][k].x!=coordinate(pos*2)||
                   geometry.polygons[p][r][k].y!=coordinate(pos*2+1))return false;
            }
        }
    }
    return true;
}
