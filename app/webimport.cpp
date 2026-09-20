#include "webimport.h"
#include "webjson.h"
#include "webpropertypreservation.h"
#include "projectcodec.h"
#include "presentationmigration.h"
#include <pandoeditor/project.h>
#include <QCryptographicHash>
#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace webimport {
namespace {
using namespace webjson;
using namespace pandoeditor;
constexpr qsizetype storageLimit=64ll*1024*1024;
QString digest(const QByteArray& b) {return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex());}
struct Builder {
    Candidate output;
    const V& root;
    const std::function<bool()>& cancelled;
    std::set<std::string> ids;
    void check() const {if(cancelled&&cancelled())throw std::invalid_argument("CANCELLED: 가져오기를 취소했습니다.");}
    void report(const std::string& path,const char* state,const QString& message,int count=0) {
        output.report.push_back(QVariantMap{{"path",QString::fromStdString(path)},{"status",state},{"message",message},{"count",count}});
    }
    void preserve(const std::string& path,const V& payload,bool known=false,std::vector<ObjectRef> dependencies={},bool archive=false) {
        check();PreservedExtension e;
        e.sourceFormat=output.sourceFormat.toStdString();e.sourceSchema=output.sourceSchema;
        e.jsonPointer=path;e.payload=payload.encode().toStdString();
        e.id="web-"+digest(QByteArray::fromStdString(path)+QByteArray::fromStdString(e.payload)).toStdString();
        e.status=archive?"migrationArchive":"unsupported";
        e.dependencyKnowledge=known?"known":"unknown";e.dependencies=std::move(dependencies);
        e.forbiddenEffects={"add","delete","type","geometry","relation","id","membership"};
        output.document.extensions.push_back(std::move(e));
    }
    void extras(const V& v,const std::string& path,const std::set<std::string>& mapped,const std::vector<ObjectRef>& dependencies={}) {
        for(const auto& [key,value]:v.object)if(!mapped.count(key)) {
            auto p=pointer(path,key);preserve(p,value,false,dependencies);
            report(p,"retained",QStringLiteral("원본 필드 보존 · 현재 편집하지 않는 데이터"));
        }
    }
    double coordinate(const V& v,const std::string& path) {
        require(v.kind==V::Number,"INVALID_GEOMETRY: "+path+" nonnumeric coordinate");
        double n=number(v);require(std::isfinite(n),"INVALID_GEOMETRY: "+path+" nonfinite coordinate");return n;
    }
    Point point(const V& v,const std::string& path) {
        const auto& a=array(v,path);require(a.size()==2,"UNSUPPORTED_GEOMETRY: "+path+" exactly two ordinates required; none discarded");
        return {coordinate(a[0],path+"/0"),coordinate(a[1],path+"/1")};
    }
    Ring line(const V& v,const std::string& path) {Ring r;std::size_t i=0;for(const auto& p:array(v,path)){check();r.push_back(point(p,path+"/"+std::to_string(i++)));}return r;}
    Polygon polygon(const V& v,const std::string& path) {Polygon r;std::size_t i=0;for(const auto& p:array(v,path))r.push_back(line(p,path+"/"+std::to_string(i++)));return r;}
    Geometry geometry(const V& v,const std::string& path,bool territory) {
        require(v.kind==V::Object,"INVALID_GEOMETRY: "+path+" missing geometry");Geometry g;g.type=text(at(v,"type"));
        const auto& c=at(v,"coordinates");
        if(g.type=="Polygon")g.polygons.push_back(polygon(c,path+"/coordinates"));
        else if(g.type=="MultiPolygon") {std::size_t i=0;for(const auto& p:array(c,path+"/coordinates"))g.polygons.push_back(polygon(p,path+"/coordinates/"+std::to_string(i++)));}
        else if(territory)throw std::invalid_argument("INVALID_GEOMETRY: "+path+" territorial polygon required");
        else if(g.type=="Point")g.points.push_back(point(c,path+"/coordinates"));
        else if(g.type=="MultiPoint")g.points=line(c,path+"/coordinates");
        else if(g.type=="LineString")g.lines.push_back(line(c,path+"/coordinates"));
        else if(g.type=="MultiLineString") {std::size_t i=0;for(const auto& l:array(c,path+"/coordinates"))g.lines.push_back(line(l,path+"/coordinates/"+std::to_string(i++)));}
        else throw std::invalid_argument("INVALID_GEOMETRY: "+path+" unsupported kind");
        return g;
    }
    Validity validity(const V& props,const std::string& path) {
        Validity v;auto from=text(at(props,"validFrom")),to=text(at(props,"validTo"));
        if(!from.empty())v.from=from;if(!to.empty())v.to=to;
        try {temporalBounds(v);}catch(const std::exception& e){throw std::invalid_argument(std::string(e.what())+" at "+path);}
        return v;
    }
    std::string id(const V& row,const std::string& path,bool requireUuid) {
        auto value=text(at(row,"id"));require(!value.empty(),"INVALID_ID: "+path+"/id is empty");
        if(requireUuid)require(uuid(value),"INVALID_ID: "+path+"/id must be a UUID");return value;
    }
    std::uint32_t color(const V& raw,const std::string& path,std::uint32_t fallback) {
        const auto s=QString::fromStdString(text(raw));
        static const QRegularExpression hex("^#[0-9a-fA-F]{6}$");
        if(!hex.match(s).hasMatch()) {
            report(path,"default",QStringLiteral("명시 색상 없음/무효: 웹 색상 어댑터의 기본색 규칙 적용. 원본 값은 archive에 보존."));return fallback;
        }
        return s.mid(1).toUInt(nullptr,16);
    }
    double opacity(const V& style) {
        if(!has(style,"opacity"))return 1;
        auto n=number(at(style,"opacity"));return std::isfinite(n)?std::clamp(n,0.,1.):1.;
    }
    void symbol(const V& props,const std::string& path,const ObjectRef& ref,bool country) {
        const auto key="flagDataUrl";
        QString state=!has(props,key)?"Default":at(props,key).kind==V::Null?"None":"Embedded";
        QVariantMap row{{"path",QString::fromStdString(pointer(path,key))},{"status","retained"},{"domain","territorial"},
            {"id",QString::fromStdString(ref.id)},{"flagState",state},
            {"message",state=="Default"?QStringLiteral("기본 국기 참조 보존 · 기본 국기 자료는 포함되지 않음"):QStringLiteral("국기 상태/원본 값 보존 · 현재 국기 표시 미지원")}};
        if(state=="Default")row["availability"]="unavailable-reference";
        output.report.push_back(row);
        if(has(props,key))preserve(pointer(path,key),at(props,key),true,{ref});
        if(country&&has(props,"capital")) {
            preserve(pointer(path,"capital"),at(props,"capital"),true,{ref});
            report(pointer(path,"capital"),"retained",QStringLiteral("수도 원본 값 보존 · 현재 표시/편집 미지원"));
        }
    }
    void unit(const V& feature,const std::string& path,bool country) {
        check();require(feature.kind==V::Object&&text(at(feature,"type"))=="Feature","INVALID_UNIT: "+path+" expected Feature");
        const auto& props=at(feature,"properties");require(props.kind==V::Object,"INVALID_UNIT: "+path+"/properties");
        TerritorialUnit u;u.id=id(feature,path,!country);
        require(ids.insert(u.id).second,"DUPLICATE_ID: "+path+"/id "+u.id);
        auto ref=territorialRef(u.id);
        const auto& overrides=at(at(root,"countryOverrides"),u.id);
        if(country) {
            require(!text(at(props,"name")).empty(),"INVALID_UNIT: "+path+"/properties/name is empty");
            u.kind=UnitKind::Country;u.baseName=text(at(props,"name"));u.name=text(at(overrides,"name"));u.nameExplicit=has(overrides,"name");
            u.notes=text(at(overrides,"notes"));u.locked=isTrue(at(overrides,"locked"));++output.countries;
        } else {
            require(number(at(props,"schemaVersion"))==2,"UNSUPPORTED_VERSION: "+path+"/properties/schemaVersion");
            auto type=QString::fromStdString(text(at(props,"unitType"))).toLower().toStdString();
            require(type=="subunit"||type=="region","INVALID_UNIT: "+path+" countries belong in countriesData");
            u.kind=type=="subunit"?UnitKind::Subunit:UnitKind::Region;
            u.name=text(at(props,"name"));u.notes=text(at(props,"notes"));u.locked=isTrue(at(props,"locked"));
            u.coverageMode=text(at(props,"coverageMode"));require(u.coverageMode=="partition"||u.coverageMode=="explicit","INVALID_UNIT: "+path+" coverageMode");
            if(u.kind==UnitKind::Subunit)++output.subunits;else ++output.regions;
            TerritorialRelation r;r.id="web-base-"+u.id;r.unit=ref;
            auto parent=text(at(props,"parentId")),sovereign=text(at(props,"sovereignId"));
            if(!parent.empty())r.parent=territorialRef(parent);if(!sovereign.empty())r.sovereign=territorialRef(sovereign);
            output.document.relations.push_back(std::move(r));
        }
        u.validity=validity(props,path+"/properties");
        u.geometry={"web-geometry-"+u.id,1};
        try {output.document.geometries.insert(u.geometry,geometry(at(feature,"geometry"),path+"/geometry",true));}
        catch(const std::exception& e){throw std::invalid_argument(std::string(e.what())+" at "+path);}
        output.document.units.push_back(u);
        const std::string group=country?"countries":u.kind==UnitKind::Subunit?"subunits":"regions";
        const auto& style=at(props,"style");
        // Source preference theme is not serialized. Qt's initial light map uses
        // the source light-theme #cccccc, while explicit colors remain explicit.
        output.document.presentation.objectStyles[ref]={color(country?at(overrides,"color"):at(style,"color"),country?"/countryOverrides/"+u.id+"/color":path+"/properties/style/color",country?0xcccccc:0x8c68d8),1};
        const auto rawColor=QString::fromStdString(text(country?at(overrides,"color"):at(style,"color")));
        auto& mappedStyle=output.document.presentation.objectStyles.at(ref);
        mappedStyle.explicitColor=QRegularExpression("^#[0-9a-fA-F]{6}$").match(rawColor).hasMatch();
        if(!mappedStyle.explicitColor)mappedStyle.color=0;
        report(path,country?"mapped":"retained",country?QStringLiteral("국가 도형·ID·이름·메모·명시 색상 매핑; 웹 표시 설정은 별도 보존"):QStringLiteral("영토 도형·관계·기간·속성 매핑 · 생성/소속 변경은 후속 단계"));
        extras(feature,path,{"type","id","properties","geometry"},{ref});
        extras(at(feature,"geometry"),path+"/geometry",{"type","coordinates"},{ref});
        if(country) {
            extras(props,path+"/properties",{"name","validFrom","validTo"},{ref});
            extras(overrides,"/countryOverrides/"+u.id,{"name","notes","color","locked","capital","flagDataUrl"},{ref});
            symbol(overrides,"/countryOverrides/"+u.id,ref,true);
        } else {
            extras(props,path+"/properties",{"schemaVersion","unitType","name","notes","locked","coverageMode","validFrom","validTo","parentId","sovereignId","style","metadata","sourceFolderId","sourceLibraryId","sourceGeometryVersion"},{ref});
            for(auto key:{"sourceFolderId","sourceLibraryId","sourceGeometryVersion"})if(has(props,key))preserve(pointer(path+"/properties",key),at(props,key),at(props,key).kind==V::String||at(props,key).kind==V::Null,{ref});
            if(has(props,"style"))extras(style,path+"/properties/style",{"color"},{ref});
            const auto& meta=at(props,"metadata");symbol(meta,path+"/properties/metadata",ref,false);
            extras(meta,path+"/properties/metadata",{"flagDataUrl"},{ref});
        }
    }
    void relations() {
        std::set<std::string> seen;std::size_t i=0;
        for(const auto& row:optionalArray(at(root,"territorialRelations"),"/territorialRelations")) {
            check();auto path="/territorialRelations/"+std::to_string(i++);
            TerritorialRelation r;r.id=id(row,path,true);require(seen.insert(r.id).second,"DUPLICATE_ID: "+path);
            require(number(at(row,"schemaVersion"))==1,"UNSUPPORTED_VERSION: "+path+"/schemaVersion");
            r.unit=territorialRef(text(at(row,"unitId")));r.dated=true;r.validity=validity(row,path);
            auto parent=text(at(row,"parentId")),sovereign=text(at(row,"sovereignId"));
            if(!parent.empty())r.parent=territorialRef(parent);if(!sovereign.empty())r.sovereign=territorialRef(sovereign);
            output.document.relations.push_back(r);
            extras(row,path,{"id","schemaVersion","unitId","parentId","sovereignId","validFrom","validTo"},{r.unit});
        }
    }
    void retainedValidation() {
        // Validate supported envelopes/references even where rendering/editing is
        // deferred. Do not turn preserved malformed known references into success.
        std::map<std::string,const V*> layers;
        std::size_t i=0;
        for(const auto& row:optionalArray(at(root,"distributionLayers"),"/distributionLayers")) {
            auto path="/distributionLayers/"+std::to_string(i++);auto key=id(row,path,true);
            require(layers.emplace(key,&row).second,"DUPLICATE_ID: "+path);
            require(number(at(row,"schemaVersion"))==2,"UNSUPPORTED_VERSION: "+path);
            auto type=text(at(row,"type"));require(type=="language"||type=="ethnicity"||type=="religion","INVALID_DISTRIBUTION: "+path);
            validity(row,path);
        }
        for(const auto& [key,row]:layers) {
            auto parent=text(at(*row,"parentId"));std::set<std::string> seen{key};
            while(!parent.empty()) {
                require(layers.count(parent),"DANGLING_REF: /distributionLayers parent "+parent);
                require(text(at(*layers.at(parent),"type"))==text(at(*row,"type")),"INVALID_DISTRIBUTION: parent type");
                require(seen.insert(parent).second,"RELATION_CYCLE: /distributionLayers");parent=text(at(*layers.at(parent),"parentId"));
            }
        }
        std::set<std::string> seen;i=0;
        for(const auto& row:optionalArray(at(root,"distributionEntries"),"/distributionEntries")) {
            check();auto path="/distributionEntries/"+std::to_string(i++);auto key=id(row,path,true);
            require(seen.insert(key).second,"DUPLICATE_ID: "+path);require(number(at(row,"schemaVersion"))==2,"UNSUPPORTED_VERSION: "+path);
            require(layers.count(text(at(row,"layerId"))),"DANGLING_REF: "+path+"/layerId");
            auto mode=text(at(row,"mode"));require(mode=="geometry"||mode=="territorial","INVALID_DISTRIBUTION: "+path+"/mode");
            if(mode=="territorial")require(ids.count(text(at(row,"territorialUnitId"))),"DANGLING_REF: "+path+"/territorialUnitId");
            else {GeometryStore validation;validation.insert({"validate",1},geometry(at(row,"geometry"),path+"/geometry",true));}
            double share=at(row,"share").kind==V::Null?100:number(at(row,"share"));
            require(std::isfinite(share)&&share>=0&&share<=100,"INVALID_DISTRIBUTION: "+path+"/share");validity(row,path);
            // No sum-of-shares constraint: 60 + 70 is valid in the original.
        }
        for(auto domain:{"genericFeatures","hydroEdits","labels"}) {
            seen.clear();i=0;
            for(const auto& row:optionalArray(at(root,domain),std::string("/")+domain)) {
                check();auto path=std::string("/")+domain+"/"+std::to_string(i++);auto key=id(row,path,true);
                require(seen.insert(key).second,"DUPLICATE_ID: "+path);
                if(std::string(domain)!="labels") {
                    const auto& p=at(row,"properties");require(number(at(p,std::string(domain)=="hydroEdits"?"pandolab_schema_version":"schemaVersion"))==(std::string(domain)=="hydroEdits"?1:2),"UNSUPPORTED_VERSION: "+path);
                    if(std::string(domain)=="genericFeatures") {
                        const auto& source=at(p,"source");
                        require(source.kind==V::Object&&number(at(source,"schemaVersion"))==1,"INVALID_SOURCE: "+path+"/properties/source/schemaVersion");
                        require(std::set<std::string>{"user","builtin","library","gis","legacy","plugin","unsupported"}.count(text(at(source,"kind"))),"INVALID_SOURCE: "+path+"/properties/source/kind");
                        for(auto key:{"dataset","sourceId","sourceFormat","sourceType","version","importedAt"})
                            require(at(source,key).kind==V::Null||at(source,key).kind==V::String,"INVALID_SOURCE: "+path+"/properties/source/"+key);
                        require(at(source,"details").kind==V::Null||at(source,"details").kind==V::Object,"INVALID_SOURCE: "+path+"/properties/source/details");
                    }
                    GeometryStore validation;validation.insert({"validate",1},geometry(at(row,"geometry"),path+"/geometry",false));
                }
            }
        }
    }
    void roots() {
        const std::set<std::string> mapped={"countriesData","countryOverrides","territorialUnits","territorialRelations"};
        const std::set<std::string> provenance={"format","schemaVersion","version","savedAt","landObjectModel","territorialModel","distributionModel"};
        const std::set<std::string> external={"sourceInfo","baseDataset","physicalSourceInfo"};
        for(const auto& [k,v]:root.object) {
            auto p=pointer("",k);
            if(mapped.count(k))report(p,"mapped",QStringLiteral("대응 필드 매핑 · 미지원 하위 필드는 별도 보존"));
            else if(provenance.count(k)) {preserve(p,v,true,{},true);report(p,"archived",QStringLiteral("원본 형식·버전·모델 계약 보존"));}
            else if(external.count(k)) {
                preserve(p,v,propertypreservation::sourceInfo(k,v));report(p,"unavailable-reference",QStringLiteral("출처/외부 자료 참조 보존 · 자료 자체를 포함/다운로드/대체한 것이 아님"));
            } else {
                std::vector<ObjectRef> deps;
                if(k=="distributionEntries")for(const auto& row:optionalArray(v,p)) {auto id=text(at(row,"territorialUnitId"));if(ids.count(id))deps.push_back(territorialRef(id));}
                const bool recognized=propertypreservation::recognized(k,v);
                if(recognized)for(const auto& id:ids)deps.push_back(territorialRef(id));
                std::sort(deps.begin(),deps.end());deps.erase(std::unique(deps.begin(),deps.end()),deps.end());
                // Recognized presentation can coexist with metadata edits. Its
                // structural dependencies remain protected; unknown substructure
                // still blocks non-text edits across the document.
                preserve(p,v,recognized,std::move(deps));
                report(p,"retained",QStringLiteral("원본 값·타입·순서 보존 · 웹의 해당 표시/편집 기능은 아직 미지원"),v.kind==V::Array?static_cast<int>(v.array.size()):0);
            }
        }
        extras(at(root,"countriesData"),"/countriesData",{"type","features"});
        extras(at(root,"landObjectModel"),"/landObjectModel",{"schemaVersion","coastlineAuthority","purpose","directCreation","sourceProvenanceSchemaVersion","canonicalProperties"});
        extras(at(root,"territorialModel"),"/territorialModel",{"schemaVersion","coastlineAuthority","countryStorage","types","coverageModes"});
        extras(at(root,"distributionModel"),"/distributionModel",{"schemaVersion","types","sourceModes","shareRange","sharesAreIndependent"});
        report("","notice",QStringLiteral("국가·하위단위·지방의 기본 속성을 편집할 수 있습니다. 국기·지명·수계·분포·혼합 등의 표시/편집은 후속 범위이며, 알 수 없는 데이터는 관련 변경을 제한할 수 있습니다."));
    }
};
}
Candidate prepare(const QByteArray& bytes,const std::function<bool()>& cancelled) {
    if(cancelled&&cancelled())throw std::invalid_argument("CANCELLED: 가져오기를 취소했습니다.");
    require(bytes.size()<=storageLimit,"LIMIT_EXCEEDED: web input exceeds current 64 MiB storage boundary");
    auto migration=migrate(bytes);auto original=losslessjson::parse(bytes),root=losslessjson::parse(migration.normalized);
    Builder b{{},root,cancelled,{}};b.output.sourceFormat=migration.sourceFormat;b.output.sourceSchema=migration.sourceSchema;
    b.output.sourceHash=digest(original.encode());b.output.document.documentId="web-"+b.output.sourceHash.toStdString();
    b.check();
    for(auto field:{"countryOverrides","sourceInfo","labelSettings","distributionSettings","physicalSettings","layerVisibility","itemVisibility","layerPresentation"}) {
        const auto& value=at(root,field);
        require(value.kind==V::Null||value.kind==V::Object,std::string("INVALID_DOCUMENT: /")+field+" expected object");
    }
    for(auto field:{"styles","objectStyles"}) {
        const auto& value=at(at(root,"layerPresentation"),field);
        require(value.kind==V::Null||value.kind==V::Object,std::string("INVALID_DOCUMENT: /layerPresentation/")+field+" expected object");
        for(const auto& [key,style]:value.object)require(style.kind==V::Object,"INVALID_DOCUMENT: /layerPresentation/"+std::string(field)+"/"+key);
    }
    for(const auto& [model,version]:std::initializer_list<std::pair<const char*,int>>{{"landObjectModel",2},{"territorialModel",2},{"distributionModel",2},{"layerPresentation",3}})
        require(number(at(at(root,model),"schemaVersion"))==version,std::string("UNSUPPORTED_VERSION: /")+model+"/schemaVersion");
    const auto& land=at(root,"landObjectModel");
    require(text(at(land,"purpose"))=="lossless-fallback"&&isFalse(at(land,"directCreation"))&&number(at(land,"sourceProvenanceSchemaVersion"))==1,"INVALID_DOCUMENT: /landObjectModel fallback contract");
    const auto& collection=at(root,"countriesData");
    require(collection.kind==V::Object&&text(at(collection,"type"))=="FeatureCollection","INVALID_DOCUMENT: /countriesData expected FeatureCollection");
    // Semantic base groups are not fabricated user layer memberships from web
    // folders. They are isolated adapters; original presentation stays retained.
    const auto& visibility=at(root,"layerVisibility");const auto& styles=at(at(root,"layerPresentation"),"styles");
    std::size_t i=0;for(const auto& f:array(at(collection,"features"),"/countriesData/features"))b.unit(f,"/countriesData/features/"+std::to_string(i++),true);
    for(const auto& [id,override]:at(root,"countryOverrides").object) {
        require(b.ids.count(id),"DANGLING_REF: /countryOverrides/"+id);
        require(override.kind==V::Object,"INVALID_DOCUMENT: /countryOverrides/"+id);
        for(const auto& [key,value]:override.object) {
            if(key=="locked")require(isTrue(value),"INVALID_OVERRIDE: /countryOverrides/"+id+"/locked");
            else if(key=="flagDataUrl"&&value.kind==V::Null)continue;
            else if(std::set<std::string>{"name","color","capital","notes","flagDataUrl"}.count(key))
                require(value.kind==V::String&&!text(value).empty()&&text(value)==value.string,"INVALID_OVERRIDE: /countryOverrides/"+id+"/"+key);
            // Unknown override keys are retained with a dependency barrier, not pruned.
        }
    }
    i=0;for(const auto& f:optionalArray(at(root,"territorialUnits"),"/territorialUnits"))b.unit(f,"/territorialUnits/"+std::to_string(i++),false);
    b.relations();b.retainedValidation();b.roots();
    presentationmigration::promote(b.output.document);
    b.preserve("",original,true,{},true);
    if(migration.sourceSchema<5)b.report("/schemaVersion","migrated",QStringLiteral("웹 schema %1 → 5 변환. 변환 전 원본은 migrationArchive로 보존.").arg(migration.sourceSchema));
    b.check();
    Project validated;validated.replace(b.output.document);
    auto encoded=projectcodec::encode(validated);
    require(encoded.size()<=storageLimit,"LIMIT_EXCEEDED: retained Qt output exceeds 64 MiB; nothing imported");
    b.output.candidateHash=digest(encoded);b.check();return std::move(b.output);
}
}
