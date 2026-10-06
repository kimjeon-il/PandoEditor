#include "projectcodec.h"
#include <pandoeditor/timeline-storage.h>
#include "webjson.h"
#include "builtinworldpolicy.h"
#include <pandoeditor/objectproperties.h>
#include "losslessjson.h"
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QBuffer>
#include <QImageReader>
#include <QSvgRenderer>
#include <climits>
#include <limits>
#include <set>

namespace projectcodec {
namespace {
using namespace pandoeditor;
using V=losslessjson::Value;
using losslessjson::require;
V currentWebHeader();
void validateHeader(const V&,const ProjectDocument&);
void validateExchangeMetadata(const ProjectDocument&);
bool presentationGroup(const std::string& key) {
    return std::set<std::string>{"countries","subunits","regions","distributions","rivers","lakes","hydro","genericFeatures","labels","countryLabels","terrain"}.count(key);
}
bool presentationSymbol(const std::string& key) {
    return std::set<std::string>{"basemapLabels","countryFlags","subunitLabels","subunitFlags","regionLabels","regionFlags"}.count(key);
}
V object(std::initializer_list<std::pair<const std::string,V>> entries) { V v=V::obj(); v.object=entries; return v; }
const V& field(const V& v,const std::string& key) {
    require(v.kind==V::Object,"INVALID_JSON: expected object");
    const auto it=v.object.find(key); require(it!=v.object.end(),"INVALID_JSON: missing required field"); return it->second;
}
std::string str(const V& v) { require(v.kind==V::String,"INVALID_JSON: expected string"); return v.string; }
void validateEmbeddedFlag(const std::string& value) {
    require(value.size()<=48u*1024u*1024u,"INVALID_EMBEDDED_FLAG_IMAGE: encoded payload limit");
    static const QRegularExpression pattern(QStringLiteral("^data:image/([A-Za-z0-9.+-]+);base64,([A-Za-z0-9+/]*={0,2})$"));
    const auto match=pattern.match(QString::fromStdString(value));
    require(match.hasMatch(),"INVALID_EMBEDDED_FLAG_IMAGE");
    auto bytes=QByteArray::fromBase64(match.captured(2).toLatin1(),QByteArray::AbortOnBase64DecodingErrors);
    require(!bytes.isEmpty()&&bytes.size()<=32ll*1024*1024,"INVALID_EMBEDDED_FLAG_IMAGE: payload limit");
    if(match.captured(1).compare("svg+xml",Qt::CaseInsensitive)==0) {
        // Preserve arbitrary vector dimensions without allocating an intrinsic raster.
        require(QSvgRenderer(bytes).isValid(),"INVALID_EMBEDDED_FLAG_IMAGE");return;
    }
    QBuffer buffer(&bytes);require(buffer.open(QIODevice::ReadOnly),"INVALID_EMBEDDED_FLAG_IMAGE");
    QImageReader reader(&buffer);
    require(reader.canRead()&&reader.format()!="svg","INVALID_EMBEDDED_FLAG_IMAGE");
    const auto size=reader.size();
    require(size.isValid()&&qint64(size.width())*size.height()<=16ll*1024*1024,"INVALID_EMBEDDED_FLAG_IMAGE: pixel limit");
    require(!reader.read().isNull(),"INVALID_EMBEDDED_FLAG_IMAGE");
}
void validateWebObjectIds(const ProjectDocument& d) {
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-8][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$"));
    const auto rows=[&](const auto& values){for(const auto& value:values)require(pattern.match(QString::fromStdString(value.id)).hasMatch(),"INVALID_WEB_OBJECT_UUID");};
    rows(d.labels);rows(d.hydro);rows(d.genericFeatures);rows(d.distributionLayers);rows(d.distributionEntries);
}
bool boolean(const V& v) { require(v.kind==V::Bool,"INVALID_JSON: expected boolean"); return v.raw=="true"; }
double number(const V& v) {
    require(v.kind==V::Number,"INVALID_JSON: expected numeric value"); bool ok=false;
    double n=v.raw.toDouble(&ok); require(ok && std::isfinite(n),"INVALID_JSON: nonfinite typed number"); return n;
}
std::uint32_t integer(const V& v) {
    require(v.kind==V::Number && !v.raw.isEmpty(),"INVALID_JSON: expected unsigned integer token");
    for (char c:v.raw) require(c>='0' && c<='9',"INVALID_JSON: expected unsigned integer token");
    bool ok=false; const auto n=v.raw.toULongLong(&ok);
    require(ok && n<=std::numeric_limits<std::uint32_t>::max(),"INVALID_JSON: integer overflow");
    return static_cast<std::uint32_t>(n);
}
const std::vector<V>& array(const V& v) { require(v.kind==V::Array,"INVALID_JSON: expected array"); return v.array; }
std::string pointer(const std::string& base,const std::string& key) {
    std::string escaped;
    for (char c:key) { if (c=='~') escaped+="~0"; else if (c=='/') escaped+="~1"; else escaped+=c; }
    return base+"/"+escaped;
}
void unknown(ProjectDocument& d,int schema,const V& v,const std::string& path,std::initializer_list<const char*> known) {
    require(v.kind==V::Object,"INVALID_JSON: expected object");
    std::set<std::string> names; for (auto k:known) names.insert(k);
    for (const auto& [key,value]:v.object) if (!names.count(key)) throw std::invalid_argument("UNSUPPORTED_FIELD: "+pointer(path,key));
}
std::uint32_t color(const V& v) {
    const auto text=QString::fromStdString(str(v));
    require(QRegularExpression("^#[0-9a-fA-F]{6}$").match(text).hasMatch(),"INVALID_JSON: invalid RGB color");
    return text.mid(1).toUInt(nullptr,16);
}
V colorValue(std::uint32_t n) { return V::str(QString("#%1").arg(n,6,16,QChar('0')).toStdString()); }
ObjectRef ref(const V& v,ProjectDocument& d,const std::string& path) {
    ObjectRef r{str(field(v,"domain")),str(field(v,"id"))};
    const std::set<std::string> domains={"territorial","distributionLayer","distributionEntry","label","hydro","generic","userLayer"};
    require(domains.count(r.domain),"INVALID_JSON: invalid ObjectRef domain");
    unknown(d,10,v,path,{"domain","id"}); return r;
}
V refValue(const ObjectRef& r) { return object({{"domain",V::str(r.domain)},{"id",V::str(r.id)}}); }
GeometryRef geometryRef(const V& v,ProjectDocument& d,const std::string& path) {
    GeometryRef r{str(field(v,"id")),integer(field(v,"version"))};
    unknown(d,10,v,path,{"id","version"}); return r;
}
V geometryRefValue(const GeometryRef& r) { return object({{"id",V::str(r.id)},{"version",V::num(r.version)}}); }
std::optional<std::string> endpoint(const V& v,ProjectDocument& d,const std::string& path) {
    if (v.kind==V::Null) return {};
    auto text=str(field(v,"text")); auto temporal=parseTemporal(text);
    require(str(field(v,"precision"))==temporal.precision,"INVALID_DATE: precision does not match text");
    unknown(d,10,v,path,{"text","precision"}); return text;
}
Validity validity(const V& v,ProjectDocument& d,const std::string& path) {
    Validity r{endpoint(field(v,"from"),d,path+"/from"),endpoint(field(v,"to"),d,path+"/to")};
    unknown(d,10,v,path,{"from","to"}); temporalBounds(r); return r;
}
V endpointValue(const std::optional<std::string>& s) {
    if (!s) return {};
    return object({{"text",V::str(*s)},{"precision",V::str(parseTemporal(*s).precision)}});
}
V validityValue(const Validity& v) { return object({{"from",endpointValue(v.from)},{"to",endpointValue(v.to)}}); }
LibraryOrigin libraryOrigin(const V& v,ProjectDocument& d,const std::string& path) {
    LibraryOrigin origin;
    origin.libraryId=str(field(v,"libraryId"));
    origin.geometryVersionId=str(field(v,"geometryVersionId"));
    const auto& date=field(v,"referenceDate");
    if(date.kind!=V::Null)origin.referenceDate=str(date);
    origin.sourceId=str(field(v,"sourceId"));
    origin.sourceVersion=str(field(v,"sourceVersion"));
    origin.certainty=str(field(v,"certainty"));
    origin.datePrecision=str(field(v,"datePrecision"));
    origin.partial=boolean(field(v,"partial"));
    for(const auto& ref:array(field(v,"missingLibraryRefs")))
        origin.missingLibraryRefs.push_back(str(ref));
    unknown(d,10,v,path,
            {"libraryId","geometryVersionId","referenceDate","sourceId","sourceVersion",
             "certainty","datePrecision","partial","missingLibraryRefs"});
    return origin;
}
V libraryOriginValue(const LibraryOrigin& origin) {
    V missing=V::arr();
    for(const auto& ref:origin.missingLibraryRefs)missing.array.push_back(V::str(ref));
    return object({{"libraryId",V::str(origin.libraryId)},
                   {"geometryVersionId",V::str(origin.geometryVersionId)},
                   {"referenceDate",origin.referenceDate?V::str(*origin.referenceDate):V{}},
                   {"sourceId",V::str(origin.sourceId)},{"sourceVersion",V::str(origin.sourceVersion)},
                   {"certainty",V::str(origin.certainty)},{"datePrecision",V::str(origin.datePrecision)},
                   {"partial",V::boolean(origin.partial)},{"missingLibraryRefs",missing}});
}
Point point(const V& v) {
    const auto& a=array(v); require(a.size()==2,"INVALID_GEOMETRY: expected two coordinates");
    return {number(a[0]),number(a[1])};
}
Ring line(const V& v) { Ring r; for (const auto& p:array(v)) r.push_back(point(p)); return r; }
Polygon polygon(const V& v) { Polygon p; for (const auto& r:array(v)) p.push_back(line(r)); return p; }
Geometry geometry(const V& v,ProjectDocument& d,int schema,const std::string& path) {
    Geometry g; g.type=str(field(v,"type")); const auto& c=field(v,"coordinates");
    if (g.type=="Point") g.points.push_back(point(c));
    else if (g.type=="MultiPoint") g.points=line(c);
    else if (g.type=="LineString") g.lines.push_back(line(c));
    else if (g.type=="MultiLineString") for (const auto& l:array(c)) g.lines.push_back(line(l));
    else if (g.type=="Polygon") g.polygons.push_back(polygon(c));
    else if (g.type=="MultiPolygon") for (const auto& p:array(c)) g.polygons.push_back(polygon(p));
    else require(false,"INVALID_GEOMETRY: unsupported GeoJSON kind");
    unknown(d,schema,v,path,{"type","coordinates"}); return g;
}
V pointValue(Point p) { V v=V::arr(); v.array={V::num(p.x),V::num(p.y)}; return v; }
V lineValue(const Ring& l) { V v=V::arr(); for (auto p:l) v.array.push_back(pointValue(p)); return v; }
V polygonValue(const Polygon& p) { V v=V::arr(); for (const auto& l:p) v.array.push_back(lineValue(l)); return v; }
V geometryValue(const Geometry& g) {
    V c=V::arr();
    if (g.type=="Point") { require(g.points.size()==1,"INVALID_GEOMETRY: Point"); c=pointValue(g.points[0]); }
    else if (g.type=="MultiPoint") c=lineValue(g.points);
    else if (g.type=="LineString") { require(g.lines.size()==1,"INVALID_GEOMETRY: LineString"); c=lineValue(g.lines[0]); }
    else if (g.type=="MultiLineString") for (const auto& l:g.lines) c.array.push_back(lineValue(l));
    else if (g.type=="Polygon") { require(g.polygons.size()==1,"INVALID_GEOMETRY: Polygon"); c=polygonValue(g.polygons[0]); }
    else if (g.type=="MultiPolygon") for (const auto& p:g.polygons) c.array.push_back(polygonValue(p));
    else require(false,"INVALID_GEOMETRY: unsupported GeoJSON kind");
    return object({{"type",V::str(g.type)},{"coordinates",c}});
}
Validity recordInterval(const V& v,bool timelineOwned=false) {
    // Preserve endpoint spelling/precision. Timeline normalization owns date
    // validation and TIMELINE_INTERVAL; other content retains its wire checks.
    const auto endpoint=[timelineOwned](const V& value)->std::optional<std::string>{if(value.kind==V::Null)return {};const auto text=str(value);if(!timelineOwned)(void)parseTemporal(text);return text;};
    return {endpoint(field(v,"validFrom")),endpoint(field(v,"validTo"))};
}
TimelineRecords timelineRecords(const V& value,ProjectDocument& d) {
    TimelineRecords result;result.schemaVersion=integer(field(value,"schemaVersion"));
    unknown(d,10,value,"/timelineRecords",{"schemaVersion","lifetimes","geometryBindings","parentRelations"});
    for(const auto& row:array(field(value,"lifetimes"))){unknown(d,10,row,"/timelineRecords/lifetimes",{"id","entityId","validFrom","validTo"});result.lifetimes.push_back({str(field(row,"id")),str(field(row,"entityId")),recordInterval(row,true)});}
    for(const auto& row:array(field(value,"geometryBindings"))){unknown(d,10,row,"/timelineRecords/geometryBindings",{"id","entityId","validFrom","validTo","geometryRef"});result.geometryBindings.push_back({str(field(row,"id")),str(field(row,"entityId")),recordInterval(row,true),geometryRef(field(row,"geometryRef"),d,"/timelineRecords/geometryBindings/geometryRef")});}
    for(const auto& row:array(field(value,"parentRelations"))){unknown(d,10,row,"/timelineRecords/parentRelations",{"id","entityId","validFrom","validTo","parentId","coverageMode"});result.parentRelations.push_back({str(field(row,"id")),str(field(row,"entityId")),recordInterval(row,true),str(field(row,"parentId")),str(field(row,"coverageMode"))});}
    return result;
}
V timelineRecordsValue(const TimelineRecords& records) {
    V life=V::arr(),bindings=V::arr(),parents=V::arr();
    auto row=[](const auto& record){return object({{"id",V::str(record.id)},{"entityId",V::str(record.entityId)},{"validFrom",record.validity.from?V::str(*record.validity.from):V{}},{"validTo",record.validity.to?V::str(*record.validity.to):V{}}});};
    for(const auto& record:records.lifetimes)life.array.push_back(row(record));
    for(const auto& record:records.geometryBindings){auto value=row(record);value.object["geometryRef"]=geometryRefValue(record.geometryRef);bindings.array.push_back(std::move(value));}
    for(const auto& record:records.parentRelations){auto value=row(record);value.object["parentId"]=V::str(record.parentId);value.object["coverageMode"]=V::str(record.coverageMode);parents.array.push_back(std::move(value));}
    return object({{"schemaVersion",V::num(records.schemaVersion)},{"lifetimes",life},{"geometryBindings",bindings},{"parentRelations",parents}});
}
void restoreArchive(const V& root,ProjectDocument& d) {
    TimelineStorageSnapshot candidate;candidate.records=timelineRecords(field(root,"timelineRecords"),d);
    for(const auto& row:array(field(root,"geometries"))){unknown(d,10,row,"/geometries",{"id","version","geojson"});candidate.geometries.push_back({{str(field(row,"id")),integer(field(row,"version"))},std::make_shared<const Geometry>(geometry(field(row,"geojson"),d,10,"/geometries/geojson"))});}
    auto restored=restoreTimelineStorage(candidate,timelineEntityCatalog(d));d.timelineRecords=std::move(restored.records);d.geometries=std::move(restored.geometries);
}
Layer layer(const V& v,ProjectDocument& d,int schema,const std::string& path) {
    Layer l{str(field(v,"id")),str(field(v,"name")),boolean(field(v,"visible")),boolean(field(v,"locked")),number(field(v,"opacity"))};
    unknown(d,schema,v,path,{"id","name","visible","locked","opacity"}); return l;
}
V layerValue(const Layer& l) { return object({{"id",V::str(l.id)},{"name",V::str(l.name)},{"visible",V::boolean(l.visible)},{"locked",V::boolean(l.locked)},{"opacity",V::num(l.opacity)}}); }
void readExtensions(const V& values,ProjectDocument& d) {
    std::size_t index=0;
    for (const auto& v:array(values)) {
        PreservedExtension e;
        e.id=str(field(v,"extensionId")); e.sourceFormat=str(field(v,"sourceFormat"));
        const auto schema=integer(field(v,"sourceSchema")); require(schema<=INT_MAX,"INVALID_JSON: sourceSchema overflow"); e.sourceSchema=int(schema);
        e.jsonPointer=str(field(v,"jsonPointer")); e.payload=field(v,"payload").encode().toStdString();
        e.status=str(field(v,"status")); e.dependencyKnowledge=str(field(v,"dependencyKnowledge"));
        require(e.status=="unsupported"||e.status=="migrationArchive","INVALID_JSON: extension status");
        require(e.dependencyKnowledge=="known"||e.dependencyKnowledge=="unknown","INVALID_JSON: dependency knowledge");
        for (const auto& r:array(field(v,"dependencies"))) e.dependencies.push_back(ref(r,d,"/extensions/"+std::to_string(index)+"/dependencies/"+std::to_string(e.dependencies.size())));
        for (const auto& effect:array(field(v,"forbiddenEffects"))) e.forbiddenEffects.push_back(str(effect));
        V extras=v;
        for (auto k:{"extensionId","sourceFormat","sourceSchema","jsonPointer","payload","status","dependencyKnowledge","dependencies","forbiddenEffects"}) extras.object.erase(k);
        e.envelopeExtras=extras.encode().toStdString(); d.extensions.push_back(std::move(e)); ++index;
    }
}
V jsonObject(const std::string& bytes) {
    auto v=losslessjson::parse(QByteArray::fromStdString(bytes));
    require(v.kind==V::Object,"INVALID_CONTENT: expected JSON object"); return v;
}
QByteArray decimalValue(QByteArray token) {
    token=token.toLower();const auto split=token.indexOf('e');
    const auto mantissa=split<0?token:token.left(split);
    bool okay=true;qint64 exponent=split<0?0:token.mid(split+1).toLongLong(&okay);
    require(okay&&exponent>=-1000000000&&exponent<=1000000000,"UNREPRESENTABLE_WEB_NUMBER");
    auto digits=mantissa;const bool negative=digits.startsWith('-');if(negative)digits.remove(0,1);
    const auto point=digits.indexOf('.');if(point>=0){exponent-=digits.size()-point-1;digits.remove(point,1);}
    while(digits.startsWith('0'))digits.remove(0,1);
    if(digits.isEmpty())return negative?QByteArray("-0"):QByteArray("0");
    while(digits.endsWith('0')){digits.chop(1);++exponent;}
    return (negative?QByteArray("-"):QByteArray())+digits+"e"+QByteArray::number(exponent);
}
void requireWebNumbers(const V& value) {
    if(value.kind==V::Number) {
        bool okay=false;const double number=value.raw.toDouble(&okay);
        require(okay&&std::isfinite(number),"UNREPRESENTABLE_WEB_NUMBER");
        require(decimalValue(value.raw)==decimalValue(V::num(number).raw),"UNREPRESENTABLE_WEB_NUMBER");
    } else if(value.kind==V::Array)for(const auto& child:value.array)requireWebNumbers(child);
    else if(value.kind==V::Object)for(const auto& [key,child]:value.object)requireWebNumbers(child);
}
V nativeIdentityMetadata(const std::string& bytes) {
    auto value=jsonObject(bytes);
    for(const auto* key:{"sovereignId","sovereign","sovereignty","politicalRelations"})require(!value.object.count(key),"UNSUPPORTED_POLITICAL_RELATION");
    for(const auto* key:{"capital","flagDataUrl","nameSource","libraryOrigin"})require(!value.object.count(key),"DUPLICATE_METADATA_OWNER");
    return value;
}
V sourceValue(const SourceProvenance& s) {
    return object({{"kind",V::str(s.kind)},{"dataset",V::str(s.dataset)},{"version",V::str(s.version)},
        {"sourceId",V::str(s.sourceId)},{"sourceFormat",V::str(s.sourceFormat)},{"sourceType",V::str(s.sourceType)},
        {"importedAt",V::str(s.importedAt)},{"details",jsonObject(s.details)}});
}
SourceProvenance readSource(const V& v,ProjectDocument& d,const std::string& path) {
    SourceProvenance s;
    s.kind=str(field(v,"kind")); s.dataset=str(field(v,"dataset")); s.version=str(field(v,"version"));
    s.sourceId=str(field(v,"sourceId")); s.sourceFormat=str(field(v,"sourceFormat"));
    s.sourceType=str(field(v,"sourceType")); s.importedAt=str(field(v,"importedAt"));
    s.details=field(v,"details").encode().toStdString(); jsonObject(s.details);
    unknown(d,6,v,path,{"kind","dataset","version","sourceId","sourceFormat","sourceType","importedAt","details"});
    return s;
}
V contentValue(const ProjectDocument& d) {
    V labels=V::arr(),hydro=V::arr(),layers=V::arr(),entries=V::arr(),generic=V::arr(),countries=V::arr(),symbols=V::arr();
    auto common=[](const auto& v){return object({{"id",V::str(v.id)},{"name",V::str(v.name)},
        {"notes",V::str(v.notes)},{"geometryRef",geometryRefValue(v.geometry)},{"source",sourceValue(v.source)}});};
    for(const auto& v:d.labels) {
        auto row=common(v); row.object["kind"]=V::str(v.kind);
        row.object["territory"]=v.territory?refValue(*v.territory):V{}; labels.array.push_back(std::move(row));
    }
    for(const auto& v:d.hydro) {
        auto row=common(v); row.object["kind"]=V::str(v.kind); row.object["color"]=colorValue(v.color);
        row.object["locked"]=V::boolean(v.locked); row.object["sourceFeatureId"]=v.sourceFeatureId?V::str(*v.sourceFeatureId):V{};
        hydro.array.push_back(std::move(row));
    }
    for(const auto& v:d.genericFeatures) {
        auto row=common(v); row.object["color"]=colorValue(v.color); row.object["locked"]=V::boolean(v.locked);
        row.object["fallbackOnly"]=V::boolean(v.fallbackOnly); generic.array.push_back(std::move(row));
    }
    for(const auto& v:d.distributionLayers) {
        V groups=V::arr(); for(const auto& group:v.groups) groups.array.push_back(V::str(group));
        V scale=object({{"mode",V::str(v.valueScale.manual?"manual":"auto")}});
        if(v.valueScale.manual){scale.object["min"]=V::num(v.valueScale.min);scale.object["max"]=V::num(v.valueScale.max);}
        layers.array.push_back(object({{"id",V::str(v.id)},{"name",V::str(v.name)},{"unit",V::str(v.unit)},{"valueScale",scale},
            {"color",colorValue(v.color)},{"locked",V::boolean(v.locked)},{"parentId",v.parentId?V::str(*v.parentId):V{}},
            {"groups",groups},{"validity",validityValue(v.validity)},{"metadata",jsonObject(v.metadata)}}));
    }
    for(const auto& v:d.distributionEntries) entries.array.push_back(object({{"id",V::str(v.id)},
        {"layerId",V::str(v.layerId)},{"territory",v.territory?refValue(*v.territory):V{}},
        {"geometryRef",v.geometry?geometryRefValue(*v.geometry):V{}},{"value",V::num(v.value)},
        {"certainty",V::str(v.certainty)},{"validity",validityValue(v.validity)},{"metadata",jsonObject(v.metadata)}}));
    for(const auto& [ref,v]:d.countryDetails) countries.array.push_back(object({{"ref",refValue(ref)},{"capital",V::str(v.capital)}}));
    for(const auto& [ref,v]:d.symbols) symbols.array.push_back(object({{"ref",refValue(ref)},
        {"policy",V::str(v.policy==FlagPolicy::Default?"default":v.policy==FlagPolicy::None?"none":"embedded")},
        {"embeddedDataUrl",V::str(v.embeddedDataUrl)},{"defaultCountryId",V::str(v.defaultCountryId)},
        {"defaultFlagDataUrl",v.defaultFlagDataUrl?V::str(*v.defaultFlagDataUrl):V{}}}));
    V hidden=V::arr(); for(const auto& id:d.physicalData.hiddenHydroIds) hidden.array.push_back(V::str(id));
    return object({{"labels",labels},{"hydro",hydro},{"distributionLayers",layers},{"distributionEntries",entries},
        {"genericFeatures",generic},{"countryDetails",countries},{"symbols",symbols},
        {"physicalData",object({{"dataset",V::str(d.physicalData.dataset)},{"version",V::str(d.physicalData.version)},
            {"source",V::str(d.physicalData.source)},{"hiddenHydroIds",hidden}})}});
}
void readContent(const V& content,ProjectDocument& d) {
    auto common=[&](const V& row,auto& v,const std::string& path) {
        v.id=str(field(row,"id")); v.name=str(field(row,"name")); v.notes=str(field(row,"notes"));
        v.geometry=geometryRef(field(row,"geometryRef"),d,path+"/geometryRef");
        v.source=readSource(field(row,"source"),d,path+"/source");
    };
    for(const auto& row:array(field(content,"labels"))) {
        auto path="/content/labels/"+std::to_string(d.labels.size()); PlaceLabel v; common(row,v,path);
        v.kind=str(field(row,"kind")); if(field(row,"territory").kind!=V::Null) v.territory=ref(field(row,"territory"),d,path+"/territory");
        unknown(d,6,row,path,{"id","name","notes","geometryRef","source","kind","territory"}); d.labels.push_back(std::move(v));
    }
    for(const auto& row:array(field(content,"hydro"))) {
        auto path="/content/hydro/"+std::to_string(d.hydro.size()); HydroFeature v; common(row,v,path);
        v.kind=str(field(row,"kind")); v.color=color(field(row,"color")); v.locked=boolean(field(row,"locked"));
        if(field(row,"sourceFeatureId").kind!=V::Null) v.sourceFeatureId=str(field(row,"sourceFeatureId"));
        unknown(d,6,row,path,{"id","name","notes","geometryRef","source","kind","color","locked","sourceFeatureId"}); d.hydro.push_back(std::move(v));
    }
    for(const auto& row:array(field(content,"genericFeatures"))) {
        auto path="/content/genericFeatures/"+std::to_string(d.genericFeatures.size()); GenericFeature v; common(row,v,path);
        v.color=color(field(row,"color")); v.locked=boolean(field(row,"locked")); v.fallbackOnly=boolean(field(row,"fallbackOnly"));
        unknown(d,6,row,path,{"id","name","notes","geometryRef","source","color","locked","fallbackOnly"}); d.genericFeatures.push_back(std::move(v));
    }
    for(const auto& row:array(field(content,"distributionLayers"))) {
        auto path="/content/distributionLayers/"+std::to_string(d.distributionLayers.size()); DistributionLayer v;
        v.id=str(field(row,"id")); v.name=str(field(row,"name"));
        v.unit=str(field(row,"unit"));const auto& scale=field(row,"valueScale");const auto mode=str(field(scale,"mode"));
        require(mode=="auto"||mode=="manual","INVALID_DISTRIBUTION: value scale");
        if(mode=="manual")v.valueScale={true,number(field(scale,"min")),number(field(scale,"max"))};
        unknown(d,10,scale,path+"/valueScale",mode=="manual"?std::initializer_list<const char*>{"mode","min","max"}:std::initializer_list<const char*>{"mode"});
        v.color=color(field(row,"color")); v.locked=boolean(field(row,"locked"));
        if(field(row,"parentId").kind!=V::Null) v.parentId=str(field(row,"parentId"));
        for(const auto& group:array(field(row,"groups"))) v.groups.push_back(str(group));
        v.validity=validity(field(row,"validity"),d,path+"/validity");
        v.metadata=field(row,"metadata").encode().toStdString(); jsonObject(v.metadata);
        unknown(d,10,row,path,{"id","name","unit","valueScale","color","locked","parentId","groups","validity","metadata"}); d.distributionLayers.push_back(std::move(v));
    }
    for(const auto& row:array(field(content,"distributionEntries"))) {
        auto path="/content/distributionEntries/"+std::to_string(d.distributionEntries.size()); DistributionEntry v;
        v.id=str(field(row,"id")); v.layerId=str(field(row,"layerId")); v.value=number(field(row,"value"));
        v.certainty=str(field(row,"certainty")); v.metadata=field(row,"metadata").encode().toStdString(); jsonObject(v.metadata);
        v.validity=validity(field(row,"validity"),d,path+"/validity");
        if(field(row,"territory").kind!=V::Null) v.territory=ref(field(row,"territory"),d,path+"/territory");
        if(field(row,"geometryRef").kind!=V::Null) v.geometry=geometryRef(field(row,"geometryRef"),d,path+"/geometryRef");
        unknown(d,10,row,path,{"id","layerId","value","certainty","metadata","validity","territory","geometryRef"}); d.distributionEntries.push_back(std::move(v));
    }
    for(const auto& row:array(field(content,"countryDetails"))) {
        auto path="/content/countryDetails/"+std::to_string(d.countryDetails.size());
        require(d.countryDetails.emplace(ref(field(row,"ref"),d,path+"/ref"),CountryDetails{str(field(row,"capital"))}).second,"DUPLICATE_ID: country details");
        unknown(d,6,row,path,{"ref","capital"});
    }
    for(const auto& row:array(field(content,"symbols"))) {
        auto path="/content/symbols/"+std::to_string(d.symbols.size()); TerritorialSymbolStyle v;
        const auto policy=str(field(row,"policy")); require(policy=="default" || policy=="none" || policy=="embedded","INVALID_FLAG: policy");
        v.policy=policy=="default"?FlagPolicy::Default:policy=="none"?FlagPolicy::None:FlagPolicy::Embedded;
        v.embeddedDataUrl=str(field(row,"embeddedDataUrl"));
        if(row.object.count("defaultCountryId"))v.defaultCountryId=str(field(row,"defaultCountryId"));
        if(row.object.count("defaultFlagDataUrl")&&field(row,"defaultFlagDataUrl").kind!=V::Null)
            v.defaultFlagDataUrl=str(field(row,"defaultFlagDataUrl"));
        if(v.policy==FlagPolicy::Embedded)validateEmbeddedFlag(v.embeddedDataUrl);
        require(d.symbols.emplace(ref(field(row,"ref"),d,path+"/ref"),v).second,"DUPLICATE_ID: symbol");
        unknown(d,6,row,path,{"ref","policy","embeddedDataUrl","defaultCountryId","defaultFlagDataUrl"});
    }
    const auto& physical=field(content,"physicalData");
    d.physicalData.dataset=str(field(physical,"dataset")); d.physicalData.version=str(field(physical,"version"));
    d.physicalData.source=str(field(physical,"source"));
    for(const auto& id:array(field(physical,"hiddenHydroIds"))) d.physicalData.hiddenHydroIds.push_back(str(id));
    unknown(d,6,physical,"/content/physicalData",{"dataset","version","source","hiddenHydroIds"});
    unknown(d,6,content,"/content",{"labels","hydro","genericFeatures","distributionLayers","distributionEntries","countryDetails","symbols","physicalData"});
}
void readPresentation(const V& value,ProjectDocument& d) {const unsigned schema=10;
        const auto& p=value;
        for (const auto& v:array(field(p,"userLayers"))) d.presentation.userLayers.push_back(layer(v,d,3,"/presentation/userLayers/"+std::to_string(d.presentation.userLayers.size())));
        std::size_t membershipIndex=0;
        for (const auto& v:array(field(p,"membership"))) {
            auto path="/presentation/membership/"+std::to_string(membershipIndex++);
            auto r=ref(field(v,"ref"),d,path+"/ref"); auto l=str(field(v,"layerId"));
            if (r.domain=="territorial") {
                require(d.presentation.membership.emplace(r,l).second,"DUPLICATE_ID: membership");
                unknown(d,10,v,path,{"ref","layerId"});
            } else {require(d.presentation.membership.emplace(r,l).second,"DUPLICATE_ID: membership");unknown(d,10,v,path,{"ref","layerId"});}
        }
        const auto& styles=field(p,"objectStyles"); require(styles.kind==V::Object,"INVALID_JSON: objectStyles");
        for (const auto& [domain,items]:styles.object) {
            auto path=pointer("/presentation/objectStyles",domain);
            require(std::set<std::string>{"territorial","label","hydro","generic","distributionEntry"}.count(domain),"INVALID_STYLE_DOMAIN");
            require(items.kind==V::Object,"INVALID_JSON: domain styles");
            for (const auto& [id,v]:items.object) {
                const bool automatic=schema>=4 && field(v,"color").kind==V::Null;
                d.presentation.objectStyles.emplace(ObjectRef{domain,id},ObjectStyle{automatic?0:color(field(v,"color")),number(field(v,"opacity")),!automatic});
                unknown(d,10,v,pointer(path,id),{"color","opacity"});
            }
        }
        if(schema>=5) {
            const auto& w=field(p,"webPresentation");auto& out=d.presentation.webPresentation;
            require(field(w,"visibility").kind==V::Object && field(w,"hiddenItems").kind==V::Object,"INVALID_JSON: presentation visibility");
            for(const auto& [key,value]:field(w,"visibility").object) {
                if(presentationGroup(key)||presentationSymbol(key))out.visibility[key]=boolean(value);
                else throw std::invalid_argument("UNSUPPORTED_VISIBILITY: "+key);
            }
            for(const auto& [group,values]:field(w,"hiddenItems").object) {
                if(presentationGroup(group))for(const auto& id:array(values))out.hiddenItems[group].insert(str(id));
                else throw std::invalid_argument("UNSUPPORTED_VISIBILITY: "+group);
            }
            auto readStyles=[&](const char* name,auto& target){
                const auto& values=field(w,name);require(values.kind==V::Object,"INVALID_JSON: presentation styles");
                for(const auto& [key,value]:values.object){
                    const bool supported=std::string(name)=="styles"?(presentationGroup(key)):key.rfind("territorial:entity:",0)==0;
                    require(supported,"UNSUPPORTED_PRESENTATION_KEY");
                    PresentationStyle s;require(value.kind==V::Object,"INVALID_JSON: presentation style");
                    if(value.object.count("opacity"))s.opacity=number(field(value,"opacity"));
                    if(value.object.count("boundaryVisible"))s.boundaryVisible=boolean(field(value,"boundaryVisible"));
                    if(value.object.count("colorVisible"))s.colorVisible=boolean(field(value,"colorVisible"));
                    if(value.object.count("labelsVisible"))s.labelsVisible=boolean(field(value,"labelsVisible"));
                    if(value.object.count("boundaryWidth"))s.boundaryWidth=number(field(value,"boundaryWidth"));
                    if(value.object.count("blendMode"))s.blendMode=str(field(value,"blendMode"));
                    target[key]=s;unknown(d,5,value,pointer(std::string("/presentation/webPresentation/")+name,key),{"opacity","colorVisible","boundaryVisible","labelsVisible","boundaryWidth","blendMode"});
                }
            };
            readStyles("styles",out.styles);readStyles("objectStyles",out.objectStyles);
            std::set<std::string> overlays;
            for(const auto& key:array(field(w,"overlayOrder"))){const auto text=str(key);require((text=="genericFeatures"||text=="distributions"||text=="subunits"||text=="regions")&&overlays.insert(text).second,"UNSUPPORTED_OVERLAY_ORDER");out.overlayOrder.push_back(text);}
            for(const auto& key:array(field(w,"objectOrder"))){const auto text=str(key);require(text.rfind("territorial:entity:",0)==0,"INVALID_PRESENTATION_ORDER");out.objectOrder.push_back(text);}
            if(schema>=6 && w.object.count("labelSettings")) for(const auto& value:array(field(w,"labelSettings"))) {
                auto path="/presentation/webPresentation/labelSettings/"+std::to_string(out.labelSettings.size());
                const auto owner=ref(field(value,"ref"),d,path+"/ref");LabelSettings s;
                if(value.object.count("priority"))s.priority=number(field(value,"priority"));
                if(value.object.count("minZoom"))s.minZoom=number(field(value,"minZoom"));
                if(value.object.count("maxZoom"))s.maxZoom=number(field(value,"maxZoom"));
                if(value.object.count("manualPosition")){const auto& a=array(field(value,"manualPosition"));require(a.size()==2,"INVALID_LABEL_SETTINGS");s.manualPosition=Point{number(a[0]),number(a[1])};}
                s.pinned=value.object.count("pinned")&&boolean(field(value,"pinned"));
                if(value.object.count("collisionGroup"))s.collisionGroup=str(field(value,"collisionGroup"));
                require(out.labelSettings.emplace(owner,std::move(s)).second,"DUPLICATE_ID: label settings");
                unknown(d,6,value,path,{"ref","priority","minZoom","maxZoom","manualPosition","pinned","collisionGroup"});
            }
            if(schema>=6 && w.object.count("distributionSettings")) {
                const auto& value=field(w,"distributionSettings");const auto mode=str(field(value,"renderMode"));
                require(mode=="overlap"||mode=="single","INVALID_DISTRIBUTION_SETTINGS");
                out.distributionSettings.renderMode=mode=="single"?DistributionRenderMode::Single:DistributionRenderMode::Overlap;
                if(schema>=8)out.distributionSettings.activeLayerId=str(field(value,"activeLayerId"));
                out.distributionSettings.boundaryVisible=boolean(field(value,"boundaryVisible"));
                unknown(d,10,value,"/presentation/webPresentation/distributionSettings",{"renderMode","activeLayerId","boundaryVisible"});
            }
            unknown(d,5,w,"/presentation/webPresentation",{"visibility","hiddenItems","styles","objectStyles","objectOrder","overlayOrder","labelSettings","distributionSettings"});

        }
        unknown(d,10,p,"/presentation",{"userLayers","membership","objectStyles","webPresentation"});

}

V extensionValue(const PreservedExtension& e) {
    V v=losslessjson::parse(QByteArray::fromStdString(e.envelopeExtras)); require(v.kind==V::Object,"INVALID_JSON: extension envelope extras");
    v.object["extensionId"]=V::str(e.id); v.object["sourceFormat"]=V::str(e.sourceFormat); v.object["sourceSchema"]=V::num(e.sourceSchema);
    v.object["jsonPointer"]=V::str(e.jsonPointer); v.object["payload"]=losslessjson::parse(QByteArray::fromStdString(e.payload));
    v.object["status"]=V::str(e.status); v.object["dependencyKnowledge"]=V::str(e.dependencyKnowledge);
    V deps=V::arr(),effects=V::arr();
    for (const auto& r:e.dependencies) deps.array.push_back(refValue(r));
    for (const auto& s:e.forbiddenEffects) effects.array.push_back(V::str(s));
    v.object["dependencies"]=deps; v.object["forbiddenEffects"]=effects; return v;
}
}

pandoeditor::ProjectDocument decode(const QByteArray& data) {
    const auto root=losslessjson::parse(data);
    require(str(field(root,"format"))=="pandoeditor-project","UNSUPPORTED_FORMAT: expected Qt project");
    const auto schema=integer(field(root,"version"));require(schema==ProjectVersion,"UNSUPPORTED_VERSION: expected Qt v10");
    ProjectDocument d;d.documentId=str(field(root,"documentId"));readExtensions(field(root,"extensions"),d);
    d.exchangeMetadata=field(root,"exchangeMetadata").encode().toStdString();validateExchangeMetadata(d);
    for(const auto& v:array(field(root,"units"))) {
        unknown(d,10,v,"/units",{"id","kind","name","notes","locked","baseName","nameExplicit","libraryOrigin","metadata","sourceFolderId","sourceEntityId","sourceGeometryVersion"});
        TerritorialUnit u;u.id=str(field(v,"id"));u.name=str(field(v,"name"));u.notes=str(field(v,"notes"));const auto kind=str(field(v,"kind"));require(kind=="general"||kind=="regional","INVALID_ENTITY_KIND");u.kind=kind=="general"?UnitKind::General:UnitKind::Regional;
        u.locked=boolean(field(v,"locked"));u.baseName=str(field(v,"baseName"));u.nameExplicit=boolean(field(v,"nameExplicit"));
        if(field(v,"libraryOrigin").kind!=V::Null)u.libraryOrigin=libraryOrigin(field(v,"libraryOrigin"),d,"/units/libraryOrigin");
        u.metadata=field(v,"metadata").encode().toStdString();nativeIdentityMetadata(u.metadata);u.sourceFolderId=str(field(v,"sourceFolderId"));u.sourceEntityId=str(field(v,"sourceEntityId"));u.sourceGeometryVersion=str(field(v,"sourceGeometryVersion"));d.units.push_back(std::move(u));
    }
    restoreArchive(root,d);
    readPresentation(field(root,"presentation"),d);
        readContent(field(root,"content"),d);
        unknown(d,10,root,"",{"format","version","documentId","units","timelineRecords","geometries","presentation","extensions","content","exchangeMetadata"});

    validateDocument(d);return d;
}

QByteArray encode(const pandoeditor::Project& project) {
    return encode(project.snapshot());
}
QByteArray encode(const pandoeditor::ProjectSnapshot& snapshot) {
    const auto& d=snapshot.document(); validateDocument(d);validateExchangeMetadata(d);
    V units=V::arr(),geometries=V::arr(),layers=V::arr(),membership=V::arr(),styles=V::obj(),extensions=V::arr();
    for (const auto& u:d.units) {
        auto value=object({{"id",V::str(u.id)},{"kind",V::str(u.kind==UnitKind::General?"general":"regional")},{"name",V::str(u.name)},{"baseName",V::str(u.baseName)},{"nameExplicit",V::boolean(u.kind==UnitKind::General?u.nameExplicit&&!u.name.empty():u.nameExplicit)},{"notes",V::str(u.kind==UnitKind::General?trimWebText(u.notes):u.notes)},{"locked",V::boolean(u.locked)},{"libraryOrigin",V{}},{"metadata",nativeIdentityMetadata(u.metadata)},{"sourceFolderId",V::str(u.sourceFolderId)},{"sourceEntityId",V::str(u.sourceEntityId)},{"sourceGeometryVersion",V::str(u.sourceGeometryVersion)}});
        if(u.libraryOrigin)value.object["libraryOrigin"]=libraryOriginValue(*u.libraryOrigin);
        units.array.push_back(std::move(value));
    }
    // Opaque domains may reference otherwise unreferenced stored geometries.
    for (const auto& [r,g]:d.geometries.versions()) geometries.array.push_back(object({{"id",V::str(r.id)},{"version",V::num(r.version)},{"geojson",geometryValue(*g)}}));
    for (const auto& l:d.presentation.userLayers) layers.array.push_back(layerValue(l));
    for (const auto& [r,l]:d.presentation.membership) membership.array.push_back(object({{"ref",refValue(r)},{"layerId",V::str(l)}}));
    for (const auto& [r,s]:d.presentation.objectStyles) {
        if (!styles.object.count(r.domain)) styles.object[r.domain]=V::obj();
        styles.object[r.domain].object[r.id]=object({{"color",s.explicitColor?colorValue(s.color):V{}},{"opacity",V::num(s.opacity)}});
    }
    for (const auto& e:d.extensions) extensions.array.push_back(extensionValue(e));
    const auto& web=d.presentation.webPresentation;
    V visibility=V::obj(),hidden=V::obj(),groups=V::obj(),overrides=V::obj(),order=V::arr(),overlayOrder=V::arr(),labelSettings=V::arr();
    for(const auto& [key,value]:web.visibility)visibility.object[key]=V::boolean(value);
    for(const auto& [key,ids]:web.hiddenItems){V values=V::arr();for(const auto& id:ids)values.array.push_back(V::str(id));hidden.object[key]=values;}
    auto writeStyles=[](const auto& source,V& target){for(const auto& [key,s]:source){V v=V::obj();if(s.opacity)v.object["opacity"]=V::num(*s.opacity);if(s.colorVisible)v.object["colorVisible"]=V::boolean(*s.colorVisible);if(s.boundaryVisible)v.object["boundaryVisible"]=V::boolean(*s.boundaryVisible);if(s.labelsVisible)v.object["labelsVisible"]=V::boolean(*s.labelsVisible);if(s.boundaryWidth)v.object["boundaryWidth"]=V::num(*s.boundaryWidth);if(s.blendMode)v.object["blendMode"]=V::str(*s.blendMode);target.object[key]=v;}};
    writeStyles(web.styles,groups);writeStyles(web.objectStyles,overrides);for(const auto& key:web.objectOrder)order.array.push_back(V::str(key));for(const auto& key:web.overlayOrder)overlayOrder.array.push_back(V::str(key));
    for(const auto& [owner,s]:web.labelSettings){V value=object({{"ref",refValue(owner)},{"pinned",V::boolean(s.pinned)},{"collisionGroup",V::str(s.collisionGroup)}});if(s.priority)value.object["priority"]=V::num(*s.priority);if(s.minZoom)value.object["minZoom"]=V::num(*s.minZoom);if(s.maxZoom)value.object["maxZoom"]=V::num(*s.maxZoom);if(s.manualPosition){V point=V::arr();point.array={V::num(s.manualPosition->x),V::num(s.manualPosition->y)};value.object["manualPosition"]=point;}labelSettings.array.push_back(std::move(value));}
    auto distributionSettings=object({{"renderMode",V::str(web.distributionSettings.renderMode==DistributionRenderMode::Single?"single":"overlap")},{"activeLayerId",V::str(web.distributionSettings.activeLayerId)},{"boundaryVisible",V::boolean(web.distributionSettings.boundaryVisible)}});
    auto webValue=object({{"visibility",visibility},{"hiddenItems",hidden},{"styles",groups},{"objectStyles",overrides},{"objectOrder",order},{"overlayOrder",overlayOrder},{"labelSettings",labelSettings},{"distributionSettings",distributionSettings}});
    auto root=object({{"format",V::str("pandoeditor-project")},{"version",V::num(ProjectVersion)},{"documentId",V::str(d.documentId)},{"units",units},{"timelineRecords",timelineRecordsValue(d.timelineRecords)},{"geometries",geometries},{"presentation",object({{"userLayers",layers},{"membership",membership},{"objectStyles",styles},{"webPresentation",webValue}})},{"extensions",extensions}});
    root.object["exchangeMetadata"]=jsonObject(d.exchangeMetadata);
    root.object["content"]=contentValue(d);
    auto bytes=root.encode()+"\n";
    require(bytes.size()<=256ll*1024*1024,"LIMIT_EXCEEDED: encoded JSON exceeds 256 MiB");
    return bytes;
}
namespace {
std::string optionalText(const V& v,const std::string& key,const std::string& fallback="") {
    const auto i=v.object.find(key);return i==v.object.end()||i->second.kind==V::Null?fallback:str(i->second);
}
V strings(std::initializer_list<const char*> values){V result=V::arr();for(const auto* value:values)result.array.push_back(V::str(value));return result;}
V currentWebHeader() {
    return object({{"version",V::str("0.1.0-timeline")},{"savedAt",V::str("")},{"baseDataset",V::str("external-territorial-entities")},{"sourceInfo",V{}},{"physicalSourceInfo",V::obj()},{"physicalSettings",V::obj()},
        {"landObjectModel",object({{"schemaVersion",V::num(2)},{"coastlineAuthority",V::str("territorialEntities")},{"purpose",V::str("lossless-fallback")},{"directCreation",V::boolean(false)},{"sourceProvenanceSchemaVersion",V::num(1)},{"canonicalProperties",strings({"name","notes","color","locked","source"})}})},
        {"territorialModel",object({{"schemaVersion",V::num(TerritorialIdentityVersion)},{"coastlineAuthority",V::str("territorialEntities")},{"storage",V::str("territorialEntities")},{"kinds",strings({"general","regional"})},{"coverageModes",strings({"partition","explicit"})}})},
        {"distributionModel",object({{"schemaVersion",V::num(3)},{"sourceModes",strings({"territorial","geometry"})},{"valueKind",V::str("finite-number")}})}});
}
void validateHeader(const V& header,const ProjectDocument&) {
    const auto contracts=currentWebHeader();
    for(const auto* key:{"landObjectModel","territorialModel","distributionModel"})require(field(header,key).encode()==field(contracts,key).encode(),"UNSUPPORTED_MODEL_CONTRACT");
    for(const auto* key:{"version","savedAt","baseDataset"})(void)str(field(header,key));
    require(field(header,"sourceInfo").kind==V::Null||field(header,"sourceInfo").kind==V::Object,"INVALID_SOURCE_INFO");
    require(field(header,"physicalSourceInfo").kind==V::Object&&field(header,"physicalSettings").kind==V::Object,"INVALID_PHYSICAL_SETTINGS");
    ProjectDocument checks;unknown(checks,9,header,"/exchangeMetadata",{"version","savedAt","baseDataset","sourceInfo","physicalSourceInfo","physicalSettings","landObjectModel","territorialModel","distributionModel"});
}
void validateExchangeMetadata(const ProjectDocument& d) {
    auto header=currentWebHeader();for(const auto& [key,value]:jsonObject(d.exchangeMetadata).object)header.object[key]=value;validateHeader(header,d);
}
SourceProvenance webSource(const V& value,ProjectDocument& d) {
    unknown(d,10,value,"/source",{"schemaVersion","kind","dataset","version","sourceId","sourceFormat","sourceType","importedAt","details"});
    require(integer(field(value,"schemaVersion"))==1,"UNSUPPORTED_SOURCE_VERSION");
    SourceProvenance result;result.kind=str(field(value,"kind"));
    result.dataset=optionalText(value,"dataset");result.version=optionalText(value,"version");result.sourceId=optionalText(value,"sourceId");result.sourceFormat=optionalText(value,"sourceFormat");result.sourceType=optionalText(value,"sourceType");result.importedAt=optionalText(value,"importedAt");
    if(value.object.count("details")&&field(value,"details").kind!=V::Null)result.details=jsonObject(field(value,"details").encode().toStdString()).encode().toStdString();return result;
}
V webSourceValue(const SourceProvenance& value){auto result=sourceValue(value);result.object["schemaVersion"]=V::num(1);return result;}
GeometryRef inlineGeometry(ProjectDocument& d,const std::string& domain,const std::string& id,const V& value) {
    const auto shape=geometry(value,d,9,"/"+domain+"/geometry");
    const GeometryRef ref{"web-"+domain+":"+id,1};
    // Reopening a web exchange uses the same immutable archive version when an
    // inline non-territorial feature also names its canonical stored shape.
    if(const auto existing=d.geometries.get(ref)){require(geometryValue(*existing).encode()==geometryValue(shape).encode(),"GEOMETRY_ARCHIVE_CONFLICT");return ref;}
    for(const auto& [candidate,stored]:d.geometries.versions())if(geometryValue(*stored).encode()==geometryValue(shape).encode())return candidate;
    d.geometries.insert(ref,shape);return ref;
}
V labelSettingsValue(const LabelSettings& settings) {
    V result=object({{"pinned",V::boolean(settings.pinned)},{"collisionGroup",V::str(settings.collisionGroup)}});
    result.object["priority"]=settings.priority?V::num(*settings.priority):V{};result.object["minZoom"]=settings.minZoom?V::num(*settings.minZoom):V::num(0);result.object["maxZoom"]=settings.maxZoom?V::num(*settings.maxZoom):V{};
    result.object["manualPosition"]=settings.manualPosition?pointValue(*settings.manualPosition):V{};return result;
}
void readWebPresentation(const V& root,ProjectDocument& d) {
    const auto& layer=field(root,"layerPresentation");require(integer(field(layer,"schemaVersion"))==4,"UNSUPPORTED_PRESENTATION_VERSION");
    unknown(d,10,layer,"/layerPresentation",{"schemaVersion","styles","objectStyles","objectOrder","overlayOrder"});
    const auto overlays=layer.object.count("overlayOrder")?field(layer,"overlayOrder"):V::arr();
    V hidden=V::obj();const auto& items=field(root,"itemVisibility");require(items.kind==V::Object,"INVALID_ITEM_VISIBILITY");
    for(const auto& [group,values]:items.object){require(values.kind==V::Object,"INVALID_ITEM_VISIBILITY");V ids=V::arr();for(const auto& [id,visible]:values.object)if(!boolean(visible))ids.array.push_back(V::str(id));hidden.object[group]=ids;}
    V labels=V::arr();const auto& settings=field(root,"labelSettings");require(settings.kind==V::Object,"INVALID_LABEL_SETTINGS");
    for(const auto& [key,value]:settings.object){const auto colon=key.find(':');require(colon!=std::string::npos,"INVALID_LABEL_KEY");const auto domain=key.substr(0,colon);require(domain=="territorial"||domain=="label","UNSUPPORTED_LABEL_KEY");auto row=value;require(row.kind==V::Object,"INVALID_LABEL_SETTINGS");row.object["ref"]=refValue({domain,key.substr(colon+1)});
        for(const auto* fieldName:{"priority","minZoom","maxZoom","manualPosition"})if(row.object.count(fieldName)&&field(row,fieldName).kind==V::Null)row.object.erase(fieldName);
        if(!row.object.count("collisionGroup"))row.object["collisionGroup"]=V::str("map");labels.array.push_back(std::move(row));}
    const auto empty=V::arr();auto presentation=object({{"userLayers",empty},{"membership",empty},{"objectStyles",V::obj()},{"webPresentation",object({{"visibility",field(root,"layerVisibility")},{"hiddenItems",hidden},{"styles",field(layer,"styles")},{"objectStyles",field(layer,"objectStyles")},{"objectOrder",field(layer,"objectOrder")},{"overlayOrder",overlays},{"labelSettings",labels},{"distributionSettings",field(root,"distributionSettings")}})}});
    // Entity color is already owned by the native identity's presentation row.
    const auto styles=d.presentation.objectStyles;readPresentation(presentation,d);d.presentation.objectStyles=styles;
}
}
void validateFlagDataUrl(const std::string& value) { validateEmbeddedFlag(value); }
ProjectDocument decodeWeb(const QByteArray& bytes) {
    const auto root=losslessjson::parse(bytes);ProjectDocument d;
    require(integer(field(root,"schemaVersion"))==ProjectVersion,"UNSUPPORTED_VERSION: expected web v10");
    const auto format=str(field(root,"format"));require(format!="pandolab-autosave-delta","BASE_DATA_REQUIRED: web delta requires its matching baseline");require(format=="pandolab-project-state"||format=="pandolab-autosave-full","UNSUPPORTED_FORMAT");
    unknown(d,10,root,"",{"format","schemaVersion","version","savedAt","baseDataset","landObjectModel","territorialModel","distributionModel","sourceInfo","physicalSourceInfo","physicalSettings","territorialEntities","timelineRecords","geometries","labels","hydroEdits","genericFeatures","distributionLayers","distributionEntries","labelSettings","distributionSettings","layerVisibility","itemVisibility","layerPresentation"});
    auto header=V::obj();for(const auto& [key,value]:currentWebHeader().object)header.object[key]=field(root,key);validateHeader(header,d);d.exchangeMetadata=header.encode().toStdString();
    d.documentId="web-"+QCryptographicHash::hash(root.encode(),QCryptographicHash::Sha256).toHex().toStdString();
    for(const auto& row:array(field(root,"territorialEntities"))) {
        unknown(d,10,row,"/territorialEntities",{"id","type","properties","geometry"});require(str(field(row,"type"))=="Feature"&&field(row,"geometry").kind==V::Null,"INVALID_TERRITORIAL_FEATURE");
        const auto& p=field(row,"properties");unknown(d,10,p,"/territorialEntities/properties",{"schemaVersion","entityKind","name","notes","style","locked","metadata","sourceFolderId","sourceEntityId","sourceGeometryVersion"});require(integer(field(p,"schemaVersion"))==TerritorialIdentityVersion,"UNSUPPORTED_ENTITY_VERSION");
        TerritorialUnit u;u.id=str(field(row,"id"));const auto kind=str(field(p,"entityKind"));require(kind=="general"||kind=="regional","INVALID_ENTITY_KIND");u.kind=kind=="general"?UnitKind::General:UnitKind::Regional;u.name=str(field(p,"name"));u.notes=str(field(p,"notes"));u.locked=boolean(field(p,"locked"));
        u.sourceFolderId=str(field(p,"sourceFolderId"));u.sourceEntityId=str(field(p,"sourceEntityId"));u.sourceGeometryVersion=str(field(p,"sourceGeometryVersion"));
        auto metadata=jsonObject(field(p,"metadata").encode().toStdString());for(const auto* key:{"sovereignId","sovereign","sovereignty","politicalRelations"})require(!metadata.object.count(key),"UNSUPPORTED_POLITICAL_RELATION");const auto owner=territorialRef(u.id);
        if(metadata.object.count("capital")){d.countryDetails[owner]={str(field(metadata,"capital"))};metadata.object.erase("capital");}
        if(metadata.object.count("flagDataUrl")){const auto& flag=field(metadata,"flagDataUrl");if(flag.kind!=V::Null)validateEmbeddedFlag(str(flag));d.symbols[owner]={flag.kind==V::Null?FlagPolicy::None:FlagPolicy::Embedded,flag.kind==V::Null?"":str(flag)};metadata.object.erase("flagDataUrl");}
        if(metadata.object.count("nameSource")){const auto& state=field(metadata,"nameSource");unknown(d,10,state,"/metadata/nameSource",{"baseName","nameExplicit"});u.baseName=str(field(state,"baseName"));u.nameExplicit=boolean(field(state,"nameExplicit"));metadata.object.erase("nameSource");}
        if(metadata.object.count("libraryOrigin")){u.libraryOrigin=libraryOrigin(field(metadata,"libraryOrigin"),d,"/metadata/libraryOrigin");metadata.object.erase("libraryOrigin");}
        u.metadata=metadata.encode().toStdString();const auto& style=field(p,"style");unknown(d,10,style,"/entity/style",{"color"});ObjectStyle nativeStyle{0,1,false};if(style.object.count("color")){nativeStyle.color=color(field(style,"color"));nativeStyle.explicitColor=true;}require(d.presentation.objectStyles.emplace(owner,nativeStyle).second,"DUPLICATE_ENTITY_ID");d.units.push_back(std::move(u));
    }
    restoreArchive(root,d);
    for(const auto& row:array(field(root,"labels"))) {
        unknown(d,10,row,"/labels",{"id","name","kind","notes","coordinates","territorialUnitId","source"});PlaceLabel label;label.id=str(field(row,"id"));label.name=str(field(row,"name"));label.kind=str(field(row,"kind"));label.notes=optionalText(row,"notes");label.geometry=inlineGeometry(d,"label",label.id,object({{"type",V::str("Point")},{"coordinates",field(row,"coordinates")}}));const auto parent=optionalText(row,"territorialUnitId");if(!parent.empty())label.territory=territorialRef(parent);if(row.object.count("source"))label.source=webSource(field(row,"source"),d);d.labels.push_back(std::move(label));
    }
    for(const auto* domain:{"hydroEdits","genericFeatures"})for(const auto& row:array(field(root,domain))) {
        unknown(d,10,row,std::string("/")+domain,{"type","id","properties","geometry"});require(str(field(row,"type"))=="Feature","INVALID_FEATURE");const auto id=str(field(row,"id"));const auto& p=field(row,"properties");const auto geo=inlineGeometry(d,domain,id,field(row,"geometry"));
        if(std::string(domain)=="genericFeatures"){unknown(d,10,p,"/genericFeatures/properties",{"schemaVersion","name","notes","color","locked","source"});require(integer(field(p,"schemaVersion"))==2,"UNSUPPORTED_GENERIC_VERSION");GenericFeature v;v.id=id;v.name=str(field(p,"name"));v.notes=str(field(p,"notes"));v.geometry=geo;v.color=color(field(p,"color"));v.locked=boolean(field(p,"locked"));v.source=webSource(field(p,"source"),d);d.genericFeatures.push_back(std::move(v));}
        else {unknown(d,10,p,"/hydroEdits/properties",{"name","notes","category","locked","editorColor","source","pandolab_schema_version","pandolab_domain","pandolab_id","sourceFeatureId"});require(integer(field(p,"pandolab_schema_version"))==1,"UNSUPPORTED_HYDRO_VERSION");require(str(field(p,"pandolab_domain"))=="hydro"&&str(field(p,"pandolab_id"))==id,"INVALID_HYDRO_IDENTITY");HydroFeature v;v.id=id;v.name=optionalText(p,"name");v.notes=optionalText(p,"notes");v.kind=str(field(p,"category"));v.geometry=geo;v.locked=boolean(field(p,"locked"));if(p.object.count("editorColor"))v.color=color(field(p,"editorColor"));if(p.object.count("source"))v.source=webSource(field(p,"source"),d);const auto sourceId=optionalText(p,"sourceFeatureId");if(!sourceId.empty())v.sourceFeatureId=sourceId;d.hydro.push_back(std::move(v));}
    }
    for(const auto& row:array(field(root,"distributionLayers"))) {
        unknown(d,10,row,"/distributionLayers",{"id","schemaVersion","name","unit","valueScale","color","locked","parentId","groups","validFrom","validTo","metadata"});require(integer(field(row,"schemaVersion"))==3,"UNSUPPORTED_DISTRIBUTION_VERSION");DistributionLayer v;v.id=str(field(row,"id"));v.name=str(field(row,"name"));v.unit=str(field(row,"unit"));v.color=color(field(row,"color"));v.locked=boolean(field(row,"locked"));const auto parent=optionalText(row,"parentId");if(!parent.empty())v.parentId=parent;for(const auto& group:array(field(row,"groups")))v.groups.push_back(str(group));v.validity=recordInterval(row);v.metadata=jsonObject(field(row,"metadata").encode().toStdString()).encode().toStdString();const auto& scale=field(row,"valueScale");const auto mode=str(field(scale,"mode"));require(mode=="auto"||mode=="manual","INVALID_VALUE_SCALE");unknown(d,10,scale,"/valueScale",{"mode","min","max"});if(mode=="manual")v.valueScale={true,number(field(scale,"min")),number(field(scale,"max"))};d.distributionLayers.push_back(std::move(v));
    }
    for(const auto& row:array(field(root,"distributionEntries"))) {
        unknown(d,10,row,"/distributionEntries",{"id","schemaVersion","layerId","mode","territorialUnitId","geometry","value","certainty","validFrom","validTo","metadata"});require(integer(field(row,"schemaVersion"))==3,"UNSUPPORTED_DISTRIBUTION_VERSION");DistributionEntry v;v.id=str(field(row,"id"));v.layerId=str(field(row,"layerId"));v.value=number(field(row,"value"));v.certainty=str(field(row,"certainty"));v.validity=recordInterval(row);v.metadata=jsonObject(field(row,"metadata").encode().toStdString()).encode().toStdString();const auto mode=str(field(row,"mode"));require(mode=="territorial"||mode=="geometry","INVALID_DISTRIBUTION_MODE");if(mode=="territorial"){require(field(row,"geometry").kind==V::Null,"INVALID_DISTRIBUTION_GEOMETRY");v.territory=territorialRef(str(field(row,"territorialUnitId")));}else{require(optionalText(row,"territorialUnitId").empty(),"INVALID_DISTRIBUTION_TERRITORY");v.geometry=inlineGeometry(d,"distributionEntry",v.id,field(row,"geometry"));}d.distributionEntries.push_back(std::move(v));
    }
    readWebPresentation(root,d);validateWebObjectIds(d);validateDocument(d);return d;
}
QByteArray encodeWeb(const ProjectSnapshot& snapshot) {
    const auto& d=snapshot.document();validateDocument(d);
    for(const auto& [owner,symbol]:d.symbols)
        require(symbol.defaultCountryId.empty()&&!symbol.defaultFlagDataUrl.has_value(),
                "UNSUPPORTED_WEB_EXPORT: native captured flag defaults");
    require(d.extensions.empty(),"UNSUPPORTED_WEB_EXPORT: retained native extensions");require(d.physicalData.dataset.empty()&&d.physicalData.version.empty()&&d.physicalData.source.empty()&&d.physicalData.hiddenHydroIds.empty(),"UNSUPPORTED_WEB_EXPORT: native physical dataset settings");for(const auto& [owner,style]:d.presentation.objectStyles)require(owner.domain=="territorial","UNSUPPORTED_WEB_EXPORT: native non-territorial style override");require(d.presentation.userLayers.empty()&&d.presentation.membership.empty(),"UNSUPPORTED_WEB_EXPORT: native user layer membership");
    auto root=currentWebHeader();for(const auto& [key,value]:jsonObject(d.exchangeMetadata).object)root.object[key]=value;validateHeader(root,d);root.object["format"]=V::str("pandolab-project-state");root.object["schemaVersion"]=V::num(ProjectVersion);
    V units=V::arr(),archive=V::arr(),labels=V::arr(),hydro=V::arr(),generic=V::arr(),layers=V::arr(),entries=V::arr();
    for(const auto& u:d.units){const auto owner=territorialRef(u.id);auto metadata=nativeIdentityMetadata(u.metadata);if(const auto details=d.countryDetails.find(owner);details!=d.countryDetails.end())metadata.object["capital"]=V::str(details->second.capital);if(const auto symbol=d.symbols.find(owner);symbol!=d.symbols.end()&&symbol->second.policy!=FlagPolicy::Default)metadata.object["flagDataUrl"]=symbol->second.policy==FlagPolicy::None?V{}:V::str(symbol->second.embeddedDataUrl);
        if(!u.baseName.empty()||!u.nameExplicit)metadata.object["nameSource"]=object({{"baseName",V::str(u.baseName)},{"nameExplicit",V::boolean(u.nameExplicit)}});if(u.libraryOrigin)metadata.object["libraryOrigin"]=libraryOriginValue(*u.libraryOrigin);
        const auto& style=d.presentation.objectStyles.at(owner);require(style.opacity==1,"UNSUPPORTED_WEB_EXPORT: native entity opacity");V color=V::obj();if(style.explicitColor)color.object["color"]=colorValue(style.color);
        units.array.push_back(object({{"type",V::str("Feature")},{"id",V::str(u.id)},{"geometry",V{}},{"properties",object({{"schemaVersion",V::num(TerritorialIdentityVersion)},{"entityKind",V::str(u.kind==UnitKind::General?"general":"regional")},{"name",V::str(u.name)},{"notes",V::str(u.notes)},{"locked",V::boolean(u.locked)},{"style",color},{"metadata",metadata},{"sourceFolderId",V::str(u.sourceFolderId)},{"sourceEntityId",V::str(u.sourceEntityId)},{"sourceGeometryVersion",V::str(u.sourceGeometryVersion)}})}}));}
    for(const auto& [ref,shape]:d.geometries.versions())archive.array.push_back(object({{"id",V::str(ref.id)},{"version",V::num(ref.version)},{"geojson",geometryValue(*shape)}}));
    for(const auto& label:d.labels){const auto shape=d.geometries.get(label.geometry);require(shape&&shape->type=="Point","INVALID_LABEL_GEOMETRY");labels.array.push_back(object({{"id",V::str(label.id)},{"name",V::str(label.name)},{"kind",V::str(label.kind)},{"notes",V::str(label.notes)},{"coordinates",pointValue(shape->points.at(0))},{"territorialUnitId",V::str(label.territory?label.territory->id:"")},{"source",webSourceValue(label.source)}}));}
    auto feature=[&](const auto& value,V props){return object({{"type",V::str("Feature")},{"id",V::str(value.id)},{"geometry",geometryValue(*d.geometries.get(value.geometry))},{"properties",props}});};
    for(const auto& value:d.genericFeatures){require(value.fallbackOnly,"UNSUPPORTED_WEB_EXPORT: generic direct creation");generic.array.push_back(feature(value,object({{"schemaVersion",V::num(2)},{"name",V::str(value.name)},{"notes",V::str(value.notes)},{"color",colorValue(value.color)},{"locked",V::boolean(value.locked)},{"source",webSourceValue(value.source)}})));}
    for(const auto& value:d.hydro){auto props=object({{"name",V::str(value.name)},{"notes",V::str(value.notes)},{"category",V::str(value.kind)},{"locked",V::boolean(value.locked)},{"editorColor",colorValue(value.color)},{"source",webSourceValue(value.source)},{"pandolab_schema_version",V::num(1)},{"pandolab_domain",V::str("hydro")},{"pandolab_id",V::str(value.id)}});if(value.sourceFeatureId)props.object["sourceFeatureId"]=V::str(*value.sourceFeatureId);hydro.array.push_back(feature(value,props));}
    auto interval=[](const auto& value){return object({{"id",V::str(value.id)},{"schemaVersion",V::num(3)},{"validFrom",value.validity.from?V::str(*value.validity.from):V{}},{"validTo",value.validity.to?V::str(*value.validity.to):V{}},{"metadata",jsonObject(value.metadata)}});};
    for(const auto& value:d.distributionLayers){auto row=interval(value);row.object["name"]=V::str(value.name);row.object["unit"]=V::str(value.unit);row.object["color"]=colorValue(value.color);row.object["locked"]=V::boolean(value.locked);row.object["parentId"]=V::str(value.parentId.value_or(""));V groups=V::arr();for(const auto& group:value.groups)groups.array.push_back(V::str(group));row.object["groups"]=groups;row.object["valueScale"]=value.valueScale.manual?object({{"mode",V::str("manual")},{"min",V::num(value.valueScale.min)},{"max",V::num(value.valueScale.max)}}):object({{"mode",V::str("auto")}});layers.array.push_back(std::move(row));}
    for(const auto& value:d.distributionEntries){auto row=interval(value);row.object["layerId"]=V::str(value.layerId);row.object["mode"]=V::str(value.territory?"territorial":"geometry");row.object["territorialUnitId"]=V::str(value.territory?value.territory->id:"");row.object["geometry"]=value.geometry?geometryValue(*d.geometries.get(*value.geometry)):V{};row.object["value"]=V::num(value.value);row.object["certainty"]=V::str(value.certainty);entries.array.push_back(std::move(row));}
    root.object["territorialEntities"]=units;root.object["timelineRecords"]=timelineRecordsValue(d.timelineRecords);root.object["geometries"]=archive;root.object["labels"]=labels;root.object["hydroEdits"]=hydro;root.object["genericFeatures"]=generic;root.object["distributionLayers"]=layers;root.object["distributionEntries"]=entries;
    const auto native=losslessjson::parse(encode(snapshot));const auto& web=field(field(native,"presentation"),"webPresentation");root.object["layerVisibility"]=field(web,"visibility");V hidden=V::obj();for(const auto& [group,ids]:d.presentation.webPresentation.hiddenItems){V rows=V::obj();for(const auto& id:ids)rows.object[id]=V::boolean(false);hidden.object[group]=rows;}root.object["itemVisibility"]=hidden;
    root.object["layerPresentation"]=object({{"schemaVersion",V::num(4)},{"styles",field(web,"styles")},{"objectStyles",field(web,"objectStyles")},{"objectOrder",field(web,"objectOrder")},{"overlayOrder",field(web,"overlayOrder")}});root.object["distributionSettings"]=field(web,"distributionSettings");V settings=V::obj();for(const auto& [owner,value]:d.presentation.webPresentation.labelSettings)settings.object[owner.domain+":"+owner.id]=labelSettingsValue(value);root.object["labelSettings"]=settings;
    requireWebNumbers(root);
    const auto bytes=root.encode()+"\n";require(bytes.size()<=256ll*1024*1024,"LIMIT_EXCEEDED");(void)decodeWeb(bytes);return bytes;
}

}
