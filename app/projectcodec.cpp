#include "projectcodec.h"
#include "losslessjson.h"
#include <QCryptographicHash>
#include <QRegularExpression>
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
    unknown(d,3,v,path,{"domain","id"}); return r;
}
V refValue(const ObjectRef& r) { return object({{"domain",V::str(r.domain)},{"id",V::str(r.id)}}); }
GeometryRef geometryRef(const V& v,ProjectDocument& d,const std::string& path) {
    GeometryRef r{str(field(v,"id")),integer(field(v,"version"))};
    unknown(d,3,v,path,{"id","version"}); return r;
}
V geometryRefValue(const GeometryRef& r) { return object({{"id",V::str(r.id)},{"version",V::num(r.version)}}); }
std::optional<std::string> endpoint(const V& v,ProjectDocument& d,const std::string& path) {
    if (v.kind==V::Null) return {};
    auto text=str(field(v,"text")); auto temporal=parseTemporal(text);
    require(str(field(v,"precision"))==temporal.precision,"INVALID_DATE: precision does not match text");
    unknown(d,3,v,path,{"text","precision"}); return text;
}
Validity validity(const V& v,ProjectDocument& d,const std::string& path) {
    Validity r{endpoint(field(v,"from"),d,path+"/from"),endpoint(field(v,"to"),d,path+"/to")};
    unknown(d,3,v,path,{"from","to"}); temporalBounds(r); return r;
}
V endpointValue(const std::optional<std::string>& s) {
    if (!s) return {};
    return object({{"text",V::str(*s)},{"precision",V::str(parseTemporal(*s).precision)}});
}
V validityValue(const Validity& v) { return object({{"from",endpointValue(v.from)},{"to",endpointValue(v.to)}}); }
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
    const auto schema=integer(field(root,"version")); require(schema>=1 && schema<=3,"UNSUPPORTED_VERSION: expected Qt v1, v2 or v3");
    ProjectDocument d;
    if (schema<3) {
        d.documentId="legacy-"+QCryptographicHash::hash(root.encode(),QCryptographicHash::Sha256).toHex().toStdString();
        if (schema==1) d.presentation.userLayers.push_back({"countries","국가"});
        else for (const auto& v:array(field(root,"layers"))) d.presentation.userLayers.push_back(layer(v,d,schema,"/layers/"+std::to_string(d.presentation.userLayers.size())));
        for (const auto& v:array(field(root,"countries"))) {
            const auto path="/countries/"+std::to_string(d.units.size());
            TerritorialUnit u; u.id=str(field(v,"id")); u.name=str(field(v,"name"));
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
            d.geometries.insert(r,geometry(field(v,"geojson"),d,3,path+"/geojson"));
            unknown(d,3,v,path,{"id","version","geojson"});
        }
        for (const auto& v:array(field(root,"units"))) {
            auto path="/units/"+std::to_string(d.units.size()); TerritorialUnit u;
            u.id=str(field(v,"id")); u.name=str(field(v,"name")); u.notes=str(field(v,"notes"));
            auto kind=str(field(v,"kind")); require(kind=="country"||kind=="subunit"||kind=="region","INVALID_JSON: territorial kind");
            u.kind=kind=="country"?UnitKind::Country:kind=="subunit"?UnitKind::Subunit:UnitKind::Region;
            u.geometry=geometryRef(field(v,"geometryRef"),d,path+"/geometryRef");
            u.locked=boolean(field(v,"locked")); u.coverageMode=str(field(v,"coverageMode"));
            u.validity=validity(field(v,"validity"),d,path+"/validity");
            unknown(d,3,v,path,{"id","kind","name","notes","geometryRef","locked","coverageMode","validity"}); d.units.push_back(std::move(u));
        }
        for (const auto& v:array(field(root,"relations"))) {
            auto path="/relations/"+std::to_string(d.relations.size()); TerritorialRelation r;
            r.id=str(field(v,"id")); r.unit=ref(field(v,"unitRef"),d,path+"/unitRef");
            if (field(v,"parentRef").kind!=V::Null) r.parent=ref(field(v,"parentRef"),d,path+"/parentRef");
            if (field(v,"sovereignRef").kind!=V::Null) r.sovereign=ref(field(v,"sovereignRef"),d,path+"/sovereignRef");
            const auto mode=str(field(v,"mode")); require(mode=="base"||mode=="dated","INVALID_JSON: relation mode"); r.dated=mode=="dated";
            r.validity=validity(field(v,"validity"),d,path+"/validity");
            unknown(d,3,v,path,{"id","unitRef","parentRef","sovereignRef","mode","validity"}); d.relations.push_back(std::move(r));
        }
        const auto& p=field(root,"presentation");
        for (const auto& v:array(field(p,"userLayers"))) d.presentation.userLayers.push_back(layer(v,d,3,"/presentation/userLayers/"+std::to_string(d.presentation.userLayers.size())));
        std::size_t membershipIndex=0;
        for (const auto& v:array(field(p,"membership"))) {
            auto path="/presentation/membership/"+std::to_string(membershipIndex++);
            auto r=ref(field(v,"ref"),d,path+"/ref"); auto l=str(field(v,"layerId"));
            if (r.domain=="territorial") {
                require(d.presentation.membership.emplace(r,l).second,"DUPLICATE_ID: membership");
                unknown(d,3,v,path,{"ref","layerId"});
            } else preserve(d,3,path,v);
        }
        const auto& styles=field(p,"objectStyles"); require(styles.kind==V::Object,"INVALID_JSON: objectStyles");
        for (const auto& [domain,items]:styles.object) {
            auto path=pointer("/presentation/objectStyles",domain);
            if (domain!="territorial") { preserve(d,3,path,items); continue; }
            require(items.kind==V::Object,"INVALID_JSON: domain styles");
            for (const auto& [id,v]:items.object) {
                d.presentation.objectStyles.emplace(ObjectRef{domain,id},ObjectStyle{color(field(v,"color")),number(field(v,"opacity"))});
                unknown(d,3,v,pointer(path,id),{"color","opacity"});
            }
        }
        unknown(d,3,p,"/presentation",{"userLayers","membership","objectStyles"});
        unknown(d,3,root,"",{"format","version","documentId","units","relations","geometries","presentation","extensions"});
    }
    validateDocument(d); return d;
}

