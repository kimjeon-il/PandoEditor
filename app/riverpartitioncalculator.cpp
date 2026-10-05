#include "riverpartitioncalculator.h"
#include "geometryruntime_p.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QResource>
#include <cmath>
#include <stdexcept>

static void initializeRiverResources() {
    static const bool initialized=[] { Q_INIT_RESOURCE(m972_river); return true; }();
    (void)initialized;
}
namespace pandoeditor {
namespace {
struct Cancelled {};
void check(const GeometryCancellation& cancelled) { if(cancelled && cancelled()) throw Cancelled{}; }
QByteArray read(const QString& path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error(("RIVER_RESOURCE_UNAVAILABLE: "+path).toStdString());return file.readAll();
}
QByteArray verified(const QString& path,const QByteArray& hash) {
    auto bytes=read(path);
    if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!=hash)
        throw std::runtime_error(("RIVER_RESOURCE_HASH_MISMATCH: "+path).toStdString());
    return bytes;
}
void jsCheck(const QJSValue& value) {
    if(value.isError())throw std::runtime_error((value.toString()+"\n"+value.property("stack").toString()).toStdString());
}
QJsonArray points(const Ring& ring) {
    QJsonArray array;for(const auto& p:ring) {
        if(!std::isfinite(p.x)||!std::isfinite(p.y))throw std::invalid_argument("RIVER_NONFINITE_COORDINATE");
        array.append(QJsonArray{p.x,p.y});
    }return array;
}
QJsonObject geometryJson(const Geometry& geometry) {
    QJsonArray coordinates;
    if(geometry.type=="Polygon"||geometry.type=="MultiPolygon") {
        for(const auto& polygon:geometry.polygons){QJsonArray rings;for(const auto& ring:polygon)rings.append(points(ring));coordinates.append(rings);}
        if(geometry.type=="Polygon") {
            if(coordinates.size()!=1)throw std::invalid_argument("RIVER_POLYGON_REQUIRES_ONE_COMPONENT");
            coordinates=coordinates[0].toArray();
        }
    }else if(geometry.type=="LineString"||geometry.type=="MultiLineString") {
        for(const auto& line:geometry.lines)coordinates.append(points(line));
        if(geometry.type=="LineString") {
            if(coordinates.size()!=1)throw std::invalid_argument("RIVER_LINE_REQUIRES_ONE_PART");
            coordinates=coordinates[0].toArray();
        }
    }else throw std::invalid_argument("RIVER_UNSUPPORTED_GEOMETRY");
    return {{"type",QString::fromStdString(geometry.type)},{"coordinates",coordinates}};
}
Point point(const QJsonValue& value) {
    const auto row=value.toArray();
    if(row.size()!=2||!row[0].isDouble()||!row[1].isDouble()||!std::isfinite(row[0].toDouble())||!std::isfinite(row[1].toDouble()))
        throw std::runtime_error("RIVER_INVALID_RESULT_POINT");
    return {row[0].toDouble(),row[1].toDouble()};
}
Geometry geometry(const QJsonObject& object) {
    Geometry result;result.type=object["type"].toString().toStdString();
    auto polygons=object["coordinates"].toArray();
    if(result.type=="Polygon")polygons=QJsonArray{polygons};
    else if(result.type!="MultiPolygon")throw std::runtime_error("RIVER_INVALID_RESULT_GEOMETRY");
    for(const auto& p:polygons){Polygon polygon;for(const auto& r:p.toArray()){Ring ring;for(const auto& v:r.toArray())ring.push_back(point(v));polygon.push_back(std::move(ring));}result.polygons.push_back(std::move(polygon));}
    return result;
}
QStringList strings(const QJsonValue& value) {QStringList result;for(const auto& item:value.toArray())result.push_back(item.toString());return result;}
RiverPartitionCell cell(const QJsonObject& row) {
    RiverPartitionCell result;result.attributes=row;result.key=row["key"].toString();result.donorCountryId=row["donorCountryId"].toString();
    result.componentKey=row["componentKey"].toString();result.algorithmRevision=row["algorithmRevision"].toString();
    result.geometry=geometry(row["geometry"].toObject());result.areaM2=row["areaM2"].toDouble();result.area=row["area"].toDouble();
    result.sourceRiverIds=strings(row["sourceRiverIds"]);
    for(const auto& value:row["riverBoundarySegments"].toArray()){const auto segment=value.toArray();if(segment.size()!=2)throw std::runtime_error("RIVER_INVALID_RESULT_SEGMENT");result.riverBoundarySegments.push_back({point(segment[0]),point(segment[1])});}
    return result;
}
RiverPartitionComponent component(const QJsonObject& row) {
    RiverPartitionComponent value;
    value.key=row["key"].toString();value.countryId=row["countryId"].toString();value.componentKey=row["componentKey"].toString();
    value.polygonIndex=row["polygonIndex"].toInt();value.sourcePolygonIndex=row["sourcePolygonIndex"].toInt();value.geometry=geometry(row["geometry"].toObject());
    value.isRiver=row["partitionKind"]=="river";if(value.isRiver)value.river=cell(row);value.attributes=row;return value;
}
RiverPartitionResult decode(QByteArray json,const GeometryCancellation& cancelled) {
    QJsonParseError parseError;
    const auto document=QJsonDocument::fromJson(json,&parseError);
    check(cancelled);
    if(parseError.error!=QJsonParseError::NoError)
        throw std::runtime_error(("RIVER_RESULT_JSON_PARSE_ERROR: "+parseError.errorString()).toStdString());
    if(!document.isObject())throw std::runtime_error("RIVER_INVALID_RESULT_SHAPE: object required");
    const auto object=document.object();
    if(object["status"]!="Completed" || !object["kernelInvoked"].isBool() || !object["kernelInvoked"].toBool() ||
       !object["inputUnchanged"].isBool() || !object["composeInputUnchanged"].isBool() ||
       !object["result"].isObject() || !object["composed"].isObject() ||
       (object.contains("identity") && !object["identity"].isObject()))
        throw std::runtime_error("RIVER_INVALID_RESULT_SHAPE: result/composition envelope required");
    const auto result=object["result"].toObject(),composed=object["composed"].toObject();
    if(!result["candidates"].isArray() || !result["donorResults"].isArray() || !result["diagnostics"].isObject() ||
       !composed["items"].isArray() || !composed["invalidDonorIds"].isArray() ||
       !composed["splitComponentCount"].isDouble() || !composed["riverCandidateCount"].isDouble())
        throw std::runtime_error("RIVER_INVALID_RESULT_SHAPE: result/composition fields required");
    RiverPartitionResult output;
    for(const auto& value:result["candidates"].toArray()){check(cancelled);output.candidates.push_back(cell(value.toObject()));}
    for(const auto& value:result["donorResults"].toArray()){
        check(cancelled);const auto row=value.toObject();RiverPartitionDonorResult donor;
        donor.donorCountryId=row["donorCountryId"].toString();donor.reason=row["reason"].toString();donor.candidateCount=row["candidateCount"].toInt();
        const auto status=row["status"].toString();
        if(status=="ready")donor.status=RiverDonorStatus::Ready;else if(status=="empty")donor.status=RiverDonorStatus::Empty;
        else if(status=="invalid")donor.status=RiverDonorStatus::Invalid;else throw std::runtime_error("RIVER_INVALID_DONOR_STATUS");
        output.donors.push_back(std::move(donor));
    }
    for(const auto& value:composed["items"].toArray()) {
        check(cancelled);output.components.push_back(component(value.toObject()));
    }
    const auto d=result["diagnostics"].toObject();auto& diag=output.diagnostics;
    diag.algorithmRevision=d["algorithmRevision"].toString();diag.hydroRevision=d["hydroRevision"];diag.computeMs=d["computeMs"].toDouble();
#define COUNTER(name) diag.name=d[#name].toInt()
    COUNTER(scannedDonors);COUNTER(scannedRiverFeatures);COUNTER(scannedRiverParts);COUNTER(scannedRiverSegments);
    COUNTER(boundaryIntersectionCount);COUNTER(riverIntersectionCount);COUNTER(boundaryFollowingEdges);
    COUNTER(retainedRiverEdges);COUNTER(prunedRiverEdges);COUNTER(tracedFaceCount);COUNTER(candidateCount);
#undef COUNTER
    output.invalidDonorIds=strings(composed["invalidDonorIds"]);output.splitComponentCount=composed["splitComponentCount"].toInt();output.riverCandidateCount=composed["riverCandidateCount"].toInt();
    output.inputUnchanged=object["inputUnchanged"].toBool();output.compositionInputUnchanged=object["composeInputUnchanged"].toBool();
    const auto identity=object["identity"].toObject();output.donorRevisionStrings=strings(identity["donorRevisionStrings"]);output.editedRiverSignature=identity["editedRiverSignature"].toString();
    check(cancelled);output.json=std::move(json);output.status=RiverPartitionStatus::Completed;return output;
}
QJsonObject cellJson(const RiverPartitionCell& value) {
    if(!std::isfinite(value.area)||!std::isfinite(value.areaM2))throw std::invalid_argument("RIVER_NONFINITE_AREA");
    auto row=value.attributes;
    row["key"]=value.key;row["donorCountryId"]=value.donorCountryId;row["componentKey"]=value.componentKey;
    row["algorithmRevision"]=value.algorithmRevision;row["geometry"]=geometryJson(value.geometry);
    row["area"]=value.area;row["areaM2"]=value.areaM2;
    row["sourceRiverIds"]=QJsonArray::fromStringList(value.sourceRiverIds);
    QJsonArray segments;for(const auto& segment:value.riverBoundarySegments)segments.append(points({segment[0],segment[1]}));
    row["riverBoundarySegments"]=segments;return row;
}
QJsonObject baseComponentJson(const RiverBaseComponent& value) {
    auto row=value.attributes;row["key"]=value.key;row["countryId"]=value.countryId;
    row["polygonIndex"]=value.polygonIndex;row["sourcePolygonIndex"]=value.sourcePolygonIndex;
    if(!value.componentKey.isEmpty())row["componentKey"]=value.componentKey;
    row["geometry"]=geometryJson(value.geometry);return row;
}
QJSValue parse(QJSEngine& engine,const QJsonDocument& input) {
    auto value=engine.globalObject().property("JSON").property("parse").call({QString::fromUtf8(input.toJson(QJsonDocument::Compact))});
    jsCheck(value);return value;
}
QByteArray stringify(QJSEngine& engine,const QJSValue& value) {
    const auto result=engine.globalObject().property("JSON").property("stringify").call({value});jsCheck(result);
    return result.toString().toUtf8();
}
QJsonDocument jsonDocument(const QByteArray& bytes) {
    QJsonParseError error;const auto document=QJsonDocument::fromJson(bytes,&error);
    if(error.error!=QJsonParseError::NoError)throw std::runtime_error("RIVER_PRESENTATION_JSON_INVALID");
    return document;
}
QJSValue loadNormalizer(QJSEngine& engine) {
    initializeRiverResources();engine.globalObject().setProperty("globalThis",engine.globalObject());
    jsCheck(engine.evaluate(QString::fromUtf8(verified(":/river/original/polygon-geometry.js",
        "cc987c4076861a02a5d60720ebf536908175a9f1a605a86701ae4cf50c3a3fb5")),":/river/original/polygon-geometry.js"));
    const auto normalize=engine.globalObject().property("PandoLabPolygonGeometry").property("normalizePolygonGeometry");
    if(!normalize.isCallable())throw std::runtime_error("RIVER_NORMALIZER_UNAVAILABLE");
    return normalize;
}
std::optional<Geometry> normalizeGeometry(QJSEngine& engine,const QJSValue& normalize,
    const Geometry& input,const GeometryCancellation& cancelled) {
    check(cancelled);const auto result=normalize.call({parse(engine,QJsonDocument(geometryJson(input)))});
    check(cancelled);jsCheck(result);if(result.isNull())return {};
    const auto document=jsonDocument(stringify(engine,result));
    check(cancelled);if(!document.isObject())throw std::runtime_error("RIVER_NORMALIZED_GEOMETRY_INVALID");
    return geometry(document.object());
}
template<class Result> Result presentationFailure(const GeometryCancellation& cancelled,const std::exception* error=nullptr) {
    Result result;
    if(!error||(cancelled&&cancelled()))result.status=RiverPartitionStatus::Cancelled;
    else result.detail=QString::fromUtf8(error->what());
    return result;
}
RiverPartitionResult stopped() {RiverPartitionResult result;result.status=RiverPartitionStatus::Cancelled;return result;}
RiverPartitionResult failed(const std::exception& error,const GeometryCancellation& cancelled) {
    if(cancelled&&cancelled())return stopped();RiverPartitionResult result;result.detail=QString::fromUtf8(error.what());return result;
}
}
RiverGeometryNormalizationResult normalizeRiverGeometry(const Geometry& input,const GeometryCancellation& cancelled) {
    try {
        check(cancelled);QJSEngine engine;const auto normalize=loadNormalizer(engine);check(cancelled);
        RiverGeometryNormalizationResult output;output.geometry=normalizeGeometry(engine,normalize,input,cancelled);
        check(cancelled);output.status=RiverPartitionStatus::Completed;return output;
    }catch(const Cancelled&){return presentationFailure<RiverGeometryNormalizationResult>(cancelled);}
    catch(const std::exception& error){return presentationFailure<RiverGeometryNormalizationResult>(cancelled,&error);}
}
RiverPartitionNormalizationResult normalizeRiverPartitionCandidates(const std::vector<RiverPartitionCell>& cells,
    const GeometryCancellation& cancelled) {
    try {
        check(cancelled);QJSEngine engine;const auto normalize=loadNormalizer(engine);check(cancelled);
        RiverPartitionNormalizationResult output;QJsonArray rows;
        for(const auto& input:cells) {
            check(cancelled);auto value=normalizeGeometry(engine,normalize,input.geometry,cancelled);
            if(!value||input.donorCountryId.isEmpty())continue;
            auto candidate=input;candidate.geometry=std::move(*value);
            candidate.attributes=cellJson(candidate);rows.append(candidate.attributes);output.candidates.push_back(std::move(candidate));
        }
        check(cancelled);output.json=QJsonDocument(rows).toJson(QJsonDocument::Compact);
        output.status=RiverPartitionStatus::Completed;return output;
    }catch(const Cancelled&){return presentationFailure<RiverPartitionNormalizationResult>(cancelled);}
    catch(const std::exception& error){return presentationFailure<RiverPartitionNormalizationResult>(cancelled,&error);}
}
RiverPartitionCompositionResult composeRiverPartitionComponents(const std::vector<RiverBaseComponent>& components,
    const std::vector<RiverPartitionCell>& normalizedCells,const std::vector<RiverPartitionDonorResult>& donors,
    const GeometryCancellation& cancelled) {
    try {
        check(cancelled);initializeRiverResources();
        verified(":/river/original/river-territory-partition.js","18b32eb7db238beaed99bce8bac980e547a48c785a7bd2071fa56fa63f51db24");
        verified(":/river/original/planar-graph-faces.js","283da7701c21cb80e4fd9ef97e8f68e69a8d6f0ec9fc611ee8c23cab45d6c804");
        verified(":/river/adapted/river-territory-partition.js","b27fd1e62bb3fbee977089686eeb5713d9bde60c49230b3e2453c2200b4b773a");
        verified(":/river/adapted/planar-graph-faces.js","283da7701c21cb80e4fd9ef97e8f68e69a8d6f0ec9fc611ee8c23cab45d6c804");
        QJSEngine engine;engine.globalObject().setProperty("globalThis",engine.globalObject());
        jsCheck(engine.evaluate(QString::fromUtf8(read(":/river/platform.js")),":/river/platform.js"));
        const auto module=engine.importModule(":/river/adapted/river-territory-partition.js");check(cancelled);jsCheck(module);
        QJsonArray base,candidates,results;
        for(const auto& value:components){check(cancelled);base.append(baseComponentJson(value));}
        for(const auto& value:normalizedCells){check(cancelled);candidates.append(cellJson(value));}
        for(const auto& value:donors){check(cancelled);results.append(QJsonObject{{"donorCountryId",value.donorCountryId},
            {"reason",value.reason},{"candidateCount",value.candidateCount},
            {"status",value.status==RiverDonorStatus::Invalid?"invalid":value.status==RiverDonorStatus::Ready?"ready":"empty"}});}
        const auto input=parse(engine,QJsonDocument(QJsonObject{{"components",base},{"candidates",candidates},{"donorResults",results}}));
        check(cancelled);const auto result=module.property("composeRiverBoundaryTerritoryComponents").call({input});
        check(cancelled);jsCheck(result);auto bytes=stringify(engine,result);check(cancelled);
        const auto document=jsonDocument(bytes);if(!document.isObject())throw std::runtime_error("RIVER_COMPOSITION_SHAPE_INVALID");
        const auto row=document.object();
        if(!row["items"].isArray()||!row["invalidDonorIds"].isArray()||!row["splitComponentCount"].isDouble()||!row["riverCandidateCount"].isDouble())
            throw std::runtime_error("RIVER_COMPOSITION_SHAPE_INVALID");
        RiverPartitionCompositionResult output;
        for(const auto& value:row["items"].toArray()){check(cancelled);output.components.push_back(component(value.toObject()));}
        output.invalidDonorIds=strings(row["invalidDonorIds"]);output.splitComponentCount=row["splitComponentCount"].toInt();
        output.riverCandidateCount=row["riverCandidateCount"].toInt();output.json=std::move(bytes);
        check(cancelled);output.status=RiverPartitionStatus::Completed;return output;
    }catch(const Cancelled&){return presentationFailure<RiverPartitionCompositionResult>(cancelled);}
    catch(const std::exception& error){return presentationFailure<RiverPartitionCompositionResult>(cancelled,&error);}
}
RiverPartitionResult calculateRiverPartitionsJson(const QByteArray& json,const GeometryCancellation& cancelled) {
    try {
        check(cancelled);initializeRiverResources();
        verified(":/river/original/river-territory-partition.js","18b32eb7db238beaed99bce8bac980e547a48c785a7bd2071fa56fa63f51db24");
        verified(":/river/original/planar-graph-faces.js","283da7701c21cb80e4fd9ef97e8f68e69a8d6f0ec9fc611ee8c23cab45d6c804");
        verified(":/river/adapted/river-territory-partition.js","b27fd1e62bb3fbee977089686eeb5713d9bde60c49230b3e2453c2200b4b773a");
        verified(":/river/adapted/planar-graph-faces.js","283da7701c21cb80e4fd9ef97e8f68e69a8d6f0ec9fc611ee8c23cab45d6c804");
        check(cancelled);
        // The captured official Chromium corpus matches stock Qt math. No
        // numerical override is installed; other runtime profiles need evidence.
        QJSEngine engine;
        engine.globalObject().setProperty("globalThis",engine.globalObject());
        jsCheck(engine.evaluate(QString::fromUtf8(read(":/river/platform.js")),":/river/platform.js"));
        loadPinnedPolygonClipping(engine);
        jsCheck(engine.evaluate(QString::fromUtf8(verified(":/river/original/polygon-geometry.js","cc987c4076861a02a5d60720ebf536908175a9f1a605a86701ae4cf50c3a3fb5")),":/river/original/polygon-geometry.js"));
        const auto module=engine.importModule(":/river/adapted/river-territory-partition.js");jsCheck(module);
        check(cancelled);
        auto input=engine.globalObject().property("JSON").property("parse").call({QString::fromUtf8(json)});jsCheck(input);
        if(!input.isObject()||input.isArray())throw std::invalid_argument("RIVER_REQUEST_OBJECT_REQUIRED");
        if(!input.property("liveHydroRevisionPrefix").isUndefined()) {
            if(!input.property("liveHydroRevisionPrefix").isString())
                throw std::invalid_argument("RIVER_LIVE_HYDRO_REVISION_PREFIX_STRING_REQUIRED");
            // Application metadata only. Preserve the pinned bridge and kernel;
            // clone first so their nonfinite/non-JSON rejection remains intact.
            const auto prepare=engine.evaluate(QStringLiteral(R"JS((function(row) {
                row=structuredClone(row);
                row.request=row.request||{};
                row.request.hydroRevision=row.liveHydroRevisionPrefix+(row.signatureEdits||[])
                    .map(f=>String(f.id)+':'+JSON.stringify(f.geometry.coordinates||[])).sort().join('|');
                return row;
            }))JS"));jsCheck(prepare);
            check(cancelled);input=prepare.call({input});check(cancelled);jsCheck(input);
        }
        const auto bridge=engine.evaluate(QString::fromUtf8(read(":/river/bridge.js")),":/river/bridge.js");jsCheck(bridge);
        check(cancelled);
        const auto result=bridge.call({input,module,engine.globalObject().property("polygonClipping")});
        check(cancelled);jsCheck(result); // Cancellation wins even when JS threw.
        const auto text=engine.globalObject().property("JSON").property("stringify").call({result});
        check(cancelled);jsCheck(text);
        auto output=decode(text.toString().toUtf8(),cancelled);
        const auto present=engine.evaluate(QString::fromUtf8(read(":/river/presentation.js")),":/river/presentation.js");jsCheck(present);
        check(cancelled);const auto shown=present.call({input,result,module,engine.globalObject().property("PandoLabPolygonGeometry").property("normalizePolygonGeometry")});
        check(cancelled);jsCheck(shown);
        const auto shownText=engine.globalObject().property("JSON").property("stringify").call({shown});jsCheck(shownText);
        QJsonParseError presentationError;const auto document=QJsonDocument::fromJson(shownText.toString().toUtf8(),&presentationError);
        check(cancelled);
        if(presentationError.error!=QJsonParseError::NoError || !document.isObject())throw std::runtime_error("RIVER_PRESENTATION_JSON_INVALID");
        const auto presentation=document.object();
        if(!presentation["candidates"].isArray() || !presentation["composed"].isObject() || !presentation["composed"].toObject()["items"].isArray())throw std::runtime_error("RIVER_PRESENTATION_SHAPE_INVALID");
        for(const auto& value:presentation["candidates"].toArray()){check(cancelled);output.presentationCandidates.push_back(cell(value.toObject()));}
        for(const auto& value:presentation["composed"].toObject()["items"].toArray()){check(cancelled);output.presentationComponents.push_back(component(value.toObject()));}
        output.presentationJson=shownText.toString().toUtf8();
        const auto trace=engine.evaluate(QString::fromUtf8(read(":/river/workspace-observation.js")),":/river/workspace-observation.js");jsCheck(trace);
        const auto traced=trace.call({module,input.property("request")});check(cancelled);jsCheck(traced);
        const auto traceText=engine.globalObject().property("JSON").property("stringify").call({traced});jsCheck(traceText);
        QJsonParseError traceError;const auto traceDocument=QJsonDocument::fromJson(traceText.toString().toUtf8(),&traceError);check(cancelled);
        if(traceError.error!=QJsonParseError::NoError || !traceDocument.isArray())throw std::runtime_error("RIVER_WORKSPACE_JSON_INVALID");
        output.workspaceJson=traceText.toString().toUtf8();check(cancelled);return output;
    }catch(const Cancelled&) {return stopped();}catch(const std::exception& error){return failed(error,cancelled);}
}
RiverPartitionResult calculateRiverPartitions(const RiverPartitionRequest& request,const GeometryCancellation& cancelled) {
    try {
        check(cancelled);QJsonArray donors,features,components,live,edits;
        for(const auto& donor:request.donors){check(cancelled);QJsonObject row{{"countryId",donor.countryId},{"geometry",geometryJson(donor.geometry)}};
            if(!donor.geometryRevision.isUndefined())row["geometryRevision"]=donor.geometryRevision;
            if(donor.revisionMode==RiverRevisionMode::LiveCoordinates)live.append(donors.size());donors.append(row);}
        for(const auto& feature:request.riverFeatures){check(cancelled);QJsonObject properties{{"pandolab_id",feature.pandolabId},{"category","river"}};
            if(feature.logicalFid)properties["__logicalFid"]=double(*feature.logicalFid);
            if(feature.sourceFeatureId)properties["source_id"]=*feature.sourceFeatureId;
            features.append(QJsonObject{{"type","Feature"},{"id",feature.id},{"properties",properties},{"geometry",geometryJson(feature.geometry)}});}
        for(const auto& component:request.components){check(cancelled);auto row=component.attributes;row["key"]=component.key;row["countryId"]=component.countryId;
            row["polygonIndex"]=component.polygonIndex;row["sourcePolygonIndex"]=component.sourcePolygonIndex;
            if(!component.componentKey.isEmpty())row["componentKey"]=component.componentKey;row["geometry"]=geometryJson(component.geometry);components.append(row);}
        for(const auto& edit:request.signatureEdits)edits.append(QJsonObject{{"id",edit.id},{"geometry",geometryJson(edit.geometry)}});
        QJsonObject input{{"donors",donors},{"riverFeatures",features},{"hydroRevision",request.hydroRevision}};
        if(!request.configOverrides.isEmpty())input["config"]=request.configOverrides;
        if(request.algorithmRevision)input["algorithmRevision"]=*request.algorithmRevision;
        QJsonObject row{{"request",input},{"components",components},{"liveDonorIndices",live},{"signatureEdits",edits},{"includeIdentity",true}};
        if(request.liveHydroRevisionPrefix)row["liveHydroRevisionPrefix"]=*request.liveHydroRevisionPrefix;
        check(cancelled);return calculateRiverPartitionsJson(QJsonDocument(row).toJson(QJsonDocument::Compact),cancelled);
    }catch(const Cancelled&){return stopped();}catch(const std::exception& error){return failed(error,cancelled);}
}
}
