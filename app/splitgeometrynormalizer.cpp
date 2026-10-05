#include "splitgeometrynormalizer.h"
#include "geometryruntime_p.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJSEngine>
#include <QResource>
#include <cmath>
#include <limits>
#include <stdexcept>

static void initializeSplitNormalizerResources() {
    static const bool initialized=[] {Q_INIT_RESOURCE(m973_split_normalizer);return true;}();
    (void)initialized;
}
namespace pandoeditor {
namespace {
struct Cancelled {};
void check(const GeometryCancellation& cancelled) {if(cancelled&&cancelled())throw Cancelled{};}
QByteArray verified(const QString& path,const QByteArray& expected) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error(("SPLIT_NORMALIZER_RESOURCE_UNAVAILABLE: "+path).toStdString());
    const auto bytes=file.readAll();
    if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!=expected)
        throw std::runtime_error(("SPLIT_NORMALIZER_RESOURCE_HASH_MISMATCH: "+path).toStdString());
    return bytes;
}
void jsCheck(const QJSValue& value) {
    if(value.isError())throw std::runtime_error(("SPLIT_NORMALIZER_JAVASCRIPT_ERROR: "+value.toString()+"\n"+value.property("stack").toString()).toStdString());
}
uint size(std::size_t value) {
    if(value>std::numeric_limits<uint>::max())throw std::invalid_argument("SPLIT_NORMALIZER_INPUT_TOO_LARGE");
    return static_cast<uint>(value);
}
QJSValue encode(QJSEngine& engine,const Geometry& geometry,const GeometryCancellation& cancelled) {
    if((geometry.type!="Polygon"&&geometry.type!="MultiPolygon")||!geometry.points.empty()||!geometry.lines.empty()||
       (geometry.type=="Polygon"&&geometry.polygons.size()!=1))
        throw std::invalid_argument("SPLIT_NORMALIZER_INVALID_INPUT: polygon dimensions");
    auto polygons=engine.newArray(size(geometry.polygons.size()));
    for(uint p=0;p<geometry.polygons.size();++p){check(cancelled);const auto& polygon=geometry.polygons[p];
        auto rings=engine.newArray(size(polygon.size()));
        for(uint r=0;r<polygon.size();++r){check(cancelled);const auto& ring=polygon[r];auto points=engine.newArray(size(ring.size()));
            for(uint i=0;i<ring.size();++i){const auto point=ring[i];
                if(!std::isfinite(point.x)||!std::isfinite(point.y))throw std::invalid_argument("SPLIT_NORMALIZER_INVALID_INPUT: non-finite coordinate");
                auto value=engine.newArray(2);value.setProperty(0,point.x);value.setProperty(1,point.y);points.setProperty(i,value);}
            rings.setProperty(r,points);}
        polygons.setProperty(p,rings);}
    auto result=engine.newObject();result.setProperty("type",QString::fromStdString(geometry.type));
    result.setProperty("coordinates",geometry.type=="Polygon"?polygons.property(0):polygons);return result;
}
uint length(const QJSValue& value) {
    if(!value.isArray())throw std::runtime_error("SPLIT_NORMALIZER_INVALID_RESULT: array expected");
    return value.property("length").toUInt();
}
Geometry decode(const QJSValue& value,const GeometryCancellation& cancelled) {
    if(!value.isObject()||!value.property("type").isString())throw std::runtime_error("SPLIT_NORMALIZER_INVALID_RESULT: polygon expected");
    Geometry geometry;geometry.type=value.property("type").toString().toStdString();
    if(geometry.type!="Polygon"&&geometry.type!="MultiPolygon")throw std::runtime_error("SPLIT_NORMALIZER_INVALID_RESULT: polygon type");
    const auto coordinates=value.property("coordinates");
    const uint count=geometry.type=="Polygon"?1:length(coordinates);
    for(uint p=0;p<count;++p){check(cancelled);const auto rings=geometry.type=="Polygon"?coordinates:coordinates.property(p);Polygon polygon;
        for(uint r=0;r<length(rings);++r){check(cancelled);const auto points=rings.property(r);Ring ring;
            for(uint i=0;i<length(points);++i){const auto point=points.property(i);
                if(length(point)!=2||!point.property(0).isNumber()||!point.property(1).isNumber())
                    throw std::runtime_error("SPLIT_NORMALIZER_INVALID_RESULT: coordinate expected");
                const Point decoded{point.property(0).toNumber(),point.property(1).toNumber()};
                if(!std::isfinite(decoded.x)||!std::isfinite(decoded.y))throw std::runtime_error("SPLIT_NORMALIZER_INVALID_RESULT: non-finite coordinate");
                ring.push_back(decoded);}
            polygon.push_back(std::move(ring));}
        geometry.polygons.push_back(std::move(polygon));}
    return geometry;
}
SplitGeometryNormalizationResult stopped() {SplitGeometryNormalizationResult output;output.status=GeometryOperationStatus::Cancelled;return output;}
SplitGeometryNormalizationResult run(const Geometry& geometry,const char* operation,const GeometryCancellation& cancelled) {
    try {
        check(cancelled);QJSEngine engine;loadApprovedSplitPolygonGeometry(engine);check(cancelled);
        loadPinnedPolygonClipping(engine);check(cancelled);
        const auto input=encode(engine,geometry,cancelled);check(cancelled);
        const auto stringify=engine.globalObject().property("JSON").property("stringify");
        const auto before=stringify.call({input});jsCheck(before);check(cancelled);
        const auto function=engine.globalObject().property("PandoLabPolygonGeometry").property(operation);
        const auto value=function.call({input,engine.globalObject().property("polygonClipping")});
        // The exact synchronous source cannot be interrupted mid-call. Discard
        // success and errors alike when cancellation arrives while it runs.
        check(cancelled);jsCheck(value);
        const auto after=stringify.call({input});jsCheck(after);
        if(before.toString()!=after.toString())throw std::runtime_error("SPLIT_NORMALIZER_INPUT_MUTATED");
        check(cancelled);SplitGeometryNormalizationResult output;
        if(!value.isNull())output.geometry=decode(value,cancelled);
        check(cancelled);output.status=output.geometry.polygons.empty()?GeometryOperationStatus::Empty:GeometryOperationStatus::Completed;
        output.inputUnchanged=true;return output;
    }catch(const Cancelled&){return stopped();}
    catch(const std::exception& error){if(cancelled&&cancelled())return stopped();SplitGeometryNormalizationResult output;output.detail=error.what();return output;}
}
}
void loadApprovedSplitPolygonGeometry(QJSEngine& engine) {
    initializeSplitNormalizerResources();
    verified(":/split-corrections/approved-manifest.json","d15bd80373af3ff98c3cc4fcec2f78b6268956c275e3318b08a5a71208c30096");
    verified(":/split-corrections/provenance.json","1755ff27ba7b5a58215bff0c0ffee4bae2b0923f0c9c0cbb1873ba4fa53b31d4");
    const auto source=verified(":/split-corrections/original/polygon-geometry.js","a645827c46f7c62dbf929f850c67f350516669dfeafe5c637aec6c67efcfebf4");
    engine.globalObject().setProperty("globalThis",engine.globalObject());
    jsCheck(engine.evaluate(QString::fromUtf8(source),":/split-corrections/original/polygon-geometry.js"));
    const auto api=engine.globalObject().property("PandoLabPolygonGeometry");
    for(const auto name:{"ensureClosedRing","hasCanonicalPolygonWinding","normalizePolygonGeometry","normalizeClippedPolygonGeometry","wrapPolygonGeometry","orientRing","ringDistinctCoordinateCount","ringSignedArea"})
        if(!api.property(name).isCallable())throw std::runtime_error("SPLIT_NORMALIZER_INVALID_EXPORT");
}
SplitGeometryNormalizationResult wrapSplitGeometry(const Geometry& geometry,const GeometryCancellation& cancelled) {
    return run(geometry,"wrapPolygonGeometry",cancelled);
}
SplitGeometryNormalizationResult normalizeSplitRawGeometry(const Geometry& geometry,const GeometryCancellation& cancelled) {
    return run(geometry,"normalizePolygonGeometry",cancelled);
}
SplitGeometryNormalizationResult normalizeSplitClippedGeometry(const Geometry& geometry,const GeometryCancellation& cancelled) {
    return run(geometry,"normalizeClippedPolygonGeometry",cancelled);
}
}
