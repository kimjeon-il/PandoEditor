#include "projectcodec.h"
#include "projectgeopackage.h"
#include "gisdocumentexport.h"
#include "gisgeopackage.h"
#include "losslessjson.h"
#include <pandoeditor/project.h>
#include <pandoeditor/map/labelengine.h>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <type_traits>
using namespace pandoeditor;
namespace {
using V=losslessjson::Value;
const std::string copied="builtin:place:synthetic:1";
const std::string userId="11111111-1111-4111-8111-111111111111";
void require(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
template<class F>void rejects(F action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}require(rejected,"invalid typed provenance must reject");}
// Runtime RED remains executable before the typed field and bucket APIs exist.
// The fallback exposes old behavior, never supplies the requested semantics.
template<class T,class=void>struct Identity {
    static void set(T&,std::string){}
    static std::optional<std::string> get(const T&){return {};}
};
template<class T>struct Identity<T,std::void_t<decltype(std::declval<T>().sourcePlaceId)>> {
    static void set(T& value,std::string id){value.sourcePlaceId=std::move(id);}
    static std::optional<std::string> get(const T& value){return value.sourcePlaceId;}
};
template<class T,class=void>struct Buckets {
    static void set(T& value,std::vector<MapLabelSource> sources,std::uint64_t revision){value.setSources(std::move(sources),revision);}
    static void suppress(T&,std::set<std::string>){}
};
template<class T>struct Buckets<T,std::void_t<decltype(std::declval<T>().setBuiltinSources(std::vector<MapLabelSource>{},1)),
    decltype(std::declval<T>().setBuiltinSuppressedIds(std::set<std::string>{}))>> {
    static void set(T& value,std::vector<MapLabelSource> sources,std::uint64_t revision){value.setBuiltinSources(std::move(sources),revision);}
    static void suppress(T& value,std::set<std::string> ids){value.setBuiltinSuppressedIds(std::move(ids));}
};
template<class T>auto copiedIdsImpl(const T& document,int)->decltype(copiedPlaceSourceIds(document)) {
    return copiedPlaceSourceIds(document);
}
template<class T>std::set<std::string> copiedIdsImpl(const T& document,long) {
    std::set<std::string> result;for(const auto& label:document.labels)if(const auto id=Identity<PlaceLabel>::get(label);id&&!id->empty())result.insert(*id);return result;
}
std::set<std::string> copiedIds(const ProjectDocument& document){return copiedIdsImpl(document,0);}
Geometry point(){return {"Point",{{0,0}},{},{}};}
ProjectDocument fixture(bool provenance=true) {
    ProjectDocument document;document.documentId="place-provenance-tests";
    GeometryRef ref{"copy-point",1};document.geometries.insert(ref,point());
    PlaceLabel label;label.id=userId;label.name="서울";label.kind="capital";label.geometry=ref;
    if(provenance)Identity<PlaceLabel>::set(label,copied);document.labels.push_back(label);return document;
}
MapViewState view(){MapViewState value;value.viewportWidth=800;value.viewportHeight=600;value.scale=8*180/3.14159265358979323846;value.translateX=400;value.translateY=300;return value;}
MapLabelLayoutOptions options(){MapLabelLayoutOptions value;value.viewportWidth=800;value.viewportHeight=600;value.zoom=2;return value;}
MapLabelSource label(std::string id,double longitude=0,double priority=1,bool builtin=false) {
    MapLabelSource result;result.ref={builtin?"placeBuiltin":"label",std::move(id)};result.text=result.ref.id;
    result.geographic={longitude,0};result.width=80;result.height=20;result.priority=priority;return result;
}
void apply(Project& project,ContentEdit edit) {
    CommandArguments args;args.action=std::move(edit);auto prepared=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"content.edit",args));
    require(prepared.preview.has_value(),prepared.detail.c_str());require(CommandProcessor::confirm(project,*prepared.preview).changed(),"content change must commit");
}
void write(const QString& path,const QByteArray& bytes){QFile file(path);require(file.open(QIODevice::WriteOnly),"temporary file opens");require(file.write(bytes)==bytes.size(),"temporary file written");}
}
int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);int passed=0,failed=0;
    const auto test=[&](const char* name,const std::function<void()>& body){try{body();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}};
    // Fixed ebc copy/suppression/storage literals, independently read from Web:
    // app-generic-commands.js310; app-territorial-labels.js224; gis-io.js1015.
    test("native model6 stores typed provenance and survives reopen",[]{Project project;project.replace(fixture());const auto encoded=projectcodec::encode(project);
        const auto native=losslessjson::parse(encoded);require(native.object.at("content").object.at("labels").array[0].object.at("sourcePlaceId").string==copied,"native explicit sourcePlaceId");
        const auto restored=projectcodec::decode(encoded);require(Identity<PlaceLabel>::get(restored.labels[0])==copied,"native typed provenance restored");});
    test("native reader accepts new optional provenance without schema migration",[]{Project project;project.replace(fixture(false));auto native=losslessjson::parse(projectcodec::encode(project));
        native.object.at("content").object.at("labels").array[0].object["sourcePlaceId"]=V::str(copied);
        require(Identity<PlaceLabel>::get(projectcodec::decode(native.encode()).labels[0])==copied,"model6 optional field decoded");});
    test("older labels omit provenance and stay ordinary editable labels",[]{Project project;project.replace(fixture(false));auto native=losslessjson::parse(projectcodec::encode(project));
        native.object.at("content").object.at("labels").array[0].object.erase("sourcePlaceId");
        const auto restored=projectcodec::decode(native.encode());require(!Identity<PlaceLabel>::get(restored.labels[0]),"absence stays absent");});
    test("Web10 output uses exact sourcePlaceId camelcase field",[]{Project project;project.replace(fixture());const auto bytes=projectcodec::encodeWeb(project.snapshot());
        const auto web=losslessjson::parse(bytes);require(web.object.at("labels").array[0].object.at("sourcePlaceId").string==copied,"Web sourcePlaceId retained");
        require(Identity<PlaceLabel>::get(projectcodec::decodeWeb(bytes).labels[0])==copied,"Web typed roundtrip");});
    test("fixed Web10 input copy provenance is supported rather than an unknown field",[]{Project project;project.replace(fixture(false));auto web=losslessjson::parse(projectcodec::encodeWeb(project.snapshot()));
        web.object.at("labels").array[0].object["sourcePlaceId"]=V::str(copied);
        require(Identity<PlaceLabel>::get(projectcodec::decodeWeb(web.encode()).labels[0])==copied,"latest Web copy decodes");});
    test("nonstring provenance cannot pass native or Web typed boundaries",[]{Project project;project.replace(fixture(false));auto native=losslessjson::parse(projectcodec::encode(project));
        native.object.at("content").object.at("labels").array[0].object["sourcePlaceId"]=V::num(1);rejects([&]{projectcodec::decode(native.encode());});
        auto web=losslessjson::parse(projectcodec::encodeWeb(project.snapshot()));web.object.at("labels").array[0].object["sourcePlaceId"]=V::boolean(true);rejects([&]{projectcodec::decodeWeb(web.encode());});});
    test("provenance-only mutation is content and Undo restores exact source identity",[]{Project project;project.replace(fixture(false));project.markSaved();const auto before=project.document();
        auto changed=project.document().labels[0];Identity<PlaceLabel>::set(changed,copied);
        auto after=before;after.labels[0]=changed;require(!semanticallyEqual(before,after),"source identity contributes semantic equality");
        apply(project,ContentEdit{{"label",userId},changed,{},false});require(project.dirty()&&copiedIds(project.document()).count(copied),"typed change dirties and suppresses");
        require(project.undo()&&copiedIds(project.document()).empty()&&!project.dirty(),"Undo provenance restores ordinary label");
        require(project.redo()&&copiedIds(project.document()).count(copied),"Redo source identity restored");});
    test("copy suppression derives from current document and deletion Undo",[]{Project project;ProjectDocument empty;empty.documentId="copy-undo";project.replace(empty);project.markSaved();
        auto copy=fixture().labels[0];apply(project,ContentEdit{{"label",userId},copy,std::make_pair(copy.geometry,point()),true});
        require(copiedIds(project.document())==std::set<std::string>{copied},"one independent user copy suppresses one source");
        require(project.undo()&&copiedIds(project.document()).empty(),"Undo copy restores builtin without stored suppression state");
        require(project.redo()&&copiedIds(project.document()).count(copied),"Redo copy hides source");
        apply(project,ContentEdit{{"label",userId},{},{},false});require(copiedIds(project.document()).empty(),"delete copy unhides source");
        require(project.undo()&&copiedIds(project.document()).count(copied),"Undo deletion restores suppression");});
    test("GIS GeoJSON uses exact fixed Web source_place_id alias",[]{Project project;project.replace(fixture());const auto exported=exportGisDocumentLayer(project.document(),"labels");
        const auto parsed=parseGisGeoJson(exportGisGeoJson(exported));require(losslessjson::parse(QByteArray::fromStdString(parsed.features[0].propertiesJson)).object.at("source_place_id").string==copied,"GIS source_place_id roundtrip");});
    test("GIS GeoPackage vector retains source_place_id provenance",[]{Project project;project.replace(fixture());QTemporaryDir temporary;require(temporary.isValid(),"temporary directory");
        const auto path=temporary.filePath("places.gpkg");write(path,exportGisGeoPackage(project.document(),{"labels"}));const auto archive=readGisGeoPackage(path);
        require(archive.layers.size()==1,"one GIS places layer");require(losslessjson::parse(QByteArray::fromStdString(archive.layers[0].collection.features[0].propertiesJson)).object.at("source_place_id").string==copied,"GIS vector provenance retained");});
    test("native project GeoPackage validates and reopens copy provenance",[]{Project project;project.replace(fixture());QTemporaryDir temporary;require(temporary.isValid(),"temporary directory");
        const auto path=temporary.filePath("project.gpkg");write(path,exportProjectGeoPackage(project));
        require(Identity<PlaceLabel>::get(projectcodec::decode(readProjectGeoPackage(path)).labels[0])==copied,"project authoritative and vectors preserve copy source");});
    test("builtin viewport bucket never replaces or rebuilds document sources",[]{MapLabelEngine engine;engine.setSources({label("document",-30,10)},11);const auto rebuilds=engine.stats().sourceRebuilds;
        Buckets<MapLabelEngine>::set(engine,{label(copied,0,20,true)},1);
        require(engine.sources().size()==1&&engine.sources()[0].ref.id=="document"&&engine.stats().sourceRevision==11&&engine.stats().sourceRebuilds==rebuilds,"document source index untouched");
        engine.layout(view(),options());require(engine.placedRefs().count({"label","document"})&&engine.placedRefs().count({"placeBuiltin",copied}),"shared engine renders both buckets");
        Buckets<MapLabelEngine>::set(engine,{label("builtin:place:synthetic:2",30,20,true)},2);
        engine.layout(view(),options());require(engine.stats().sourceRebuilds==rebuilds&&engine.sources()[0].ref.id=="document","new viewport cannot rebuild document labels");});
    test("unified document and builtin collision priority is deterministic",[]{MapLabelEngine engine;engine.setSources({label("document",0,100)},1);
        Buckets<MapLabelEngine>::set(engine,{label(copied,0,10,true)},1);engine.layout(view(),options());
        require(engine.placedRefs()==std::set<ObjectRef>{{"label","document"}},"higher document priority wins one shared collision pass");
        engine.layout(view(),options(),{{"placeBuiltin",copied}});require(engine.placedRefs()==std::set<ObjectRef>{{"placeBuiltin",copied}},"selected builtin forced priority shares collision pass");});
    test("builtin suppression survives selection and viewport updates then Undo restores source",[]{MapLabelEngine engine;engine.setSources({label("document",-30,1)},1);
        Buckets<MapLabelEngine>::set(engine,{label(copied,0,20,true)},1);Buckets<MapLabelEngine>::suppress(engine,{copied});
        engine.layout(view(),options(),{{"placeBuiltin",copied}});require(!engine.placedRefs().count({"placeBuiltin",copied}),"copied source suppressed even selected");
        Buckets<MapLabelEngine>::set(engine,{label(copied,0,20,true)},2);engine.layout(view(),options());require(!engine.placedRefs().count({"placeBuiltin",copied}),"viewport requery preserves derived suppression");
        Buckets<MapLabelEngine>::suppress(engine,{});engine.layout(view(),options());require(engine.placedRefs().count({"placeBuiltin",copied}),"empty current-document set restores source");});
    test("ordinary source rebuild preserves transient bucket and clear removes both",[]{MapLabelEngine engine;engine.setSources({label("old",-30)},1);Buckets<MapLabelEngine>::set(engine,{label(copied,0,10,true)},1);
        engine.setSources({label("new",30)},2);engine.layout(view(),options());require(engine.placedRefs().count({"placeBuiltin",copied})&&engine.placedRefs().count({"label","new"}),"document rebuild retains builtin snapshot");
        engine.clear();engine.layout(view(),options());require(engine.placements().empty(),"scope clear removes both owners");});
    test("builtin bucket participates in bounded candidate work",[]{MapLabelEngine engine;engine.setSources({label("document",-30)},1);std::vector<MapLabelSource> sources;
        for(int i=0;i<3000;++i)sources.push_back(label("builtin:place:synthetic:"+std::to_string(i),0,double(i),true));Buckets<MapLabelEngine>::set(engine,std::move(sources),1);
        auto limited=options();limited.maxCandidates=32;limited.maxPlaced=10;engine.layout(view(),limited);require(engine.stats().candidatesExamined<=32&&engine.placements().size()<=10,"one total candidate budget across buckets");});
    test("canonical builtin dateline record draws in the native wrapped viewport",[]{MapLabelEngine engine;engine.setSources({},1);
        Buckets<MapLabelEngine>::set(engine,{label(copied,-179,10,true)},1);auto wrapped=view();wrapped.centerLongitude=180;
        engine.layout(wrapped,options());require(engine.placements().size()==1&&engine.placements()[0].x>400&&engine.placements()[0].x<420,"canonical -179 draws beside +180");
        wrapped=view();wrapped.translateX-=180*8;engine.layout(wrapped,options());
        require(engine.placements().size()==1&&engine.placements()[0].x>400&&engine.placements()[0].x<420,"native translation pan has same wrapped semantics");});
    test("builtin globe layout respects hemisphere and rotation",[]{MapLabelEngine engine;engine.setSources({},1);
        Buckets<MapLabelEngine>::set(engine,{label(copied,0,10,true),label("builtin:place:synthetic:back",180,20,true)},1);
        auto globe=view();globe.mode=ProjectionMode::Globe;globe.scale=250;engine.layout(globe,options());
        require(engine.placedRefs()==std::set<ObjectRef>{{"placeBuiltin",copied}},"only front builtin visible");
        globe.centerLongitude=180;engine.layout(globe,options());
        require(engine.placedRefs()==std::set<ObjectRef>{{"placeBuiltin","builtin:place:synthetic:back"}},"rotated opposite builtin appears");});
    std::cout<<passed<<" passed; "<<failed<<" failed; skip=0\n";return failed?1:0;
}
