#include "geometrycalculator.h"
#include "geometryruntime_p.h"
#include <pandoeditor/geometrypredicates.h>
#include <QFile>
#include <QCryptographicHash>
#include <QJSEngine>
#include <QJSValue>
#include <QResource>
#include <cmath>
#include <stdexcept>
#include <optional>

static void initializeGeometryResource() {
    static const bool initialized=[] { Q_INIT_RESOURCE(m4_geometry);return true; }();
    (void)initialized;
}

namespace pandoeditor {
namespace {
QJSValue coordinates(QJSEngine& engine,const Geometry& geometry) {
    auto polygons=engine.newArray(static_cast<uint>(geometry.polygons.size()));
    for(uint p=0;p<geometry.polygons.size();++p) {
        auto rings=engine.newArray(static_cast<uint>(geometry.polygons[p].size()));
        for(uint r=0;r<geometry.polygons[p].size();++r) {
            const auto& ring=geometry.polygons[p][r];
            auto points=engine.newArray(static_cast<uint>(ring.size()));
            for(uint i=0;i<ring.size();++i) {
                auto point=engine.newArray(2);
                point.setProperty(0,ring[i].x);point.setProperty(1,ring[i].y);
                points.setProperty(i,point);
            }
            rings.setProperty(r,points);
        }
        polygons.setProperty(p,rings);
    }
    return polygons;
}
uint length(const QJSValue& value) {
    if(!value.isArray())throw std::runtime_error("INVALID_KERNEL_RESULT: array expected");
    return value.property("length").toUInt();
}
Geometry decode(const QJSValue& value) {
    Geometry geometry;
    for(uint p=0;p<length(value);++p) {
        Polygon polygon;const auto rings=value.property(p);
        for(uint r=0;r<length(rings);++r) {
            Ring ring;const auto points=rings.property(r);
            for(uint i=0;i<length(points);++i) {
                const auto point=points.property(i);
                if(length(point)!=2 || !point.property(0).isNumber() || !point.property(1).isNumber())
                    throw std::runtime_error("INVALID_KERNEL_RESULT: coordinate expected");
                ring.push_back({point.property(0).toNumber(),point.property(1).toNumber()});
            }
            polygon.push_back(std::move(ring));
        }
        geometry.polygons.push_back(std::move(polygon));
    }
    return geometry;
}
void validatePolygon(const Geometry& geometry) {
    if(geometry.type!="Polygon" && geometry.type!="MultiPolygon")
        throw std::invalid_argument("INVALID_GEOMETRY: polygon required");
    GeometryStore validator;validator.insert({"calculation",1},geometry);
}
}
void loadPinnedPolygonClipping(QJSEngine& engine) {
    initializeGeometryResource();
    QFile source(":/geometry/polygon-clipping-0.15.7.js");
    if(!source.open(QIODevice::ReadOnly))throw std::runtime_error("GEOMETRY_KERNEL_UNAVAILABLE");
    const auto original=source.readAll();
    if(QCryptographicHash::hash(original,QCryptographicHash::Sha256).toHex()!=
       "8c1ed56df8b1f97b047f82d91b910aacdaff67d8d9a55f2495eb26e8369186f7")
        throw std::runtime_error("GEOMETRY_KERNEL_HASH_MISMATCH");
    auto script=QString::fromUtf8(original);
    // Qt 6.8.3 loses the _root assignment in this comma-return expression.
    // Preserve the pinned upstream resource and expand only this statement.
    const QString compressed=QStringLiteral("return this._size++,this._root=i(t,e,this._root,this._comparator)");
    if(script.count(compressed)!=1)throw std::runtime_error("GEOMETRY_KERNEL_COMPAT_MISMATCH");
    script.replace(compressed,QStringLiteral("this._size++;var inserted=i(t,e,this._root,this._comparator);this._root=inserted;return inserted"));
    const auto loaded=engine.evaluate(script,source.fileName());
    if(loaded.isError())throw std::runtime_error(loaded.toString().toStdString());
}
namespace {
void validateRiverIntermediate(const Geometry& geometry) {
    const auto require=[](bool condition,const char* reason) {
        if(!condition)throw std::invalid_argument(reason);
    };
    require((geometry.type=="Polygon"||geometry.type=="MultiPolygon")&&geometry.points.empty()&&geometry.lines.empty()
        &&(geometry.type!="Polygon"||geometry.polygons.size()==1),"INVALID_RIVER_INTERMEDIATE: polygon dimensions");
    for(const auto& polygon:geometry.polygons) {
        require(!polygon.empty(),"INVALID_RIVER_INTERMEDIATE: empty polygon");
        for(const auto& ring:polygon) {
            require(ring.size()>=4&&ring.front().x==ring.back().x&&ring.front().y==ring.back().y,
                "INVALID_RIVER_INTERMEDIATE: open ring");
            double area=0;const auto origin=ring.front();
            for(std::size_t i=0;i<ring.size();++i) {
                const auto point=ring[i];
                require(std::isfinite(point.x)&&std::isfinite(point.y)&&std::abs(point.x)<=180&&std::abs(point.y)<=90,
                    "INVALID_RIVER_INTERMEDIATE: coordinate");
                if(i)area+=(ring[i-1].x-origin.x)*(point.y-origin.y)-(point.x-origin.x)*(ring[i-1].y-origin.y);
            }
            require(std::isfinite(area)&&std::abs(area)>0,"INVALID_RIVER_INTERMEDIATE: degenerate ring");
        }
    }
}
QJSValue loadPinnedRiverQuantizer(QJSEngine& engine) {
    // Pinned polygon-clipping-calculation.js, unchanged source bytes. Only the
    // quantizer is evaluated: C++ owns the identical retry order so cancellation
    // can win between synchronous clipping calls. No retry on the ordinary path.
    const QByteArray original=R"RETRY(const RETRY_PRECISIONS = [9, 8, 7, 6];
const retryableClippingError = error => /SweepLine tree|Unable to find segment/i.test(String(error?.message || error || ''));

export function quantizePolygonCoordinates(value, precision) {
  const factor = 10 ** precision;
  const visit = item => {
    if (Array.isArray(item) && item.length >= 2
      && Number.isFinite(Number(item[0])) && Number.isFinite(Number(item[1]))) {
      return [
        Math.round(Number(item[0]) * factor) / factor,
        Math.round(Number(item[1]) * factor) / factor,
      ];
    }
    return Array.isArray(item) ? item.map(visit) : item;
  };
  return visit(value);
}

/** Runs one polygon-clipping method and retries only its known sweep failures. */
export function clippingOperationWithPrecisionRetry(clipper, method, ...inputs) {
  let originalError = null;
  for (const precision of [null, ...RETRY_PRECISIONS]) {
    try {
      const operationInputs = precision == null
        ? inputs
        : inputs.map(input => quantizePolygonCoordinates(input, precision));
      return clipper[method](...operationInputs);
    } catch (error) {
      if (!retryableClippingError(error)) throw error;
      originalError ||= error;
    }
  }
  throw originalError;
}
)RETRY";
    if(QCryptographicHash::hash(original,QCryptographicHash::Sha256).toHex()!=
        "79095890e49875610f27585c694a3f05e6c3322ef6a8b3e57c4a087236339406")
        throw std::runtime_error("RIVER_CLIPPING_RETRY_HASH_MISMATCH");
    const auto boundary=original.indexOf("\n/** Runs one polygon-clipping method");
    if(boundary<0)throw std::runtime_error("RIVER_CLIPPING_RETRY_EXPORT_MISMATCH");
    auto script=QString::fromUtf8(original.left(boundary));
    if(script.count("export function quantizePolygonCoordinates(")!=1)
        throw std::runtime_error("RIVER_CLIPPING_RETRY_EXPORT_MISMATCH");
    script.replace("export function quantizePolygonCoordinates(","function quantizePolygonCoordinates(");
    const auto loaded=engine.evaluate(script,"pinned-polygon-clipping-calculation.js");
    if(loaded.isError())throw std::runtime_error(loaded.toString().toStdString());
    auto quantize=engine.globalObject().property("quantizePolygonCoordinates");
    if(!quantize.isCallable())throw std::runtime_error("RIVER_CLIPPING_RETRY_EXPORT_MISMATCH");
    return quantize;
}
bool retryableRiverClippingError(const QJSValue& error) {
    const auto detail=error.toString();
    return detail.contains("SweepLine tree",Qt::CaseInsensitive)||detail.contains("Unable to find segment",Qt::CaseInsensitive);
}
GeometryOperationResult calculateWithValidation(const GeometryOperationRequest& request,
    const GeometryCancellation& cancelled,void (*validate)(const Geometry&),bool riverRetry=false) {
    const auto isCancelled=[&]{return cancelled && cancelled();};
    if(isCancelled())return {GeometryOperationStatus::Cancelled,{},{}};
    try {
        const auto operands=request.operands.empty()?std::vector<Geometry>{request.left,request.right}:request.operands;
        if(operands.size()<2)throw std::invalid_argument("INVALID_GEOMETRY_OPERATION: at least two operands required");
        for(const auto& operand:operands)validate(operand);
        QJSEngine engine;
        loadPinnedPolygonClipping(engine);
        const char* operation=nullptr;
        switch(request.operation) {
        case GeometryOperation::Union:operation="union";break;
        case GeometryOperation::Difference:operation="difference";break;
        case GeometryOperation::Intersection:operation="intersection";break;
        default:throw std::invalid_argument("INVALID_GEOMETRY_OPERATION");
        }
        if(isCancelled())return {GeometryOperationStatus::Cancelled,{},{}};
        auto function=engine.globalObject().property("polygonClipping").property(operation);
        if(!function.isCallable())throw std::runtime_error("GEOMETRY_KERNEL_INVALID_EXPORT");
        QJSValueList arguments;arguments.reserve(qsizetype(operands.size()));
        for(const auto& operand:operands)arguments.push_back(coordinates(engine,operand));
        auto result=function.call(arguments);
        // The synchronous kernel is not interrupted mid-call. Cancellation wins
        // over success AND errors and prevents a result entering a candidate.
        if(isCancelled())return {GeometryOperationStatus::Cancelled,{},{}};
        if(riverRetry&&result.isError()&&retryableRiverClippingError(result)) {
            const auto originalError=result;auto quantize=loadPinnedRiverQuantizer(engine);
            for(const auto precision:{9,8,7,6}) {
                if(isCancelled())return {GeometryOperationStatus::Cancelled,{},{}};
                QJSValueList rounded;rounded.reserve(arguments.size());
                for(const auto& argument:arguments) {
                    auto input=quantize.call({argument,precision});
                    if(input.isError())throw std::runtime_error(input.toString().toStdString());
                    rounded.push_back(std::move(input));
                }
                result=function.call(rounded);
                if(isCancelled())return {GeometryOperationStatus::Cancelled,{},{}};
                if(!result.isError())break;
                if(!retryableRiverClippingError(result))throw std::runtime_error(result.toString().toStdString());
            }
            if(result.isError())result=originalError;
        }
        if(result.isError())throw std::runtime_error(result.toString().toStdString());
        auto geometry=decode(result);
        if(geometry.polygons.empty())return {GeometryOperationStatus::Empty,{},{}};
        validate(geometry);
        if(isCancelled())return {GeometryOperationStatus::Cancelled,{},{}};
        return {GeometryOperationStatus::Completed,std::move(geometry),{}};
    } catch(const std::exception& error) {
        if(isCancelled())return {GeometryOperationStatus::Cancelled,{},{}};
        return {GeometryOperationStatus::Failed,{},error.what()};
    }
}
}
GeometryOperationResult calculateGeometry(const GeometryOperationRequest& request,
                                         const GeometryCancellation& cancelled) {
    return calculateWithValidation(request,cancelled,validatePolygon);
}

