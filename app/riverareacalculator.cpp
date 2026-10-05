#include "riverareacalculator.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJSEngine>
#include <QResource>
#include <cmath>
#include <stdexcept>

static void initializeRiverAreaResources() {
    static const bool initialized=[] {Q_INIT_RESOURCE(m972_river);return true;}();
    (void)initialized;
}
namespace pandoeditor {
namespace {
struct Cancelled {};
void check(const GeometryCancellation& cancelled) {
    if(cancelled&&cancelled())throw Cancelled{};
}
void jsCheck(const QJSValue& value) {
    if(value.isError())throw std::runtime_error(
        ("RIVER_AREA_JAVASCRIPT_ERROR: "+value.toString()+"\n"+value.property("stack").toString()).toStdString());
}
QByteArray verifiedResource(const QString& path,qsizetype size,const QByteArray& sha256) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))throw std::runtime_error(("RIVER_AREA_RESOURCE_UNAVAILABLE: "+path).toStdString());
    const auto bytes=file.readAll();
    if(bytes.size()!=size||QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!=sha256)
        throw std::runtime_error(("RIVER_AREA_RESOURCE_HASH_MISMATCH: "+path).toStdString());
    return bytes;
}
QByteArray verifiedD3() {
    initializeRiverAreaResources();
    verifiedResource(":/river/original/d3.min.js",151144,
        "4cdf92091ed0cfdd8b862af1c6d4744bd0458e746b92c1bbd5403a1143ecd538");
    // The checked-in, independently reviewed adapter only inserts redundant
    // ES5 function-scope var declarations. No original byte/math is changed.
    return verifiedResource(":/river/adapted/d3.min.js",158242,
        "f273f409d8b1ba4c35d98fb06ca56f06726359d792ad719400d0e46a8c15cd85");
}
QJSValue geometryValue(QJSEngine& engine,const Geometry& input,const GeometryCancellation& cancelled) {
    if((input.type!="Polygon"&&input.type!="MultiPolygon")||
       (input.type=="Polygon"&&input.polygons.size()!=1)||
       !input.lines.empty()||!input.points.empty())
        throw std::invalid_argument("RIVER_AREA_INVALID_POLYGON_GEOMETRY");
    auto polygons=engine.newArray(static_cast<uint>(input.polygons.size()));
    for(std::size_t p=0;p<input.polygons.size();++p) {
        check(cancelled);const auto& polygon=input.polygons[p];
        if(polygon.empty())throw std::invalid_argument("RIVER_AREA_EMPTY_POLYGON");
        auto rings=engine.newArray(static_cast<uint>(polygon.size()));
        for(std::size_t r=0;r<polygon.size();++r) {
            const auto& ring=polygon[r];
            if(ring.size()<4||ring.front().x!=ring.back().x||ring.front().y!=ring.back().y)
                throw std::invalid_argument("RIVER_AREA_INVALID_RING");
            auto points=engine.newArray(static_cast<uint>(ring.size()));
            for(std::size_t i=0;i<ring.size();++i) {
                check(cancelled);const auto& point=ring[i];
                if(!std::isfinite(point.x)||!std::isfinite(point.y)||std::abs(point.x)>180||std::abs(point.y)>90)
                    throw std::invalid_argument("RIVER_AREA_INVALID_COORDINATE");
                auto xy=engine.newArray(2);xy.setProperty(0,point.x);xy.setProperty(1,point.y);
                points.setProperty(static_cast<uint>(i),xy);
            }
            rings.setProperty(static_cast<uint>(r),points);
        }
        polygons.setProperty(static_cast<uint>(p),rings);
    }
    auto geometry=engine.newObject();geometry.setProperty("type",QString::fromStdString(input.type));
    geometry.setProperty("coordinates",input.type=="Polygon"?polygons.property(0):polygons);
    return geometry;
}
}
RiverAreaResult calculateRiverAreaKm2(const Geometry& input,const GeometryCancellation& cancelled) {
    try {
        check(cancelled);QJSEngine engine;const auto geometry=geometryValue(engine,input,cancelled);
        check(cancelled);const auto bytes=verifiedD3();check(cancelled);
        jsCheck(engine.evaluate(QString::fromUtf8(bytes),":/river/adapted/d3.min.js"));
        check(cancelled);const auto d3=engine.globalObject().property("d3");
        if(d3.property("version").toString()!="3.5.6"||!d3.property("geo").property("area").isCallable())
            throw std::runtime_error("RIVER_AREA_D3_UNAVAILABLE");
        // Exact pinned app display expression, with no DOM or math shim.
        // Only the independently verified redundant-var Qt adapter is applied.
        const auto metric=engine.evaluate(
            "(function(geometry) { return Math.max(0, d3.geo.area(geometry) * 6371.0088 ** 2); })",
            "river-area-display-expression.js");
        jsCheck(metric);check(cancelled);const auto value=metric.call({geometry});
        check(cancelled);jsCheck(value);
        if(!value.isNumber())throw std::runtime_error("RIVER_AREA_INVALID_RESULT");
        const auto areaKm2=value.toNumber();check(cancelled);
        if(!std::isfinite(areaKm2)||areaKm2<0)throw std::runtime_error("RIVER_AREA_NONFINITE_RESULT");
        RiverAreaResult result;result.areaKm2=areaKm2;check(cancelled);
        result.status=RiverPartitionStatus::Completed;return result;
    }catch(const Cancelled&) {
        RiverAreaResult result;result.status=RiverPartitionStatus::Cancelled;return result;
    }catch(const std::exception& error) {
        RiverAreaResult result;
        if(cancelled&&cancelled())result.status=RiverPartitionStatus::Cancelled;
        else result.detail=QString::fromUtf8(error.what());
        return result;
    }
}
}
