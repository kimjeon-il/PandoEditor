#include "webimport.h"
#include "webjson.h"
#include <set>

namespace webimport {
namespace {
using namespace webjson;
V source(const V& input,const V& defaults=V::obj()) {
    V raw=input.kind==V::String?obj({{"dataset",input}}):objectOrEmpty(input);
    V details=objectOrEmpty(at(defaults,"details"));merge(details,objectOrEmpty(at(raw,"details")));
    const std::set<std::string> keys={"schemaVersion","kind","dataset","sourceId","sourceFormat","sourceType","version","importedAt","details","id","format","type"};
    V extra=V::obj();for(const auto& [k,v]:raw.object)if(!keys.count(k))extra.object[k]=v;
    if(!extra.object.empty()) {auto prev=objectOrEmpty(at(details,"unmappedSourceFields"));merge(prev,extra);details.object["unmappedSourceFields"]=prev;}
    auto kind=text(either(at(raw,"kind"),at(defaults,"kind")));
    if(!std::set<std::string>{"user","builtin","library","gis","legacy","plugin","unsupported"}.count(kind))kind="unsupported";
    auto r=obj({{"schemaVersion",V::num(1)},{"kind",V::str(kind)},{"details",details}});
    for(auto key:{"dataset","sourceId","sourceFormat","sourceType","version","importedAt"}) {
        const V* value=&at(raw,key);
        if(value->kind==V::Null) {
            const std::string alias=std::string(key)=="sourceId"?"id":std::string(key)=="sourceFormat"?"format":std::string(key)=="sourceType"?"type":"";
            if(!alias.empty())value=&at(raw,alias);
        }
        if(value->kind==V::Null)value=&at(defaults,key);
        r.object[key]=V::str(text(*value));
    }
    return r;
}
V generic(V f) {
    auto props=objectOrEmpty(at(f,"properties"));
    auto legacy=V::obj(),extra=V::obj();
    const std::set<std::string> canonical={"schemaVersion","name","notes","color","locked","source"};
    const std::set<std::string> semantic={"role","ownerId","parentId","landBinding","topologyGroup"};
    for(const auto& [k,v]:props.object) {
        if(semantic.count(k)) {if(v.kind!=V::Null&&!(v.kind==V::String&&v.string.empty()))legacy.object[k]=v;}
        else if(!canonical.count(k))extra.object[k]=v;
    }
    double version=truth(at(props,"schemaVersion"))?number(at(props,"schemaVersion")):1;
    bool hasLegacy=!legacy.object.empty()||!extra.object.empty();
    auto s=source(at(props,"source"),obj({{"kind",V::str(version<=1||hasLegacy?"legacy":"unsupported")},
        {"sourceFormat",V::str(version<=1?"pandolab-generic-v1":hasLegacy?"pandolab-generic-compat":"")}}));
    auto details=at(s,"details");
    if(!legacy.object.empty()) {auto previous=objectOrEmpty(at(details,"legacyGenericSemantics"));merge(previous,legacy);details.object["legacyGenericSemantics"]=previous;}
    if(!extra.object.empty()) {auto previous=objectOrEmpty(at(details,"legacyProperties"));merge(previous,extra);details.object["legacyProperties"]=previous;}
    s.object["details"]=details;
    auto color=text(at(props,"color"));
    f.object["properties"]=obj({{"schemaVersion",V::num(2)},{"name",V::str(text(at(props,"name")))},
        {"notes",V::str(text(at(props,"notes")))},{"color",V::str(color.empty()?"#8c68d8":color)},
        {"locked",V::boolean(isTrue(at(props,"locked")))},{"source",source(s)}});
    const auto id=text(at(f,"id"));require(!id.empty(),"INVALID_ID: generic ID is empty");
    const auto& g=at(f,"geometry");auto type=text(at(g,"type"));
    require(std::set<std::string>{"Point","MultiPoint","LineString","MultiLineString","Polygon","MultiPolygon"}.count(type)&&at(g,"coordinates").kind==V::Array&&!at(g,"coordinates").array.empty(),"INVALID_GEOMETRY: generic "+id);
    return f;
}
void countries(V& p,const std::string& field,bool delta=false) {
    V collection=at(p,field);
    if(delta)collection=obj({{"type",V::str("FeatureCollection")},{"features",at(collection,"changed")}});
    if(!has(collection,"features"))return;
    V overrides=objectOrEmpty(at(p,"countryOverrides")),next=V::arr();
    int index=0;for(const auto& f:array(at(collection,"features"),field+"/features")) {
        const auto& props=at(f,"properties");
        auto first=[&](std::initializer_list<const char*> names,const V* initial=nullptr) {
            if(initial&&truth(*initial))return text(*initial);
            for(auto name:names)if(truth(at(props,name)))return text(at(props,name));
            return std::string{};
        };
        ++index;auto id=first({"editor_id","iso_a3","ISO_A3","ADM0_A3"},&at(f,"id"));
        if(id.empty())id="country_"+std::to_string(index);
        auto name=first({"name","editor_name","editor_original_name","ADMIN","NAME","NAME_LONG"});if(name.empty())name=id;
        V ov=V::obj();
        for(const auto& [key,old]:std::initializer_list<std::pair<const char*,const char*>>{{"name","editor_name"},{"color","editor_color"},{"capital","editor_capital"},{"notes","editor_notes"},{"flagDataUrl","editor_flag_data_url"}}) {
            auto value=text(at(props,old));if(!value.empty())ov.object[key]=V::str(value);
        }
        if(isTrue(at(props,"editor_locked"))||isTrue(at(props,"locked")))ov.object["locked"]=V::boolean(true);
        if(!ov.object.empty()) {merge(ov,at(overrides,id));overrides.object[id]=ov;}
        V properties=obj({{"name",V::str(name)}});
        for(auto k:{"validFrom","validTo"}) {auto v=text(at(props,k));if(!v.empty())properties.object[k]=V::str(v);}
        next.array.push_back(obj({{"type",V::str("Feature")},{"id",V::str(id)},{"properties",properties},{"geometry",at(f,"geometry")}}));
    }
    if(delta)p.object[field].object["changed"]=next;
    else p.object[field]=obj({{"type",V::str("FeatureCollection")},{"features",next}});
    p.object["countryOverrides"]=overrides;
}
void v3(V& p) {
    countries(p,"countriesData");
    if(has(at(p,"countryDelta"),"changed"))countries(p,"countryDelta",true);
    if(!has(p,"countryOverrides"))p.object["countryOverrides"]=V::obj();
    V rows=V::arr();std::set<std::string> ids;
    for(auto key:{"genericFeatures","drawings"})if(at(p,key).kind==V::Array)for(const auto& f:at(p,key).array) {
        auto r=generic(f);require(ids.insert(text(at(r,"id"))).second,"DUPLICATE_ID: generic");rows.array.push_back(std::move(r));
    }
    p.object["genericFeatures"]=rows;p.object.erase("drawings");
    for(auto key:{"layerVisibility","itemVisibility"}) {
        if(!has(p,key)||p.object[key].kind!=V::Object)continue;
        auto& c=p.object[key];for(auto alias:{"drawings","userDrawings"}) {
            if(at(c,"genericFeatures").kind==V::Null&&at(c,alias).kind!=V::Null)c.object["genericFeatures"]=at(c,alias);
            c.object.erase(alias);
        }
    }
    if(has(p,"layerPresentation")) {
        auto& l=p.object["layerPresentation"];
        if(at(l,"styles").kind==V::Object)for(auto alias:{"drawings","userDrawings"}) {
            auto& styles=l.object["styles"];
            if(at(styles,"genericFeatures").kind==V::Null&&at(styles,alias).kind!=V::Null)styles.object["genericFeatures"]=at(styles,alias);
            styles.object.erase(alias);
        }
        if(at(l,"overlayOrder").kind==V::Array)for(auto& g:l.object["overlayOrder"].array)if(g.string=="drawings"||g.string=="userDrawings")g=V::str("genericFeatures");
    }
    p.object["landObjectModel"]=obj({{"schemaVersion",V::num(2)},{"coastlineAuthority",V::str("countries")},{"purpose",V::str("lossless-fallback")},{"directCreation",V::boolean(false)},{"sourceProvenanceSchemaVersion",V::num(1)},
        {"canonicalProperties",arr({V::str("name"),V::str("notes"),V::str("color"),V::str("locked"),V::str("source")})}});
    p.object["schemaVersion"]=V::num(4);
}
V migratedKey(V v) {
    if(v.kind!=V::String)return v;
    for(const auto& prefix:{std::string("territorial:territory:"),std::string("territorial:admin:")})
        if(v.string.rfind(prefix,0)==0)return V::str("territorial:subunit:"+v.string.substr(prefix.size()));
    return v;
}
void v4(V& p) {
    auto rows=optionalArray(at(p,"territorialUnits"),"/territorialUnits");
    V presentation=objectOrEmpty(at(p,"layerPresentation"));
    V styles=objectOrEmpty(at(presentation,"objectStyles")),order=V::arr(),groups=V::arr();
    for(auto& k:optionalArray(at(presentation,"objectOrder"),"/layerPresentation/objectOrder"))order.array.push_back(migratedKey(k));
    for(auto& g:optionalArray(at(presentation,"overlayOrder"),"/layerPresentation/overlayOrder"))uniqueAppend(groups,g);
    for(auto g:{"religions","ethnicities","languages","administrative","territories","regions","genericFeatures"})uniqueAppend(groups,V::str(g));
    auto legacyType=[](const std::string& t){return t=="admin"||t=="territory";};
    for(auto& group:groups.array)for(auto& f:rows) {
        auto type=text(at(at(f,"properties"),"unitType"));
        if(!legacyType(type)||group.string!=(type=="admin"?"administrative":"territories"))continue;
        auto key="territorial:subunit:"+jsString(at(f,"id"));order.array.push_back(V::str(key));
        styles.object[key]=objectOrEmpty(at(at(presentation,"styles"),group.string));
    }
    auto visibility=objectOrEmpty(at(p,"layerVisibility")),items=objectOrEmpty(at(p,"itemVisibility"));
    auto subvisible=objectOrEmpty(at(items,"subunits"));
    for(auto& f:rows) {
        auto& props=f.object["properties"];require(props.kind==V::Object,"INVALID_JSON: territorial properties");
        auto type=text(at(props,"unitType")),id=jsString(at(f,"id"));
        if(type=="subunit")subvisible.object[id]=V::boolean(!isFalse(at(visibility,"subunits"))&&!isFalse(at(subvisible,id)));
        else if(legacyType(type)) {
            auto group=type=="admin"?"administrative":"territories";
            subvisible.object[id]=V::boolean(!isFalse(at(visibility,group))&&!isFalse(at(at(items,group),id)));
            props.object["unitType"]=V::str("subunit");
        }
        props.object.erase("adminLevel");props.object.erase("isRemainder");
        if(at(props,"metadata").kind==V::Object)props.object["metadata"].object.erase("legacyTerritorialPartition");
        props.object["schemaVersion"]=V::num(2);
    }
    V newRows=V::arr();newRows.array=std::move(rows);p.object["territorialUnits"]=std::move(newRows);
    visibility.object["subunits"]=V::boolean(true);items.object["subunits"]=subvisible;
    for(auto group:{"territories","administrative"}){visibility.object.erase(group);items.object.erase(group);}
    p.object["layerVisibility"]=visibility;p.object["itemVisibility"]=items;
    auto groupStyles=objectOrEmpty(at(presentation,"styles"));
    groupStyles.object["subunits"]=truth(at(groupStyles,"subunits"))?at(groupStyles,"subunits"):V::obj();
    groupStyles.object.erase("territories");groupStyles.object.erase("administrative");
    V newOrder=V::arr(),newGroups=V::arr();for(auto& k:order.array)uniqueAppend(newOrder,k);
    for(auto& g:groups.array)uniqueAppend(newGroups,g.string=="territories"||g.string=="administrative"?V::str("subunits"):g);
    presentation.object["schemaVersion"]=V::num(3);presentation.object["styles"]=groupStyles;
    presentation.object["objectStyles"]=styles;presentation.object["objectOrder"]=newOrder;presentation.object["overlayOrder"]=newGroups;
    p.object["layerPresentation"]=presentation;
    auto model=objectOrEmpty(at(p,"territorialModel"));model.object["schemaVersion"]=V::num(2);
    model.object["types"]=arr({V::str("country"),V::str("subunit"),V::str("region")});p.object["territorialModel"]=model;
    for(auto key:{"selected","selectionAnchor"})if(at(at(p,key),"domain").string=="territorial"&&legacyType(text(at(at(p,key),"type")))) {
        auto& r=p.object[key];r.object["type"]=V::str("subunit");if(truth(at(r,"key")))r.object["key"]=migratedKey(at(r,"key"));
    }
    p.object["schemaVersion"]=V::num(5);
}
FileKind classifyValue(const V& p) {
    require(p.kind==V::Object,"UNSUPPORTED_FORMAT: expected project object");
    const auto f=text(at(p,"format"));
    if(f=="pandolab-autosave-delta")throw std::invalid_argument("BASE_DATA_REQUIRED: 웹에서 완전 저장본을 내보내 주세요.");
    if(f=="pandoeditor-project") {
        double v=number(at(p,"version"));require(std::isfinite(v)&&std::floor(v)==v&&v>=1&&v<=4,"UNSUPPORTED_VERSION: Qt version");return FileKind::QtProject;
    }
    require(f=="pandolab-project-state"||f=="pandolab-autosave-full","UNSUPPORTED_FORMAT: not a web full project");
    double v=number(at(p,"schemaVersion"));require(std::isfinite(v)&&std::floor(v)==v&&v>=3&&v<=5,"UNSUPPORTED_VERSION: web schema 3..5 required");
    require(!has(p,"countryDelta"),"BASE_DATA_REQUIRED: delta content needs base data");return FileKind::WebFull;
}
}
FileKind classify(const QByteArray& bytes) {return classifyValue(losslessjson::parse(bytes));}
Migration migrate(const QByteArray& bytes) {
    auto p=losslessjson::parse(bytes);require(classifyValue(p)==FileKind::WebFull,"UNSUPPORTED_FORMAT: use Qt file open");
    Migration result;result.sourceFormat=QString::fromStdString(text(at(p,"format")));result.sourceSchema=static_cast<int>(number(at(p,"schemaVersion")));
    if(result.sourceSchema==3)v3(p);
    if(result.sourceSchema<=4)v4(p);
    result.normalized=p.encode();return result;
}
}
