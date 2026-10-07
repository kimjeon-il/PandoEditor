#include "placeruntimeprovider.h"
#include <pandoeditor/map/labelengine.h>
#include <pandoeditor/project.h>
#include <QGuiApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QElapsedTimer>
#include <QThread>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace pandoeditor;
namespace {
void require(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
QByteArray read(const QString& path){QFile f(path);require(f.open(QIODevice::ReadOnly),"open pinned fixture");return f.readAll();}
QJsonArray ids(const std::vector<PlaceRecord>& rows){QJsonArray a;for(const auto& r:rows)a.append(r.id);return a;}
QJsonArray records(const std::vector<PlaceRecord>& rows){QJsonArray a;for(const auto& r:rows)a.append(QJsonObject{{"id",r.id},{"source",r.source},{"sourceId",r.sourceId},{"name",r.name},{"kind",r.kind},{"coordinates",QJsonArray{r.coordinates.x,r.coordinates.y}},{"countryCode",r.countryCode},{"featureCode",r.featureCode},{"population",r.population},{"priority",r.priority},{"minZoom",r.minZoom},{"notes",""}});return a;}
PlaceViewport view(double zoom=3,double width=400,double dpr=1,double bottom=0){PlaceViewport v;v.zoom=zoom;v.view.viewportWidth=width;v.view.viewportHeight=300;v.view.scale=100;v.view.translateX=width/2;v.view.translateY=150;v.view.devicePixelRatio=dpr;v.safeBottom=bottom;return v;}
void wait(const std::function<bool()>& done){QElapsedTimer t;t.start();while(!done()&&t.elapsed()<10000){QCoreApplication::processEvents();QThread::msleep(1);}require(done(),"provider completion timeout");}
MapLabelSource label(const PlaceRecord& r){MapLabelSource s;s.ref={"placeBuiltin",r.id.toStdString()};s.text=r.name.toStdString();s.geographic=r.coordinates;s.width=std::max(22,int(r.name.toUcs4().size())*9+16);s.height=19;const auto p=automaticLabelSettings(r.kind.toStdString());s.priority=p.priority.value_or(40);s.minZoom=std::max(r.minZoom,p.minZoom.value_or(0));s.maxZoom=p.maxZoom.value_or(1e100);s.collisionGroup=p.collisionGroup;return s;}
QJsonArray placed(const std::vector<MapLabelPlacement>& rows){QJsonArray a;for(const auto& r:rows)a.append(QString::fromStdString(r.ref.id));return a;}
}
int main(int argc,char** argv){QGuiApplication application(argc,argv);try{
    require(argc==3,"usage: place_parity_probe FIXTURE_ROOT OUTPUT_JSON");const QString root=QString::fromLocal8Bit(argv[1]);QJsonObject cases;
    const auto open=[&](const QString& name){QString error;auto s=PlaceRuntimeStore::open(QDir(root).filePath("synthetic/"+name+"/manifest.json"),error);require(bool(s),qPrintable(error));return s;};
    const auto basicRows=PlaceRuntimeStore::decodeTile(read(QDir(root).filePath("synthetic/basic/0.bin")));
    cases.insert("decode-basic",records(basicRows));
    struct Camera {const char* name;QString fixture;PlaceViewport v;};
    auto pan=view();pan.view.centerLongitude=150;pan.view.translateX+=100*150*3.14159265358979323846/180;
    auto dateline=view();dateline.view.centerLongitude=180;
    auto globe=view();globe.view.mode=ProjectionMode::Globe;
    auto back=globe;back.view.rotationLongitude=180;
    const std::vector<Camera> cameras={{"flat-basic","basic",view()},{"flat-zoom1","basic",view(1)},{"flat-pan","basic",pan},{"flat-dateline","basic",dateline},{"globe-front","basic",globe},{"globe-back","basic",back},{"dpr1","basic",view(3,400,1)},{"dpr3","basic",view(3,400,3)},{"mobile-layout","basic",view(3,400,3,96)},{"desktop-layout","basic",view(3,800,3,32)},{"safe-bottom","basic",view(3,400,1,160)},{"dense-limit","dense",view()},{"overscan","overscan",view()}};
    for(const auto& c:cameras){const auto result=open(c.fixture)->queryViewport(c.v);cases.insert(QString("viewport-")+c.name,QJsonObject{{"records",records(result.records)},{"tileCount",int(result.tileCount)}});}
    const QStringList queries={"",QString::fromUtf8("서"),QString::fromUtf8("  서울  "),QString::fromUtf8("서울 도시"),QString::fromUtf8("\xEF\xBB\xBF서울\xC2\xA0"),"missing"};
    for(int i=0;i<queries.size();++i){const auto result=open("basic")->search(queries[i]);cases.insert("search-"+QString::number(i),QJsonObject{{"normalized",PlaceRuntimeStore::normalizeQuery(queries[i])},{"records",records(result.records)},{"truncated",result.truncated}});}
    {QString error;auto s=PlaceRuntimeStore::open(QDir(root).filePath("assets/data/places/manifest.json"),error);require(bool(s),"empty production source opens");cases.insert("production-empty",QJsonObject{{"revision",s->revision()},{"ids",ids(s->queryViewport(view()).records)},{"sourceRecordCountObserved",bool(s->stats().sourceRecordCount)}});}
    {auto s=open("basic");bool cancelled=false;try{s->queryViewport(view(),[]{return true;});}catch(const PlaceRuntimeCancelled&){cancelled=true;}cases.insert("store-cancel",QJsonObject{{"cancelled",cancelled},{"cachedTiles",int(s->stats().cachedTiles)}});}
    {
        PlaceRuntimeProvider p;QString error;require(p.open(QDir(root).filePath("synthetic/basic/manifest.json"),"A",error),"provider source opens");int accepted=0,searches=0;QObject::connect(&p,&PlaceRuntimeProvider::snapshotChanged,[&]{++accepted;});QObject::connect(&p,&PlaceRuntimeProvider::searchCompleted,[&]{++searches;});
        p.requestViewport(view());wait([&]{return p.stats().pendingCount==0;});require(bool(p.snapshot()),"published initial snapshot");cases.insert("runtime-ready",ids(p.snapshot()->records));
        auto previous=p.snapshot();p.beginInteraction();const bool deferred=!p.requestViewport(view(1));cases.insert("runtime-moving",QJsonObject{{"deferred",deferred},{"held",p.snapshot()==previous},{"moving",p.stats().moving}});
        p.settle();wait([&]{return p.stats().pendingCount==0;});cases.insert("runtime-settled",ids(p.snapshot()->records));
        previous=p.snapshot();const auto before=accepted;p.requestViewport(view());p.cancelViewport();wait([&]{return p.stats().pendingCount==0;});cases.insert("runtime-cancel",QJsonObject{{"held",p.snapshot()==previous},{"noPublication",accepted==before}});
        p.requestViewport(view());p.requestViewport(view(1));wait([&]{return p.stats().pendingCount==0;});cases.insert("runtime-latest",ids(p.snapshot()->records));
        previous=p.snapshot();p.search(QString::fromUtf8("서울"));wait([&]{return p.stats().pendingCount==0;});const QString selected="builtin:place:synthetic:city";p.setProtectedIds({selected});auto selectedRecord=p.recordById(selected);require(bool(selectedRecord),"shared search selection resolves");cases.insert("runtime-shared-selection",QJsonObject{{"viewportHeld",p.snapshot()==previous},{"selected",selectedRecord->id},{"searchIds",ids(p.searchResults())}});
        const auto generation=p.sourceIdentity()->generation;p.requestViewport(view());require(p.open(QDir(root).filePath("synthetic/basic/manifest.json"),"B",error),"replace project source");wait([&]{return p.stats().pendingCount==0;});cases.insert("runtime-revision",QJsonObject{{"advanced",p.sourceIdentity()->generation==generation+1},{"snapshotEmpty",!p.snapshot()},{"selectionEmpty",!p.recordById(selected)}});
        p.requestViewport(view());p.close("C");wait([&]{return p.stats().pendingCount==0;});cases.insert("runtime-close",QJsonObject{{"closed",!p.isOpen()},{"snapshotEmpty",!p.snapshot()},{"selectionEmpty",!p.recordById(selected)}});
    }
    QJsonArray fontGeometry;
    for(int scenario=0;scenario<3;++scenario) {
        auto v=view(3,800).view;v.viewportHeight=600;v.scale=180/3.14159265358979323846;v.translateY=300;
        const int count=scenario==1?2048:60;std::vector<MapLabelSource> rows;
        for(int i=0;i<count;++i) {
            MapLabelSource s;s.ref={"placeBuiltin",QString("row%1").arg(i,4,10,QChar('0')).toStdString()};
            s.text=s.ref.id;s.geographic={0,0};s.width=scenario==1?2000:20;s.height=19;
            s.priority=scenario==0?double(i):90;s.collisionGroup=s.ref.id;
            s.pinned=scenario==0&&i==0;rows.push_back(s);
        }
        if(scenario==1){auto lower=rows.front();lower.ref.id="lower";lower.text="lower";lower.width=20;lower.priority=1;rows.push_back(lower);}
        if(scenario==2)std::reverse(rows.begin(),rows.end());
        MapLabelEngine engine;engine.setBuiltinSources(rows,1);
        MapLabelLayoutOptions options;options.zoom=3;options.viewportWidth=800;options.viewportHeight=600;options.labelDensity=scenario==1?1:.52;
        std::set<ObjectRef> selected;if(scenario==0)selected.insert({"placeBuiltin","row0001"});
        cases.insert("quality-"+QString::number(scenario),placed(engine.layout(v,options,selected)));
    }
    for(const auto& r:basicRows){const auto canonical=label(r);QFontMetricsF fm(application.font());fontGeometry.append(QJsonObject{{"id",r.id},{"webWidth",canonical.width},{"webHeight",canonical.height},{"qtWidth",std::max(22.,fm.horizontalAdvance(r.name)+16)},{"qtHeight",std::max(19.,fm.height())},{"font",application.font().toString()}});}
    for(int scenario=0;scenario<8;++scenario){MapLabelEngine e;std::vector<MapLabelSource> builtin;for(const auto& r:basicRows)builtin.push_back(label(r));std::vector<MapLabelSource> document;std::set<ObjectRef> selected;std::set<std::string> suppressed;
        if(scenario==1)selected.insert({"placeBuiltin","builtin:place:synthetic:city"});
        if(scenario==2||scenario==3){auto s=label(basicRows.front());s.ref={scenario==2?"territorial":"label","document-label"};s.text="문서";s.width=34;s.priority=100;s.collisionGroup=scenario==2?"country":"place";document.push_back(s);}
        if(scenario==4||scenario==6)suppressed.insert("builtin:place:synthetic:capital");
        if(scenario==7)for(auto& s:builtin)s.nameVisible=false;
        e.setSources(document,17);e.setBuiltinSources(builtin,1);e.setBuiltinSuppressedIds(suppressed);MapLabelLayoutOptions o;o.zoom=3;o.viewportWidth=400;o.viewportHeight=300;o.maxCandidates=2048;o.maxPlaced=2048;
        cases.insert("labels-"+QString::number(scenario),QJsonObject{{"ids",placed(e.layout(view().view,o,selected))},{"documentSourceCount",int(e.stats().sourceCount)},{"documentSourceRevision",int(e.stats().sourceRevision)}});
    }
    {Project p;ProjectDocument d;d.documentId="P7-place-storage";p.replace(d);p.markSaved();const auto revision=p.revision();MapLabelEngine e;std::vector<MapLabelSource> s;for(const auto& r:basicRows)s.push_back(label(r));e.setBuiltinSources(s,1);
        const bool separated=p.document().labels.empty()&&p.revision()==revision&&!p.dirty()&&!p.canUndo();const auto& source=basicRows.front();PlaceLabel copy;copy.id="11111111-1111-4111-8111-111111111111";copy.name=source.name.toStdString();copy.kind=source.kind.toStdString();copy.geometry={"place-copy",1};copy.sourcePlaceId=source.id.toStdString();Geometry g{"Point",{source.coordinates},{},{}};ContentEdit edit{{"label",copy.id},copy,std::make_pair(copy.geometry,g),true};LabelSettings settings;settings.pinned=true;settings.manualPosition=source.coordinates;auto stored=automaticLabelSettings(copy.kind,settings);stored.maxZoom.reset();edit.initialLabelSettings=std::move(stored);CommandArguments a;a.action=edit;auto prepared=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"content.edit",a));if(!prepared.preview)throw std::runtime_error("copy command prepares: "+prepared.detail);require(CommandProcessor::confirm(p,*prepared.preview).changed(),"copy command commits");const bool copySuppress=copiedPlaceSourceIds(p.document()).count(source.id.toStdString());const bool pinned=p.document().presentation.webPresentation.labelSettings.at({"label",copy.id}).pinned;
        const bool undone=p.undo()&&p.document().labels.empty()&&!p.dirty();const bool redone=p.redo()&&copiedPlaceSourceIds(p.document()).count(source.id.toStdString());
        cases.insert("storage-copy",QJsonObject{{"transientSeparated",separated},{"copySourceId",source.id},{"copySuppresses",copySuppress},{"pinned",pinned},{"oneUndoRestores",undone},{"redoSuppresses",redone},{"sourceUnchanged",basicRows.front().coordinates.x==0&&basicRows.front().name==QString::fromUtf8("서울")}});}
    QJsonObject result{{"version",1},{"webCommit","ebcfae4d27b29cbbea6416a7045a4806930204be"},{"scope","synthetic mechanism exchange; production empty-v1 BLOCKED"},{"cases",cases},{"fontGeometryReport",fontGeometry}};
    QFile output(QString::fromLocal8Bit(argv[2]));require(output.open(QIODevice::WriteOnly),"open exchange output");const auto bytes=QJsonDocument(result).toJson(QJsonDocument::Compact);require(output.write(bytes)==bytes.size(),"write complete exchange");std::cerr<<"native place parity probe: cases="<<cases.size()<<" fontGeometryReport="<<fontGeometry.size()<<"; production-data=BLOCKED\n";return 0;
}catch(const std::exception& e){std::cerr<<"place parity probe FAIL: "<<e.what()<<'\n';return 1;}}