QByteArray encode(const pandoeditor::Project& project) {
    const auto& d=project.document(); validateDocument(d);
    V units=V::arr(),relations=V::arr(),geometries=V::arr(),layers=V::arr(),membership=V::arr(),styles=V::obj(),extensions=V::arr();
    for (const auto& u:d.units) units.array.push_back(object({{"id",V::str(u.id)},{"kind",V::str(u.kind==UnitKind::Country?"country":u.kind==UnitKind::Subunit?"subunit":"region")},{"name",V::str(u.name)},{"notes",V::str(u.notes)},{"geometryRef",geometryRefValue(u.geometry)},{"locked",V::boolean(u.locked)},{"coverageMode",V::str(u.coverageMode)},{"validity",validityValue(u.validity)}}));
    for (const auto& r:d.relations) relations.array.push_back(object({{"id",V::str(r.id)},{"unitRef",refValue(r.unit)},{"parentRef",r.parent?refValue(*r.parent):V{}},{"sovereignRef",r.sovereign?refValue(*r.sovereign):V{}},{"mode",V::str(r.dated?"dated":"base")},{"validity",validityValue(r.validity)}}));
    // Opaque domains may reference otherwise unreferenced stored geometries.
    for (const auto& [r,g]:d.geometries.versions()) geometries.array.push_back(object({{"id",V::str(r.id)},{"version",V::num(r.version)},{"geojson",geometryValue(*g)}}));
    for (const auto& l:d.presentation.userLayers) layers.array.push_back(layerValue(l));
    for (const auto& [r,l]:d.presentation.membership) membership.array.push_back(object({{"ref",refValue(r)},{"layerId",V::str(l)}}));
    for (const auto& [r,s]:d.presentation.objectStyles) {
        if (!styles.object.count(r.domain)) styles.object[r.domain]=V::obj();
        styles.object[r.domain].object[r.id]=object({{"color",colorValue(s.color)},{"opacity",V::num(s.opacity)}});
    }
    for (const auto& e:d.extensions) extensions.array.push_back(extensionValue(e));
    auto root=object({{"format",V::str("pandoeditor-project")},{"version",V::num(3)},{"documentId",V::str(d.documentId)},{"units",units},{"relations",relations},{"geometries",geometries},{"presentation",object({{"userLayers",layers},{"membership",membership},{"objectStyles",styles}})},{"extensions",extensions}});
    auto bytes=root.encode()+"\n";
    require(bytes.size()<=256ll*1024*1024,"LIMIT_EXCEEDED: encoded JSON exceeds 256 MiB");
    return bytes;
}
}
