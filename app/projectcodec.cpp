#include "projectcodec.h"
#include <pandoeditor/objectproperties.h>
#include "losslessjson.h"
#include "presentationmigration.h"
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QImage>
#include <climits>
#include <limits>
#include <set>

namespace projectcodec {
namespace {
using namespace pandoeditor;
using V=losslessjson::Value;
using losslessjson::require;
V object(std::initializer_list<std::pair<const std::string,V>> entries) { V v=V::obj(); v.object=entries; return v; }
const V& field(const V& v,const std::string& key) {
    require(v.kind==V::Object,"INVALID_JSON: expected object");
    const auto it=v.object.find(key); require(it!=v.object.end(),"INVALID_JSON: missing required field"); return it->second;
}
std::string str(const V& v) { require(v.kind==V::String,"INVALID_JSON: expected string"); return v.string; }
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
void preserve(ProjectDocument& d,int schema,const std::string& path,const V& payload) {
    PreservedExtension e;
    const auto hash=QCryptographicHash::hash(QByteArray::number(schema)+QByteArray::fromStdString(path)+payload.encode(),QCryptographicHash::Sha256).toHex();
    e.id="archive-"+hash.toStdString(); e.sourceSchema=schema; e.jsonPointer=path; e.payload=payload.encode().toStdString();
    e.status="unsupported";
    e.forbiddenEffects={"add","delete","type","geometry","relation","id"};
    d.extensions.push_back(std::move(e));
}
void unknown(ProjectDocument& d,int schema,const V& v,const std::string& path,std::initializer_list<const char*> known) {
    require(v.kind==V::Object,"INVALID_JSON: expected object");
    std::set<std::string> names; for (auto k:known) names.insert(k);
    for (const auto& [key,value]:v.object) if (!names.count(key)) preserve(d,schema,pointer(path,key),value);
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
    unknown(d,d.nativeSourceVersion,v,path,{"domain","id"}); return r;
}
V refValue(const ObjectRef& r) { return object({{"domain",V::str(r.domain)},{"id",V::str(r.id)}}); }
GeometryRef geometryRef(const V& v,ProjectDocument& d,const std::string& path) {
    GeometryRef r{str(field(v,"id")),integer(field(v,"version"))};
    unknown(d,d.nativeSourceVersion,v,path,{"id","version"}); return r;
}
V geometryRefValue(const GeometryRef& r) { return object({{"id",V::str(r.id)},{"version",V::num(r.version)}}); }
std::optional<std::string> endpoint(const V& v,ProjectDocument& d,const std::string& path) {
    if (v.kind==V::Null) return {};
    auto text=str(field(v,"text")); auto temporal=parseTemporal(text);
    require(str(field(v,"precision"))==temporal.precision,"INVALID_DATE: precision does not match text");
    unknown(d,d.nativeSourceVersion,v,path,{"text","precision"}); return text;
}
Validity validity(const V& v,ProjectDocument& d,const std::string& path) {
    Validity r{endpoint(field(v,"from"),d,path+"/from"),endpoint(field(v,"to"),d,path+"/to")};
    unknown(d,d.nativeSourceVersion,v,path,{"from","to"}); temporalBounds(r); return r;
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
    unknown(d,d.nativeSourceVersion,v,path,
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
        layers.array.push_back(object({{"id",V::str(v.id)},{"name",V::str(v.name)},{"type",V::str(v.type)},
            {"color",colorValue(v.color)},{"locked",V::boolean(v.locked)},{"parentId",v.parentId?V::str(*v.parentId):V{}},
            {"groups",groups},{"validity",validityValue(v.validity)},{"metadata",jsonObject(v.metadata)}}));
    }
    for(const auto& v:d.distributionEntries) entries.array.push_back(object({{"id",V::str(v.id)},
        {"layerId",V::str(v.layerId)},{"territory",v.territory?refValue(*v.territory):V{}},
        {"geometryRef",v.geometry?geometryRefValue(*v.geometry):V{}},{"share",V::num(v.share)},
        {"certainty",V::str(v.certainty)},{"validity",validityValue(v.validity)},{"metadata",jsonObject(v.metadata)}}));
    for(const auto& [ref,v]:d.countryDetails) countries.array.push_back(object({{"ref",refValue(ref)},{"capital",V::str(v.capital)}}));
    for(const auto& [ref,v]:d.symbols) symbols.array.push_back(object({{"ref",refValue(ref)},
        {"policy",V::str(v.policy==FlagPolicy::Default?"default":v.policy==FlagPolicy::None?"none":"embedded")},
        {"embeddedDataUrl",V::str(v.embeddedDataUrl)}}));
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
        v.id=str(field(row,"id")); v.name=str(field(row,"name")); v.type=str(field(row,"type"));
        v.color=color(field(row,"color")); v.locked=boolean(field(row,"locked"));
        if(field(row,"parentId").kind!=V::Null) v.parentId=str(field(row,"parentId"));
        for(const auto& group:array(field(row,"groups"))) v.groups.push_back(str(group));
        v.validity=validity(field(row,"validity"),d,path+"/validity");
        v.metadata=field(row,"metadata").encode().toStdString(); jsonObject(v.metadata);
        unknown(d,6,row,path,{"id","name","type","color","locked","parentId","groups","validity","metadata"}); d.distributionLayers.push_back(std::move(v));
    }
    for(const auto& row:array(field(content,"distributionEntries"))) {
        auto path="/content/distributionEntries/"+std::to_string(d.distributionEntries.size()); DistributionEntry v;
        v.id=str(field(row,"id")); v.layerId=str(field(row,"layerId")); v.share=number(field(row,"share"));
        v.certainty=str(field(row,"certainty")); v.metadata=field(row,"metadata").encode().toStdString(); jsonObject(v.metadata);
        v.validity=validity(field(row,"validity"),d,path+"/validity");
        if(field(row,"territory").kind!=V::Null) v.territory=ref(field(row,"territory"),d,path+"/territory");
        if(field(row,"geometryRef").kind!=V::Null) v.geometry=geometryRef(field(row,"geometryRef"),d,path+"/geometryRef");
        unknown(d,6,row,path,{"id","layerId","share","certainty","metadata","validity","territory","geometryRef"}); d.distributionEntries.push_back(std::move(v));
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
        require(d.symbols.emplace(ref(field(row,"ref"),d,path+"/ref"),v).second,"DUPLICATE_ID: symbol");
        unknown(d,6,row,path,{"ref","policy","embeddedDataUrl"});
    }
    const auto& physical=field(content,"physicalData");
    d.physicalData.dataset=str(field(physical,"dataset")); d.physicalData.version=str(field(physical,"version"));
    d.physicalData.source=str(field(physical,"source"));
    for(const auto& id:array(field(physical,"hiddenHydroIds"))) d.physicalData.hiddenHydroIds.push_back(str(id));
    unknown(d,6,physical,"/content/physicalData",{"dataset","version","source","hiddenHydroIds"});
    unknown(d,6,content,"/content",{"labels","hydro","genericFeatures","distributionLayers","distributionEntries","countryDetails","symbols","physicalData"});
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

std::vector<std::string> promoteContent(ProjectDocument& document) {
    std::vector<std::string> diagnostics;
    const std::vector<std::vector<std::string>> groups={
        {"/labels"},{"/hydroEdits"},{"/genericFeatures"},{"/distributionLayers","/distributionEntries"}};
    auto textOr=[](const V& v,const char* key,const std::string& fallback=std::string{}) {
        const auto it=v.object.find(key); return it==v.object.end()||it->second.kind==V::Null?fallback:str(it->second);
    };
    auto readProvenance=[&](const V& properties) {
        SourceProvenance s; s.kind="legacy";
        auto found=properties.object.find("source"); if(found==properties.object.end()) return s;
        const auto& raw=found->second; require(raw.kind==V::Object,"INVALID_SOURCE: source object");
        s.kind=textOr(raw,"kind","legacy"); s.dataset=textOr(raw,"dataset"); s.version=textOr(raw,"version");
        s.sourceId=textOr(raw,"sourceId"); s.sourceFormat=textOr(raw,"sourceFormat"); s.sourceType=textOr(raw,"sourceType");
        s.importedAt=textOr(raw,"importedAt");
        // details retains its original shape. The full source envelope remains
        // in the migration archive; unknown envelope fields must not become
        // an extra nested details object on every reopen.
        if(raw.object.count("details"))s.details=field(raw,"details").encode().toStdString();
        jsonObject(s.details);return s;
    };
    for(const auto& paths:groups) {
        std::vector<std::string> ids;
        for(const auto& e:document.extensions) if(e.status=="unsupported" && e.sourceFormat.rfind("pandolab-",0)==0 &&
            std::find(paths.begin(),paths.end(),e.jsonPointer)!=paths.end()) ids.push_back(e.id);
        if(ids.empty()) continue;
        try {
            auto candidate=document;
            for(const auto& id:ids) {
                const auto extension=*std::find_if(document.extensions.begin(),document.extensions.end(),[&](const auto& e){return e.id==id;});
                const auto rows=losslessjson::parse(QByteArray::fromStdString(extension.payload));
                std::set<std::string> sourceIds;
                std::size_t rowIndex=0;
                for(const auto& row:array(rows)) {
                    require(row.kind==V::Object,"INVALID_CONTENT: row");
                    const auto path=extension.jsonPointer+"/"+std::to_string(rowIndex++);
                    const auto objectId=str(field(row,"id")); require(!objectId.empty(),"INVALID_CONTENT: empty ID");
                    require(sourceIds.insert(objectId).second,"DUPLICATE_ID: promoted source object");
                    auto hasId=[&](const auto& values){return std::any_of(values.begin(),values.end(),[&](const auto& v){return v.id==objectId;});};
                    auto geo=[&](const char* domain,const V& raw) {
                        GeometryRef ref{"web-content:"+std::string(domain)+":"+objectId,1};
                        require(!candidate.geometries.get(ref),"DUPLICATE_ID: promoted geometry");
                        candidate.geometries.insert(ref,geometry(raw,candidate,extension.sourceSchema,path+"/geometry")); return ref;
                    };
                    auto interval=[&](const V& raw) {
                        Validity result; const auto from=textOr(raw,"validFrom"),to=textOr(raw,"validTo");
                        if(!from.empty()) result.from=from; if(!to.empty()) result.to=to; temporalBounds(result); return result;
                    };
                    if(extension.jsonPointer=="/labels") {
                        if(hasId(candidate.labels)) continue;
                        PlaceLabel v; v.id=objectId; v.name=textOr(row,"name"); v.kind=textOr(row,"kind","custom"); v.notes=textOr(row,"notes");
                        v.geometry=geo("label",object({{"type",V::str("Point")},{"coordinates",field(row,"coordinates")}}));
                        v.source=readProvenance(row); candidate.labels.push_back(std::move(v));
                        unknown(candidate,extension.sourceSchema,row,path,{"id","name","kind","notes","coordinates","source"});
                    } else if(extension.jsonPointer=="/hydroEdits" || extension.jsonPointer=="/genericFeatures") {
                        const bool hydro=extension.jsonPointer=="/hydroEdits";
                        if(hydro?hasId(candidate.hydro):hasId(candidate.genericFeatures)) continue;
                        const auto& props=field(row,"properties");
                        const auto geometryRef=geo(hydro?"hydro":"generic",field(row,"geometry"));
                        const auto name=textOr(props,"name"),notes=textOr(props,"notes");
                        const bool locked=props.object.count("locked")?boolean(field(props,"locked")):false;
                        if(hydro) {
                            HydroFeature v; v.id=objectId; v.name=name; v.notes=notes; v.geometry=geometryRef; v.locked=locked;
                            const auto shape=candidate.geometries.get(geometryRef);
                            v.kind=shape->type=="Polygon"||shape->type=="MultiPolygon"?"lake":"river"; v.source=readProvenance(props);
                            if(props.object.count("editorColor")) v.color=color(field(props,"editorColor")); candidate.hydro.push_back(std::move(v));
                            unknown(candidate,extension.sourceSchema,props,path+"/properties",{"name","notes","category","locked","editorColor","source","pandolab_schema_version","pandolab_domain","pandolab_id"});
                        } else {
                            GenericFeature v; v.id=objectId; v.name=name; v.notes=notes; v.geometry=geometryRef; v.locked=locked; v.source=readProvenance(props);
                            if(props.object.count("fallbackOnly"))v.fallbackOnly=boolean(field(props,"fallbackOnly"));
                            if(props.object.count("color")) v.color=color(field(props,"color")); candidate.genericFeatures.push_back(std::move(v));
                            unknown(candidate,extension.sourceSchema,props,path+"/properties",{"name","notes","locked","color","source","schemaVersion","fallbackOnly"});
                        }
                        unknown(candidate,extension.sourceSchema,row,path,{"id","type","geometry","properties"});
                    } else if(extension.jsonPointer=="/distributionLayers") {
                        if(hasId(candidate.distributionLayers)) continue;
                        DistributionLayer v; v.id=objectId; v.name=textOr(row,"name",objectId); v.type=textOr(row,"type");
                        v.color=row.object.count("color")?color(field(row,"color")):0x8c68d8;
                        v.locked=row.object.count("locked")?boolean(field(row,"locked")):false;
                        auto parent=textOr(row,"parentId"); if(!parent.empty()) v.parentId=parent;
                        if(row.object.count("groups")) for(const auto& group:array(field(row,"groups"))) v.groups.push_back(str(group));
                        v.validity=interval(row); if(row.object.count("metadata")) v.metadata=field(row,"metadata").encode().toStdString();
                        jsonObject(v.metadata); candidate.distributionLayers.push_back(std::move(v));
                        unknown(candidate,extension.sourceSchema,row,path,{"id","schemaVersion","name","type","color","locked","parentId","groups","validFrom","validTo","metadata"});
                    } else {
                        if(hasId(candidate.distributionEntries)) continue;
                        DistributionEntry v; v.id=objectId; v.layerId=textOr(row,"layerId"); const auto mode=textOr(row,"mode");
                        require(mode=="territorial"||mode=="geometry","INVALID_DISTRIBUTION: mode");
                        if(mode=="territorial") v.territory=territorialRef(textOr(row,"territorialUnitId")); else v.geometry=geo("distributionEntry",field(row,"geometry"));
                        v.share=row.object.count("share")?number(field(row,"share")):100; v.certainty=textOr(row,"certainty","unknown");
                        v.validity=interval(row); if(row.object.count("metadata")) v.metadata=field(row,"metadata").encode().toStdString();
                        jsonObject(v.metadata); candidate.distributionEntries.push_back(std::move(v));
                        unknown(candidate,extension.sourceSchema,row,path,{"id","schemaVersion","layerId","mode","territorialUnitId","geometry","share","certainty","validFrom","validTo","metadata"});
                    }
                }
                auto archived=std::find_if(candidate.extensions.begin(),candidate.extensions.end(),[&](const auto& e){return e.id==id;});
                archived->status="migrationArchive"; archived->dependencies.clear(); archived->forbiddenEffects.clear();
            }
            validateDocument(candidate); document=std::move(candidate);
        } catch(const std::exception& error) { diagnostics.push_back(paths.front()+": "+error.what()); }
    }
    const auto originalExtensions=document.extensions;
    for(const auto& e:originalExtensions) {
        if(e.status!="unsupported" || e.sourceFormat.rfind("pandolab-",0)!=0 || e.dependencies.size()!=1) continue;
        const auto slash=e.jsonPointer.rfind('/'); const auto fieldName=e.jsonPointer.substr(slash+1);
        if(fieldName!="capital" && fieldName!="flagDataUrl") continue;
        try {
            auto candidate=document; const auto ref=e.dependencies.front();
            const auto value=losslessjson::parse(QByteArray::fromStdString(e.payload));
            if(fieldName=="capital") {
                if(!candidate.countryDetails.count(ref)) candidate.countryDetails[ref]={str(value)};
            } else if(!candidate.symbols.count(ref)) {
                TerritorialSymbolStyle symbol;
                if(value.kind==V::Null) symbol.policy=FlagPolicy::None;
                else {
                    symbol.policy=FlagPolicy::Embedded; symbol.embeddedDataUrl=str(value);
                    const auto url=QByteArray::fromStdString(symbol.embeddedDataUrl); const auto comma=url.indexOf(',');
                    require(comma>0 && url.startsWith("data:image/") && url.left(comma).endsWith(";base64"),"INVALID_FLAG: embedded image required");
                    const auto bytes=QByteArray::fromBase64(url.mid(comma+1),QByteArray::AbortOnBase64DecodingErrors);
                    require(!bytes.isEmpty() && !QImage::fromData(bytes).isNull(),"INVALID_FLAG: image decode failed");
                }
                candidate.symbols[ref]=std::move(symbol);
            }
            auto old=std::find_if(candidate.extensions.begin(),candidate.extensions.end(),[&](const auto& x){return x.id==e.id;});
            old->status="migrationArchive"; old->dependencies.clear(); old->forbiddenEffects.clear();
            validateDocument(candidate); document=std::move(candidate);
        } catch(const std::exception& error) { diagnostics.push_back(e.jsonPointer+": "+error.what()); }
    }
    return diagnostics;
}

pandoeditor::ProjectDocument decode(const QByteArray& data) {
    const auto root=losslessjson::parse(data);
    require(str(field(root,"format"))=="pandoeditor-project","UNSUPPORTED_FORMAT: expected Qt project");
    const auto schema=integer(field(root,"version")); require(schema>=1 && schema<=7,"UNSUPPORTED_VERSION: expected Qt v1 through v7");
    ProjectDocument d;d.nativeSourceVersion=int(schema);
    bool canonicalLabelSettings=false,canonicalDistributionSettings=false;
    if (schema<3) {
        d.documentId="legacy-"+QCryptographicHash::hash(root.encode(),QCryptographicHash::Sha256).toHex().toStdString();
        if (schema==1) d.presentation.userLayers.push_back({"countries","국가"});
        else for (const auto& v:array(field(root,"layers"))) d.presentation.userLayers.push_back(layer(v,d,schema,"/layers/"+std::to_string(d.presentation.userLayers.size())));
        for (const auto& v:array(field(root,"countries"))) {
            const auto path="/countries/"+std::to_string(d.units.size());
            TerritorialUnit u; u.id=str(field(v,"id")); u.name=str(field(v,"name")); u.baseName=u.name;
            u.geometry={"legacy-geometry-"+std::to_string(d.units.size()),1};
            auto g=geometry(field(v,"geometry"),d,schema,path+"/geometry"); require(g.type=="MultiPolygon","INVALID_GEOMETRY: legacy country requires MultiPolygon");
            d.geometries.insert(u.geometry,std::move(g));
            auto r=territorialRef(u.id);
            ObjectStyle style; style.color=color(field(v,"color"));
            if (schema==2) { u.notes=str(field(v,"memo")); style.opacity=number(field(v,"opacity")); d.presentation.membership.emplace(r,str(field(v,"layerId"))); }
            else d.presentation.membership.emplace(r,"countries");
            d.presentation.objectStyles.emplace(r,style);
            if (schema==1) unknown(d,schema,v,path,{"id","name","color","geometry"});
            else unknown(d,schema,v,path,{"id","name","memo","opacity","layerId","color","geometry"});
            d.units.push_back(std::move(u));
        }
        if (schema==1) unknown(d,schema,root,"",{"format","version","countries"});
        else unknown(d,schema,root,"",{"format","version","countries","layers"});
    } else {
        d.documentId=str(field(root,"documentId"));
        // Existing archives stay archives; their payloads never overwrite canonical fields.
        if (root.object.count("extensions")) readExtensions(field(root,"extensions"),d);
        for (const auto& v:array(field(root,"geometries"))) {
            auto path="/geometries/"+std::to_string(d.geometries.versions().size());
            GeometryRef r{str(field(v,"id")),integer(field(v,"version"))};
            require(!d.geometries.get(r),"DUPLICATE_ID: geometry version");
            d.geometries.insert(r,geometry(field(v,"geojson"),d,d.nativeSourceVersion,path+"/geojson"));
            unknown(d,d.nativeSourceVersion,v,path,{"id","version","geojson"});
        }
        for (const auto& v:array(field(root,"units"))) {
            auto path="/units/"+std::to_string(d.units.size()); TerritorialUnit u;
            u.id=str(field(v,"id")); u.name=str(field(v,"name")); u.baseName=u.name; u.notes=str(field(v,"notes"));
            auto kind=str(field(v,"kind")); require(kind=="country"||kind=="subunit"||kind=="region","INVALID_JSON: territorial kind");
            u.kind=kind=="country"?UnitKind::Country:kind=="subunit"?UnitKind::Subunit:UnitKind::Region;
            u.geometry=geometryRef(field(v,"geometryRef"),d,path+"/geometryRef");
            u.locked=boolean(field(v,"locked")); u.coverageMode=str(field(v,"coverageMode"));
            u.validity=validity(field(v,"validity"),d,path+"/validity");
            if(schema>=7&&v.object.count("libraryOrigin")&&field(v,"libraryOrigin").kind!=V::Null)
                u.libraryOrigin=libraryOrigin(field(v,"libraryOrigin"),d,path+"/libraryOrigin");
            if(schema>=4) {
                u.baseName=str(field(v,"baseName"));u.nameExplicit=boolean(field(v,"nameExplicit"));
                require(u.kind==UnitKind::Country || (u.baseName.empty() && u.nameExplicit),"INVALID_JSON: country-only name state");
                require(u.nameExplicit || u.name.empty(),"INVALID_JSON: automatic country name must be empty");
                if(schema>=7)unknown(d,7,v,path,{"id","kind","name","notes","geometryRef","locked","coverageMode","validity","baseName","nameExplicit","libraryOrigin"});
                else unknown(d,4,v,path,{"id","kind","name","notes","geometryRef","locked","coverageMode","validity","baseName","nameExplicit"});
            } else {
                if(u.kind!=UnitKind::Country)u.baseName.clear();
                unknown(d,d.nativeSourceVersion,v,path,{"id","kind","name","notes","geometryRef","locked","coverageMode","validity"});
            }
            d.units.push_back(std::move(u));
        }
        for (const auto& v:array(field(root,"relations"))) {
            auto path="/relations/"+std::to_string(d.relations.size()); TerritorialRelation r;
            r.id=str(field(v,"id")); r.unit=ref(field(v,"unitRef"),d,path+"/unitRef");
            if (field(v,"parentRef").kind!=V::Null) r.parent=ref(field(v,"parentRef"),d,path+"/parentRef");
            if (field(v,"sovereignRef").kind!=V::Null) r.sovereign=ref(field(v,"sovereignRef"),d,path+"/sovereignRef");
            const auto mode=str(field(v,"mode")); require(mode=="base"||mode=="dated","INVALID_JSON: relation mode"); r.dated=mode=="dated";
            r.validity=validity(field(v,"validity"),d,path+"/validity");
            unknown(d,d.nativeSourceVersion,v,path,{"id","unitRef","parentRef","sovereignRef","mode","validity"}); d.relations.push_back(std::move(r));
        }
        const auto& p=field(root,"presentation");
        for (const auto& v:array(field(p,"userLayers"))) d.presentation.userLayers.push_back(layer(v,d,3,"/presentation/userLayers/"+std::to_string(d.presentation.userLayers.size())));
        std::size_t membershipIndex=0;
        for (const auto& v:array(field(p,"membership"))) {
            auto path="/presentation/membership/"+std::to_string(membershipIndex++);
            auto r=ref(field(v,"ref"),d,path+"/ref"); auto l=str(field(v,"layerId"));
            if (r.domain=="territorial") {
                require(d.presentation.membership.emplace(r,l).second,"DUPLICATE_ID: membership");
                unknown(d,d.nativeSourceVersion,v,path,{"ref","layerId"});
            } else preserve(d,3,path,v);
        }
        const auto& styles=field(p,"objectStyles"); require(styles.kind==V::Object,"INVALID_JSON: objectStyles");
        for (const auto& [domain,items]:styles.object) {
            auto path=pointer("/presentation/objectStyles",domain);
            if (domain!="territorial") { preserve(d,3,path,items); continue; }
            require(items.kind==V::Object,"INVALID_JSON: domain styles");
            for (const auto& [id,v]:items.object) {
                const bool automatic=schema>=4 && field(v,"color").kind==V::Null;
                d.presentation.objectStyles.emplace(ObjectRef{domain,id},ObjectStyle{automatic?0:color(field(v,"color")),number(field(v,"opacity")),!automatic});
                unknown(d,d.nativeSourceVersion,v,pointer(path,id),{"color","opacity"});
            }
        }
        if(schema>=5) {
            const auto& w=field(p,"webPresentation");auto& out=d.presentation.webPresentation;
            canonicalLabelSettings=schema>=6&&w.object.count("labelSettings");
            canonicalDistributionSettings=schema>=6&&w.object.count("distributionSettings");
            require(field(w,"visibility").kind==V::Object && field(w,"hiddenItems").kind==V::Object,"INVALID_JSON: presentation visibility");
            for(const auto& [key,value]:field(w,"visibility").object) {
                if(presentationmigration::group(key)||presentationmigration::contentGroup(key)||presentationmigration::symbol(key))out.visibility[key]=boolean(value);
                else preserve(d,5,pointer("/presentation/webPresentation/visibility",key),value);
            }
            for(const auto& [group,values]:field(w,"hiddenItems").object) {
                if(presentationmigration::group(group)||presentationmigration::contentGroup(group))for(const auto& id:array(values))out.hiddenItems[group].insert(str(id));
                else preserve(d,5,pointer("/presentation/webPresentation/hiddenItems",group),values);
            }
            auto readStyles=[&](const char* name,auto& target){
                const auto& values=field(w,name);require(values.kind==V::Object,"INVALID_JSON: presentation styles");
                for(const auto& [key,value]:values.object){
                    const bool supported=std::string(name)=="styles"?(presentationmigration::group(key)||presentationmigration::contentGroup(key)):key.rfind("territorial:subunit:",0)==0||key.rfind("territorial:region:",0)==0;
                    if(!supported){preserve(d,5,pointer(std::string("/presentation/webPresentation/")+name,key),value);continue;}
                    PresentationStyle s;require(value.kind==V::Object,"INVALID_JSON: presentation style");
                    if(value.object.count("opacity"))s.opacity=number(field(value,"opacity"));
                    if(value.object.count("boundaryVisible"))s.boundaryVisible=boolean(field(value,"boundaryVisible"));
                    if(value.object.count("labelsVisible"))s.labelsVisible=boolean(field(value,"labelsVisible"));
                    if(value.object.count("boundaryWidth"))s.boundaryWidth=number(field(value,"boundaryWidth"));
                    if(value.object.count("blendMode"))s.blendMode=str(field(value,"blendMode"));
                    target[key]=s;unknown(d,5,value,pointer(std::string("/presentation/webPresentation/")+name,key),{"opacity","boundaryVisible","labelsVisible","boundaryWidth","blendMode"});
                }
            };
            readStyles("styles",out.styles);readStyles("objectStyles",out.objectStyles);
            const auto& encodedOrder=field(w,"objectOrder");
            bool retainedOrder=false;
            for(const auto& key:array(encodedOrder)) {
                if(key.kind==V::String &&
                    (key.string.rfind("territorial:subunit:",0)==0||key.string.rfind("territorial:region:",0)==0))
                    out.objectOrder.push_back(key.string);
                else retainedOrder=true;
            }
            // Keep the original array when a foreign entry is present.  The
            // canonical order only owns territorial subunit/region keys;
            // silently normalizing other domains here would be destructive.
            if(retainedOrder) preserve(d,5,"/presentation/webPresentation/objectOrder",encodedOrder);
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
                require(mode=="dominant"||mode=="intensity","INVALID_DISTRIBUTION_SETTINGS");
                out.distributionSettings.renderMode=mode=="intensity"?DistributionRenderMode::Intensity:DistributionRenderMode::Dominant;
                out.distributionSettings.boundaryVisible=boolean(field(value,"boundaryVisible"));
                unknown(d,6,value,"/presentation/webPresentation/distributionSettings",{"renderMode","boundaryVisible"});
            }
            unknown(d,5,w,"/presentation/webPresentation",{"visibility","hiddenItems","styles","objectStyles","objectOrder","labelSettings","distributionSettings"});
            validatePresentation(d); // Content owners are decoded below.
        }
        if(schema>=5)unknown(d,d.nativeSourceVersion,p,"/presentation",{"userLayers","membership","objectStyles","webPresentation"});
        else unknown(d,d.nativeSourceVersion,p,"/presentation",{"userLayers","membership","objectStyles"});
        if(schema>=6) readContent(field(root,"content"),d);
        if(schema>=6) unknown(d,d.nativeSourceVersion,root,"",{"format","version","documentId","units","relations","geometries","presentation","extensions","content"});
        else unknown(d,d.nativeSourceVersion,root,"",{"format","version","documentId","units","relations","geometries","presentation","extensions"});
    }
    if(schema<5) {
        const bool webSource=std::any_of(d.extensions.begin(),d.extensions.end(),[](const auto& e){return e.status=="migrationArchive"&&e.sourceFormat.rfind("pandolab-",0)==0;});
        if(webSource) {
            promoteContent(d);
            presentationmigration::promote(d);
            const auto& p=d.presentation.webPresentation;
            bool adapters=d.presentation.userLayers.size()==3;
            const std::map<std::string,std::string> names={{"countries","국가"},{"subunits","하위단위"},{"regions","지방"}};
            for(const auto& l:d.presentation.userLayers){auto n=names.find(l.id);auto s=p.styles.find(l.id);const double opacity=s==p.styles.end()?1:s->second.opacity.value_or(1);adapters=adapters&&n!=names.end()&&n->second==l.name&&!l.locked&&l.visible==groupVisible(p,l.id)&&l.opacity==opacity;}
            for(const auto& u:d.units)adapters=adapters&&nativeLayerId(d,territorialRef(u.id))==territorialGroup(u.kind);
            if(adapters){d.presentation.userLayers.clear();d.presentation.membership.clear();}
        }
    }
    promoteContent(d);
    if(schema>=5) presentationmigration::promote(d,!canonicalLabelSettings,!canonicalDistributionSettings);
    normalizePresentation(d);
    validateDocument(d); return d;
}

QByteArray encode(const pandoeditor::Project& project) {
    const auto& d=project.document(); validateDocument(d);
    V units=V::arr(),relations=V::arr(),geometries=V::arr(),layers=V::arr(),membership=V::arr(),styles=V::obj(),extensions=V::arr();
    for (const auto& u:d.units) {
        auto value=object({{"id",V::str(u.id)},{"kind",V::str(u.kind==UnitKind::Country?"country":u.kind==UnitKind::Subunit?"subunit":"region")},{"name",V::str(u.name)},{"baseName",V::str(u.baseName)},{"nameExplicit",V::boolean(u.kind==UnitKind::Country?u.nameExplicit&&!u.name.empty():u.nameExplicit)},{"notes",V::str(u.kind==UnitKind::Country?trimWebText(u.notes):u.notes)},{"geometryRef",geometryRefValue(u.geometry)},{"locked",V::boolean(u.locked)},{"coverageMode",V::str(u.coverageMode)},{"validity",validityValue(u.validity)}});
        if(u.libraryOrigin)value.object["libraryOrigin"]=libraryOriginValue(*u.libraryOrigin);
        units.array.push_back(std::move(value));
    }
    for (const auto& r:d.relations) relations.array.push_back(object({{"id",V::str(r.id)},{"unitRef",refValue(r.unit)},{"parentRef",r.parent?refValue(*r.parent):V{}},{"sovereignRef",r.sovereign?refValue(*r.sovereign):V{}},{"mode",V::str(r.dated?"dated":"base")},{"validity",validityValue(r.validity)}}));
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
    V visibility=V::obj(),hidden=V::obj(),groups=V::obj(),overrides=V::obj(),order=V::arr(),labelSettings=V::arr();
    for(const auto& [key,value]:web.visibility)visibility.object[key]=V::boolean(value);
    for(const auto& [key,ids]:web.hiddenItems){V values=V::arr();for(const auto& id:ids)values.array.push_back(V::str(id));hidden.object[key]=values;}
    auto writeStyles=[](const auto& source,V& target){for(const auto& [key,s]:source){V v=V::obj();if(s.opacity)v.object["opacity"]=V::num(*s.opacity);if(s.boundaryVisible)v.object["boundaryVisible"]=V::boolean(*s.boundaryVisible);if(s.labelsVisible)v.object["labelsVisible"]=V::boolean(*s.labelsVisible);if(s.boundaryWidth)v.object["boundaryWidth"]=V::num(*s.boundaryWidth);if(s.blendMode)v.object["blendMode"]=V::str(*s.blendMode);target.object[key]=v;}};
    writeStyles(web.styles,groups);writeStyles(web.objectStyles,overrides);for(const auto& key:web.objectOrder)order.array.push_back(V::str(key));
    for(const auto& [owner,s]:web.labelSettings){V value=object({{"ref",refValue(owner)},{"pinned",V::boolean(s.pinned)},{"collisionGroup",V::str(s.collisionGroup)}});if(s.priority)value.object["priority"]=V::num(*s.priority);if(s.minZoom)value.object["minZoom"]=V::num(*s.minZoom);if(s.maxZoom)value.object["maxZoom"]=V::num(*s.maxZoom);if(s.manualPosition){V point=V::arr();point.array={V::num(s.manualPosition->x),V::num(s.manualPosition->y)};value.object["manualPosition"]=point;}labelSettings.array.push_back(std::move(value));}
    auto distributionSettings=object({{"renderMode",V::str(web.distributionSettings.renderMode==DistributionRenderMode::Intensity?"intensity":"dominant")},{"boundaryVisible",V::boolean(web.distributionSettings.boundaryVisible)}});
    auto webValue=object({{"visibility",visibility},{"hiddenItems",hidden},{"styles",groups},{"objectStyles",overrides},{"objectOrder",order},{"labelSettings",labelSettings},{"distributionSettings",distributionSettings}});
    auto root=object({{"format",V::str("pandoeditor-project")},{"version",V::num(5)},{"documentId",V::str(d.documentId)},{"units",units},{"relations",relations},{"geometries",geometries},{"presentation",object({{"userLayers",layers},{"membership",membership},{"objectStyles",styles},{"webPresentation",webValue}})},{"extensions",extensions}});
    root.object["version"]=V::num(7);
    root.object["content"]=contentValue(d);
    auto bytes=root.encode()+"\n";
    require(bytes.size()<=256ll*1024*1024,"LIMIT_EXCEEDED: encoded JSON exceeds 256 MiB");
    return bytes;
}
}