RiverGeometryIntermediateResult calculateRiverGeometryIntermediate(const GeometryOperationRequest& request,
    const GeometryCancellation& cancelled) {
    auto result=calculateWithValidation(request,cancelled,validateRiverIntermediate,true);
    return {result.status,std::move(result.geometry),std::move(result.detail)};
}

namespace {
bool near(Point a,Point b,double e=1e-8){return std::hypot(a.x-b.x,a.y-b.y)<=e;}
struct CutHit{double position=0,boundaryT=0;std::size_t polygon=0,segment=0;Point point{};};
std::optional<CutHit> cutIntersection(Point a,Point b,Point c,Point d,std::size_t lineIndex,std::size_t polygon,std::size_t segment){
    const Point r{b.x-a.x,b.y-a.y},s{d.x-c.x,d.y-c.y};const auto denominator=r.x*s.y-r.y*s.x;if(std::abs(denominator)<1e-12)return {};
    const Point ca{c.x-a.x,c.y-a.y};const auto t=(ca.x*s.y-ca.y*s.x)/denominator,u=(ca.x*r.y-ca.y*r.x)/denominator;
    if(t<-1e-9||t>1+1e-9||u<-1e-9||u>1+1e-9)return {};
    return CutHit{double(lineIndex)+std::clamp(t,0.,1.),std::clamp(u,0.,1.),polygon,segment,{a.x+r.x*std::clamp(t,0.,1.),a.y+r.y*std::clamp(t,0.,1.)}};
}
bool pointInRing(const Ring& ring,Point point){bool inside=false;for(std::size_t i=0,j=ring.size()-1;i<ring.size();j=i++){const auto a=ring[j],b=ring[i];if((a.y>point.y)!=(b.y>point.y)&&point.x<(b.x-a.x)*(point.y-a.y)/(b.y-a.y)+a.x)inside=!inside;}return inside;}
Ring ensureClosed(Ring ring){if(ring.size()>1&&!near(ring.front(),ring.back()))ring.push_back(ring.front());return ring;}
Ring walk(const Ring& ring,std::size_t start,std::size_t end,int step){Ring result{ring[start]};auto index=start;for(std::size_t guard=0;index!=end&&guard<=ring.size();++guard){index=(index+ring.size()+step)%ring.size();result.push_back(ring[index]);}if(index!=end)throw std::runtime_error("INVALID_CUT_ARC");return result;}
}
SplitGeometryResult splitGeometryByLine(const Geometry& source,const Ring& line,const GeometryCancellation& cancelled) {
    const auto stopped=[&]{return cancelled&&cancelled();};if(stopped())return {GeometryOperationStatus::Cancelled,{},{},""};
    try {
        if(line.size()<2||source.polygons.empty())throw std::invalid_argument("CUT_REQUIRES_TWO_POINTS");std::vector<CutHit> hits;
        for(std::size_t l=0;l+1<line.size();++l)for(std::size_t p=0;p<source.polygons.size();++p){if(source.polygons[p].empty())continue;const auto ring=ensureClosed(source.polygons[p].front());for(std::size_t s=0;s+1<ring.size();++s)if(auto hit=cutIntersection(line[l],line[l+1],ring[s],ring[s+1],l,p,s))hits.push_back(*hit);}
        std::sort(hits.begin(),hits.end(),[](const auto& a,const auto& b){return a.position<b.position;});hits.erase(std::unique(hits.begin(),hits.end(),[](const auto& a,const auto& b){return std::abs(a.position-b.position)<1e-7&&near(a.point,b.point,1e-7);}),hits.end());
        if(hits.size()!=2||hits[0].polygon!=hits[1].polygon)throw std::invalid_argument("CUT_MUST_CROSS_ONE_COMPONENT_TWICE");const auto component=hits[0].polygon;auto outer=ensureClosed(source.polygons[component].front());outer.pop_back();
        std::vector<std::pair<std::size_t,CutHit>> insertions{{hits[0].segment,hits[0]},{hits[1].segment,hits[1]}};Ring augmented;
        for(std::size_t i=0;i<outer.size();++i){augmented.push_back(outer[i]);std::vector<CutHit> at;for(const auto& [segment,hit]:insertions)if(segment==i&&hit.boundaryT>0.002&&hit.boundaryT<0.998)at.push_back(hit);std::sort(at.begin(),at.end(),[](const auto& a,const auto& b){return a.boundaryT<b.boundaryT;});for(const auto& hit:at)if(!near(augmented.back(),hit.point))augmented.push_back(hit.point);}
        const auto indexOf=[&](Point point){for(std::size_t i=0;i<augmented.size();++i)if(near(augmented[i],point,1e-7))return i;throw std::runtime_error("CUT_ENDPOINT_NOT_ON_BOUNDARY");};const auto first=indexOf(hits[0].point),last=indexOf(hits[1].point);if(first==last)throw std::runtime_error("CUT_ENDPOINTS_TOO_CLOSE");
        Ring cut{hits[0].point};for(std::size_t i=1;i+1<line.size();++i)if(double(i)>hits[0].position+1e-7&&double(i)<hits[1].position-1e-7)cut.push_back(line[i]);if(!near(cut.back(),hits[1].point))cut.push_back(hits[1].point);
        const auto forward=walk(augmented,first,last,1),backward=walk(augmented,first,last,-1);std::array<Ring,2> rings;
        for(int side=0;side<2;++side){rings[side]=cut;const auto& arc=side?backward:forward;for(auto i=arc.rbegin()+1;i+1!=arc.rend();++i)rings[side].push_back(*i);rings[side]=ensureClosed(std::move(rings[side]));}
        SplitGeometryResult result;result.status=GeometryOperationStatus::Completed;result.componentIndex=component;
        for(int side=0;side<2;++side){result.candidates[side].type="MultiPolygon";result.candidates[side].polygons={Polygon{rings[side]}};}
        for(std::size_t h=1;h<source.polygons[component].size();++h){const auto& hole=source.polygons[component][h];if(hole.empty())continue;const auto point=hole.front();if(pointInRing(rings[0],point))result.candidates[0].polygons.front().push_back(hole);else if(pointInRing(rings[1],point))result.candidates[1].polygons.front().push_back(hole);else throw std::runtime_error("CUT_SPLITS_HOLE");}
        for(const auto& candidate:result.candidates){GeometryStore validator;validator.insert({"cut",1},candidate);if(planarArea(candidate)<=1e-10)throw std::runtime_error("EMPTY_CUT_PART");}
        if(stopped())return {GeometryOperationStatus::Cancelled,{},{},""};return result;
    }catch(const std::exception& error){if(stopped())return {GeometryOperationStatus::Cancelled,{},{},""};return {GeometryOperationStatus::Failed,{},0,error.what()};}
}
}
