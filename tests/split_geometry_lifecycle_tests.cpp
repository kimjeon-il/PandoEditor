#include "territorialpreviewruntime.h"
#include "territorial_fixture.h"
#include "territorialgeometry.h"
#include "projectcodec.h"
#include "geometrycalculator.h"
#include "splitgeometrynormalizer.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <zlib.h>
#include <pandoeditor/project.h>
#include <pandoeditor/presentationcommands.h>
#include <pandoeditor/geometrypredicates.h>
#include <QtTest>
using namespace pandoeditor;
namespace {
Geometry box(double x,double y,double width,double height) {
    Geometry geometry;geometry.polygons={{{{x,y},{x+width,y},{x+width,y+height},{x,y+height},{x,y}}}};return geometry;
}
void append(ProjectDocument& document,const std::string& id,const Geometry& geometry,const std::string& parent) {
    const GeometryRef binding{id,1};document.geometries.insert(binding,geometry);
    appendTerritory(document,{id,id,"",UnitKind::General,false},binding,parent,"partition");
    document.presentation.objectStyles[territorialRef(id)]={};
}
ProjectDocument nested() {
    ProjectDocument document({{"root","Root",box(0,0,10,10).polygons,0x123456}},{{"countries","Countries"}});
    append(document,"source",box(1,1,8,8),"root");append(document,"child",box(2,2,4,4),"source");
    append(document,"grandchild",box(2.5,2.5,1,1),"child");return document;
}
SplitGeometryPreviewResult preview(Project& project,const std::string& source,const Geometry& selected) {
    JobScheduler jobs;auto ticket=jobs.enqueue(project.snapshot(),"split-preview");jobs.takeNext();
    SplitTerritorialIntent intent{territorialRef(source),selected,"created","Created"};
    return calculateSplitGeometryPreview(project.snapshot(),intent,territorialPreviewCalculators(),ticket.token());
}
PrepareResult prepare(Project& project,const SplitGeometryPreviewResult& receipt) {
    JobScheduler jobs;auto ticket=jobs.enqueue(project.snapshot(),"split-commit");jobs.takeNext();
    return prepareSplitGeometryCommit(project.snapshot(),receipt,ticket.token());
}
double area(const Project& project,const std::string& id) {
    return planarArea(*project.document().geometries.get(staticGeometryBinding(project.document(),id).geometryRef));
}
}
namespace {
QByteArray readSplitObservations(const QString& name,const QByteArray& expectedSha,const QByteArray& compressedSha={}) {
    const auto path=QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures/web-m973-split/"+name);
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("SPLIT_OBSERVATION_INPUT_UNAVAILABLE");
    const auto input=file.readAll();QByteArray bytes;
    if(!compressedSha.isEmpty()&&QCryptographicHash::hash(input,QCryptographicHash::Sha256).toHex()!=compressedSha)throw std::runtime_error("SPLIT_OBSERVATION_COMPRESSED_MISMATCH");
    {
        z_stream stream{};if(inflateInit2(&stream,MAX_WBITS+16)!=Z_OK)throw std::runtime_error("SPLIT_OBSERVATION_INFLATE_FAILED");
        struct Cleanup {z_stream* stream;~Cleanup(){inflateEnd(stream);}} cleanup{&stream};
        stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));stream.avail_in=uInt(input.size());
        int status=Z_OK;do {char chunk[65536];stream.next_out=reinterpret_cast<Bytef*>(chunk);stream.avail_out=sizeof(chunk);
            status=inflate(&stream,Z_NO_FLUSH);if(status!=Z_OK&&status!=Z_STREAM_END)throw std::runtime_error("SPLIT_OBSERVATION_GZIP_INVALID");
            bytes.append(chunk,int(sizeof(chunk)-stream.avail_out));if(bytes.size()>64*1024*1024)throw std::runtime_error("SPLIT_OBSERVATION_TOO_LARGE");
        }while(status!=Z_STREAM_END);
        if(stream.avail_in)throw std::runtime_error("SPLIT_OBSERVATION_GZIP_TRAILING_DATA");
    }
    if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!=expectedSha)throw std::runtime_error("SPLIT_OBSERVATION_SOURCE_MISMATCH");
    return bytes;
}
Geometry observedGeometry(const QJsonObject& value) {
    Geometry geometry;geometry.type=value["type"].toString().toStdString();QJsonArray polygons=value["coordinates"].toArray();
    if(geometry.type=="Polygon")polygons=QJsonArray{polygons};
    for(const auto& p:polygons) {Polygon polygon;for(const auto& r:p.toArray()) {Ring ring;for(const auto& v:r.toArray()) {const auto point=v.toArray();ring.push_back({point[0].toDouble(),point[1].toDouble()});}polygon.push_back(std::move(ring));}geometry.polygons.push_back(std::move(polygon));}
    return geometry;
}
QJsonObject observedGeometryJson(const Geometry& geometry) {
    QJsonArray polygons;for(const auto& polygon:geometry.polygons) {QJsonArray rings;for(const auto& ring:polygon) {QJsonArray points;for(const auto point:ring)points.append(QJsonArray{point.x,point.y});rings.append(points);}polygons.append(rings);}
    return {{"type",QString::fromStdString(geometry.type)},{"coordinates",geometry.type=="Polygon"?polygons[0]:QJsonValue(polygons)}};
}
QJsonObject observedDocument(const QJsonObject& stage) {return stage["document"].toObject()["document"].toObject();}
ProjectDocument observedFixture(const QJsonObject& stage) {
    ProjectDocument document;document.documentId="m973-observed";document.presentation.userLayers={{"countries","Countries"}};
    const auto entities=observedDocument(stage)["entities"].toArray();
    for(const auto& value:entities) {
        const auto row=value.toObject(),properties=row["properties"].toObject();TerritorialUnit unit;
        unit.id=row["id"].toString().toStdString();unit.name=properties["name"].toString().toStdString();unit.notes=properties["notes"].toString().toStdString();unit.locked=properties["locked"].toBool();
        unit.kind=row["entityKind"]=="regional"?UnitKind::Regional:UnitKind::General;
        unit.metadata=QJsonDocument(properties["metadata"].toObject()).toJson(QJsonDocument::Compact).toStdString();
        unit.sourceFolderId=properties["sourceFolderId"].toString().toStdString();unit.sourceLibraryId=properties["sourceLibraryId"].toString().toStdString();unit.sourceGeometryVersion=properties["sourceGeometryVersion"].toString().toStdString();
        const GeometryRef geometry{"observed-"+unit.id,1};document.geometries.insert(geometry,observedGeometry(row["geometry"].toObject()));
        appendTerritory(document,unit,geometry,row["parentId"].toString().toStdString(),row["coverageMode"].toString().toStdString());
        const auto color=properties["style"].toObject()["color"].toString();bool valid=false;const auto rgb=color.mid(1).toUInt(&valid,16);document.presentation.objectStyles[territorialRef(unit.id)]={valid?rgb:0,1,valid};
    }
    for(const auto& value:observedDocument(stage)["distributionLayers"].toArray()) {const auto row=value.toObject();DistributionLayer layer;layer.id=row["id"].toString().toStdString();layer.name=row["name"].toString().toStdString();layer.locked=row["locked"].toBool();document.distributionLayers.push_back(layer);}
    for(const auto& value:observedDocument(stage)["distributionEntries"].toArray()) {const auto row=value.toObject();DistributionEntry entry;entry.id=row["id"].toString().toStdString();entry.layerId=row["layerId"].toString().toStdString();entry.value=row["value"].toDouble();if(!row["territorialUnitId"].toString().isEmpty())entry.territory=territorialRef(row["territorialUnitId"].toString().toStdString());document.distributionEntries.push_back(entry);}
    const auto observation=stage["document"].toObject();const auto settings=observation["presentation"].toObject()["labelSettings"].toObject();
    for(auto it=settings.begin();it!=settings.end();++it)if(it.key().startsWith("territorial:"))document.presentation.webPresentation.labelSettings[territorialRef(it.key().mid(12).toStdString())]={};
    for(const auto& value:observation["labels"].toArray()) {const auto row=value.toObject();PlaceLabel label;label.id=row["id"].toString().toStdString();label.name=row["name"].toString().toStdString();label.territory=territorialRef(row["countryId"].toString().toStdString());label.geometry={"observed-label-"+label.id,1};Geometry point;point.type="Point";point.points={{row["lon"].toDouble(),row["lat"].toDouble()}};document.geometries.insert(label.geometry,point);document.labels.push_back(label);}
    return document;
}
bool sameObservedCoverage(const Geometry& left,const Geometry& right) {
    const auto a=calculateGeometry({GeometryOperation::Difference,left,right}),b=calculateGeometry({GeometryOperation::Difference,right,left});
    return a.status==GeometryOperationStatus::Empty&&b.status==GeometryOperationStatus::Empty;
}
}
class SplitGeometryLifecycleTests:public QObject {
    Q_OBJECT
private slots:
    void pinnedWebSelectedUnionsReplayMutationLifecycle_data() {
        QTest::addColumn<QJsonObject>("observation");
        // The original rejection is immutable historical evidence. The actual
        // separately approved 07d3e20 observations own current expectations.
        const auto original=QJsonDocument::fromJson(readSplitObservations("lifecycle-observations.json.gz","b29f65be6069b98925d4c9a32c11794c723517ad52bd3dc842f6dff1fa2b7f5d")).object();
        bool oldDatelineRejected=false;for(const auto& value:original["cases"].toArray())if(value.toObject()["case"]=="root-date-line")oldDatelineRejected=!value.toObject()["stages"].toObject()["selected"].toObject()["selection"].toObject()["previewReady"].toBool();QVERIFY(oldDatelineRejected);
        QFile manifestFile(QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath("fixtures/web-m973-split/corrections/dateline/observation-manifest.json"));QVERIFY(manifestFile.open(QIODevice::ReadOnly));
        const auto manifest=QJsonDocument::fromJson(manifestFile.readAll()).object();const QString commit="07d3e2053c71573e11c5cf89151f5f6686038511";
        QCOMPARE(manifest["behavioralCommit"].toString(),commit);QCOMPARE(manifest["encoding"].toString(),QString("gzip-json-parts"));
        int all=0,count=0;QSet<QString> seen;
        for(const auto& partValue:manifest["parts"].toArray()) {
            const auto part=partValue.toObject();const auto bytes=readSplitObservations("corrections/dateline/"+part["path"].toString(),part["uncompressedSha256"].toString().toLatin1(),part["sha256"].toString().toLatin1());
            QCOMPARE(bytes.size(),part["uncompressedBytes"].toInt());const auto corpus=QJsonDocument::fromJson(bytes).object();QCOMPARE(corpus["behavioralCommit"].toString(),commit);
            const auto cases=corpus["cases"].toArray(),ids=part["caseIds"].toArray();QCOMPARE(cases.size(),ids.size());
            for(int index=0;index<cases.size();++index) {const auto row=cases[index].toObject();const auto id=row["case"].toString();QCOMPARE(id,ids[index].toString());QVERIFY(!seen.contains(id));seen.insert(id);++all;
                const auto selected=row["stages"].toObject()["selected"].toObject()["selection"].toObject();
                // A draft failure never supplies a mutation input. Kernel and
                // controller tests own those cases; no selection is invented.
                if(!selected["combinedGeometry"].isObject())continue;
                QTest::newRow(qPrintable(id))<<row;++count;
            }
        }
        QCOMPARE(all,37);QVERIFY(count>=27);
    }
    void pinnedWebSelectedUnionsReplayMutationLifecycle() {
        QFETCH(QJsonObject,observation);const auto stages=observation["stages"].toObject();const auto beforeStage=stages["before"].toObject();
        Project project;project.replace(observedFixture(beforeStage));const auto before=projectcodec::encode(project);const auto selected=stages["selected"].toObject()["selection"].toObject();
        const auto createdIds=observation["referenceEffects"].toObject()["createdIds"].toArray();
        // The mutation port accepts the observed generated ID as explicit input;
        // neither source observations nor expected entity IDs are rewritten.
        const auto created=createdIds.isEmpty()?std::string("uncommitted-created"):createdIds[0].toString().toStdString();
        SplitTerritorialIntent intent{territorialRef("source"),observedGeometry(selected["combinedGeometry"].toObject()),created,"새 객체"};
        JobScheduler jobs;auto ticket=jobs.enqueue(project.snapshot(),"observed-split");jobs.takeNext();
        const auto receipt=calculateSplitGeometryPreview(project.snapshot(),intent,territorialPreviewCalculators(),ticket.token());QCOMPARE(projectcodec::encode(project),before);
        const auto webPreview=stages["review"].toObject()["document"].toObject()["preview"];
        QVERIFY(!webPreview.isUndefined());
        if(webPreview.isObject()) {
            QVERIFY(webPreview.toObject()["afterFeatures"].isArray());const auto expected=webPreview.toObject()["afterFeatures"].toArray();
            QSet<QString> initialIds;for(const auto& value:observedDocument(beforeStage)["entities"].toArray())initialIds.insert(value.toObject()["id"].toString());
            QString previewCreated;for(const auto& value:expected){const auto id=value.toObject()["id"].toString();if(!initialIds.contains(id)){QVERIFY(previewCreated.isEmpty());previewCreated=id;}}
            // Map only the observed preview-created identity to this explicit
            // mutation input. Existing IDs and parent targets remain untouched.
            const auto ownerId=[&](const QString& id){return !previewCreated.isEmpty()&&id==previewCreated?created:id.toStdString();};
            std::vector<const AnnexGeometryRow*> shown;for(const auto& row:receipt.rows)if(row.after)shown.push_back(&row);
            QCOMPARE(shown.size(),std::size_t(expected.size()));
            for(int index=0;index<expected.size();++index) {
                const auto feature=expected[index].toObject(),properties=feature["properties"].toObject();QVERIFY(properties["parentId"].isString());
                const auto& row=*shown[std::size_t(index)];QCOMPARE(row.owner,territorialRef(ownerId(feature["id"].toString())));
                const auto parent=receipt.parentIds.find(row.owner);QVERIFY2(parent!=receipt.parentIds.end(),row.owner.id.c_str());QCOMPARE(parent->second,ownerId(properties["parentId"].toString()));
            }
            QCOMPARE(projectcodec::encode(project),before);
        }
        if(!stages.contains("confirm")) {
            QCOMPARE(receipt.ok(),selected["previewReady"].toBool());
            if(receipt.ok()){const auto rejected=prepareSplitGeometryCommit(project.snapshot(),receipt,ticket.token());QVERIFY(!rejected.ok());}
            QVERIFY(!project.undo());return;
        }
        QVERIFY2(receipt.ok(),receipt.detail.c_str());auto command=prepareSplitGeometryCommit(project.snapshot(),receipt,ticket.token());
        const auto confirm=stages["confirm"].toObject();
        if(!confirm["outcome"].toBool()) {QVERIFY(!command.ok());QCOMPARE(projectcodec::encode(project),before);QVERIFY(!project.undo());return;}
        QVERIFY2(command.ok(),command.detail.c_str());QCOMPARE(projectcodec::encode(project),before);QVERIFY(CommandProcessor::confirm(project,*command.preview).ok());
        const auto expected=observedDocument(confirm)["entities"].toArray();QCOMPARE(project.document().units.size(),std::size_t(expected.size()));
        for(const auto& value:expected) {const auto row=value.toObject(),properties=row["properties"].toObject();const auto id=row["id"].toString().toStdString();
            QVERIFY2(project.index().objects.count(territorialRef(id)),id.c_str());const auto& actual=project.document().units.at(project.index().objects.at(territorialRef(id)));
            QCOMPARE(actual.name,properties["name"].toString().toStdString());QCOMPARE(actual.notes,properties["notes"].toString().toStdString());QCOMPARE(actual.locked,properties["locked"].toBool());
            QCOMPARE(QJsonDocument::fromJson(QByteArray::fromStdString(actual.metadata)).object(),properties["metadata"].toObject());
            QCOMPARE(actual.sourceFolderId,properties["sourceFolderId"].toString().toStdString());QCOMPARE(actual.sourceLibraryId,properties["sourceLibraryId"].toString().toStdString());QCOMPARE(actual.sourceGeometryVersion,properties["sourceGeometryVersion"].toString().toStdString());
            QCOMPARE(staticParentRelation(project.document(),id).parentId,row["parentId"].toString().toStdString());QCOMPARE(staticParentRelation(project.document(),id).coverageMode,row["coverageMode"].toString().toStdString());
            const auto& nativeGeometry=*project.document().geometries.get(staticGeometryBinding(project.document(),id).geometryRef);
            QVERIFY2(sameObservedCoverage(nativeGeometry,observedGeometry(row["geometry"].toObject())),id.c_str());
            QCOMPARE(observedGeometryJson(nativeGeometry),row["geometry"].toObject());
            QCOMPARE(project.document().presentation.objectStyles.at(territorialRef(id)).explicitColor,properties["style"].toObject().contains("color"));
        }
        const auto entries=observedDocument(confirm)["distributionEntries"].toArray();QCOMPARE(project.document().distributionEntries.size(),std::size_t(entries.size()));
        for(int index=0;index<entries.size();++index) {const auto row=entries[index].toObject();const auto& actual=project.document().distributionEntries[index];QCOMPARE(actual.id,row["id"].toString().toStdString());QCOMPARE(actual.territory,std::optional<ObjectRef>{territorialRef(row["territorialUnitId"].toString().toStdString())});}
        QCOMPARE(project.document().labels.size(),std::size_t(confirm["document"].toObject()["labels"].toArray().size()));
        for(const auto& label:project.document().labels)QCOMPARE(label.territory,std::optional<ObjectRef>{territorialRef("source")});
        const auto after=projectcodec::encode(project);QVERIFY(!CommandProcessor::confirm(project,*command.preview).ok());QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),before);QVERIFY(!project.undo());
        QVERIFY(project.redo());QCOMPARE(projectcodec::encode(project),after);QVERIFY(!project.redo());Project reopened;reopened.replace(projectcodec::decode(after));QCOMPARE(projectcodec::encode(reopened),after);
    }
    void rawEntityNormalizationDoesNotWrapOrSegmentGeographicEdges() {
        Geometry input;input.type="Polygon";input.polygons={{{{179,-2},{179,2},{-179,2},{-179,-2},{179,-2}}}};
        const auto before=observedGeometryJson(input);const auto raw=normalizeSplitRawGeometry(input),wrapped=wrapSplitGeometry(input),clipped=normalizeSplitClippedGeometry(input);
        QVERIFY(raw.succeeded());QVERIFY(raw.inputUnchanged);QVERIFY(wrapped.succeeded());QVERIFY(clipped.succeeded());QCOMPARE(observedGeometryJson(input),before);
        QCOMPARE(raw.geometry.type,std::string("Polygon"));QCOMPARE(raw.geometry.polygons[0][0].size(),std::size_t(5));QCOMPARE(raw.geometry.polygons[0][0][0].x,-179.);
        QCOMPARE(wrapped.geometry.polygons.size(),std::size_t(2));QVERIFY(clipped.geometry.polygons[0][0].size()>raw.geometry.polygons[0][0].size());
    }
    void childSourceMovesContainedChildAndPreservesGrandchildRelation() {
        Project project;project.replace(nested());const auto before=projectcodec::encode(project);
        const auto receipt=preview(project,"source",box(1,1,5,8));QVERIFY2(receipt.ok(),receipt.detail.c_str());
        QCOMPARE(receipt.parentIds.at(territorialRef("created")),std::string("root"));
        QCOMPARE(receipt.parentIds.at(territorialRef("source")),std::string("root"));
        QCOMPARE(receipt.parentIds.at(territorialRef("child")),std::string("created"));
        QCOMPARE(receipt.parentIds.at(territorialRef("grandchild")),std::string("child"));
        QCOMPARE(receipt.rows.size(),std::size_t(3));QCOMPARE(receipt.rows[0].owner,territorialRef("created"));QCOMPARE(receipt.rows[1].owner,territorialRef("source"));QCOMPARE(receipt.rows[2].owner,territorialRef("child"));
        QCOMPARE(projectcodec::encode(project),before);auto command=prepare(project,receipt);QVERIFY2(command.ok(),command.detail.c_str());
        QVERIFY(CommandProcessor::confirm(project,*command.preview).ok());
        QCOMPARE(staticParentRelation(project.document(),"created").parentId,std::string("root"));
        QCOMPARE(staticParentRelation(project.document(),"child").parentId,std::string("created"));
        QCOMPARE(staticParentRelation(project.document(),"grandchild").parentId,std::string("child"));
        QCOMPARE(area(project,"child"),16.);QCOMPARE(area(project,"grandchild"),1.);
        const auto after=projectcodec::encode(project);QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),before);
        QVERIFY(project.redo());QCOMPARE(projectcodec::encode(project),after);Project reopened;reopened.replace(projectcodec::decode(after));QCOMPARE(projectcodec::encode(reopened),after);
    }
    void lockedGrandchildOfWhollyMovedChildRemainsUntouched() {
        auto document=nested();document.units.at(validateDocument(document).objects.at(territorialRef("grandchild"))).locked=true;
        Project project;project.replace(document);const auto before=projectcodec::encode(project);const auto binding=staticGeometryBinding(project.document(),"grandchild").geometryRef;
        const auto receipt=preview(project,"source",box(1,1,5,8));QVERIFY2(receipt.ok(),receipt.detail.c_str());
        auto command=prepare(project,receipt);QVERIFY2(command.ok(),command.detail.c_str());QVERIFY(CommandProcessor::confirm(project,*command.preview).ok());
        QCOMPARE(staticParentRelation(project.document(),"child").parentId,std::string("created"));
        QCOMPARE(staticParentRelation(project.document(),"grandchild").parentId,std::string("child"));
        QCOMPARE(staticGeometryBinding(project.document(),"grandchild").geometryRef,binding);
        QVERIFY(project.document().units.at(project.index().objects.at(territorialRef("grandchild"))).locked);
        QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),before);
    }
    void childSourceClipsParentAndMovesContainedGrandchild() {
        Project project;project.replace(nested());const auto receipt=preview(project,"source",box(1,1,3,8));
        QVERIFY2(receipt.ok(),receipt.detail.c_str());
        QCOMPARE(receipt.parentIds.at(territorialRef("child")),std::string("source"));QCOMPARE(receipt.parentIds.at(territorialRef("grandchild")),std::string("created"));
        auto command=prepare(project,receipt);QVERIFY2(command.ok(),command.detail.c_str());
        QVERIFY(CommandProcessor::confirm(project,*command.preview).ok());
        QCOMPARE(staticParentRelation(project.document(),"child").parentId,std::string("source"));
        QCOMPARE(staticParentRelation(project.document(),"grandchild").parentId,std::string("created"));
        QCOMPARE(area(project,"child"),8.);QCOMPARE(area(project,"grandchild"),1.);
    }
    void rootSourceClipsAndRemovesChildrenWithoutReparenting() {
        Project project;project.replace(nested());const auto before=projectcodec::encode(project);
        const auto receipt=preview(project,"root",box(0,0,4,10));QVERIFY2(receipt.ok(),receipt.detail.c_str());
        auto command=prepare(project,receipt);QVERIFY2(command.ok(),command.detail.c_str());
        QVERIFY(CommandProcessor::confirm(project,*command.preview).ok());
        QCOMPARE(staticParentRelation(project.document(),"created").parentId,std::string());
        QCOMPARE(staticParentRelation(project.document(),"source").parentId,std::string("root"));
        QCOMPARE(staticParentRelation(project.document(),"child").parentId,std::string("source"));
        QVERIFY(!project.index().objects.count(territorialRef("grandchild")));
        QCOMPARE(area(project,"source"),40.);QCOMPARE(area(project,"child"),8.);
        QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),before);
    }
    void rootRemovedChildReferencesBlockCommit_data() {
        QTest::addColumn<bool>("distribution");QTest::newRow("distribution")<<true;QTest::newRow("label-settings")<<false;
    }
    void rootRemovedChildReferencesBlockCommit() {
        QFETCH(bool,distribution);auto document=nested();
        if(distribution) {DistributionLayer layer;layer.id="statistics";layer.name="Statistics";document.distributionLayers.push_back(layer);
            DistributionEntry entry;entry.id="entry";entry.layerId=layer.id;entry.territory=territorialRef("grandchild");entry.value=1;document.distributionEntries.push_back(entry);}
        else document.presentation.webPresentation.labelSettings[territorialRef("grandchild")]={};
        Project project;project.replace(document);const auto before=projectcodec::encode(project);
        const auto receipt=preview(project,"root",box(0,0,4,10));QVERIFY2(receipt.ok(),receipt.detail.c_str());
        const auto command=prepare(project,receipt);QVERIFY(!command.ok());QVERIFY(command.detail.find("DANGLING_REF")!=std::string::npos);
        QCOMPARE(projectcodec::encode(project),before);QVERIFY(!project.undo());
    }
    void referencesRemainOnSurvivingSourceAndReparentedChild() {
        auto document=nested();DistributionLayer layer;layer.id="statistics";layer.name="Statistics";document.distributionLayers.push_back(layer);
        for(const auto id:{"source","child"}) {DistributionEntry entry;entry.id=std::string("entry-")+id;entry.layerId=layer.id;entry.territory=territorialRef(id);entry.value=1;document.distributionEntries.push_back(entry);
            document.presentation.webPresentation.labelSettings[territorialRef(id)]={};}
        Project project;project.replace(document);const auto before=projectcodec::encode(project);
        const auto receipt=preview(project,"source",box(1,1,5,8));QVERIFY(receipt.ok());auto command=prepare(project,receipt);QVERIFY2(command.ok(),command.detail.c_str());
        QVERIFY(CommandProcessor::confirm(project,*command.preview).ok());
        QCOMPARE(project.document().distributionEntries.size(),std::size_t(2));
        QCOMPARE(project.document().distributionEntries[0].territory,std::optional<ObjectRef>{territorialRef("source")});
        QCOMPARE(project.document().distributionEntries[1].territory,std::optional<ObjectRef>{territorialRef("child")});
        QVERIFY(project.document().presentation.webPresentation.labelSettings.count(territorialRef("source")));
        QVERIFY(project.document().presentation.webPresentation.labelSettings.count(territorialRef("child")));
        QVERIFY(!project.document().presentation.webPresentation.labelSettings.count(territorialRef("created")));
        QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),before);
    }
    void untouchedLockedChildDoesNotBlockSelectionButChangedChildDoes() {
        auto document=nested();document.units.at(validateDocument(document).objects.at(territorialRef("child"))).locked=true;
        Project project;project.replace(document);const auto before=projectcodec::encode(project);
        const auto untouched=preview(project,"source",box(7,1,2,8));QVERIFY2(untouched.ok(),untouched.detail.c_str());
        const auto command=prepare(project,untouched);QVERIFY2(command.ok(),command.detail.c_str());
        QVERIFY(!preview(project,"source",box(1,1,5,8)).ok());QCOMPARE(projectcodec::encode(project),before);
    }
    void datelineSplitCommitsWithoutRewritingUntouchedParent_data() {
        QTest::addColumn<bool>("childScope");QTest::newRow("root")<<false;QTest::newRow("child")<<true;
    }
    void datelineSplitCommitsWithoutRewritingUntouchedParent() {
        QFETCH(bool,childScope);Geometry raw;raw.type="Polygon";raw.polygons={{{{179,-2},{179,2},{-179,2},{-179,-2},{179,-2}}}};
        ProjectDocument document({{"source","Source",raw.polygons,0x123456}},{{"countries","Countries"}});
        if(childScope) {Geometry parent;parent.polygons={{{{178,-4},{178,4},{-178,4},{-178,-4},{178,-4}}}};const GeometryRef ref{"parent",1};document.geometries.insert(ref,parent);appendTerritory(document,{"parent","Parent","",UnitKind::General,false},ref,"","explicit");document.presentation.objectStyles[territorialRef("parent")]={};setFixtureParent(document,territorialRef("source"),territorialRef("parent"));}
        Geometry selected=box(179,0,1,2);selected.polygons.push_back(box(-180,0,1,2).polygons.front());
        Project project;project.replace(document);const auto before=projectcodec::encode(project);
        const auto receipt=preview(project,"source",selected);QVERIFY2(receipt.ok(),receipt.detail.c_str());QCOMPARE(projectcodec::encode(project),before);
        auto command=prepare(project,receipt);QVERIFY2(command.ok(),command.detail.c_str());QVERIFY(CommandProcessor::confirm(project,*command.preview).ok());
        QCOMPARE(area(project,"source"),4.);QCOMPARE(area(project,"created"),4.);
        QCOMPARE(staticParentRelation(project.document(),"created").parentId,childScope?std::string("parent"):std::string());
        if(childScope) {QCOMPARE(staticGeometryBinding(project.document(),"parent").geometryRef,(GeometryRef{"parent",1}));QCOMPARE(project.document().geometries.get({"parent",1})->polygons[0][0][2].x,-178.);}
        const auto after=projectcodec::encode(project);Project reopened;reopened.replace(projectcodec::decode(after));QCOMPARE(projectcodec::encode(reopened),after);
        QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),before);QVERIFY(project.redo());QCOMPARE(projectcodec::encode(project),after);
    }
    void rootExhaustionHasInertPreviewButCannotCommit() {
        Project project;project.replace(nested());const auto before=projectcodec::encode(project);
        const auto receipt=preview(project,"root",box(0,0,10,10));QVERIFY2(receipt.ok(),receipt.detail.c_str());
        QVERIFY(receipt.remainingGeometry.polygons.empty());QCOMPARE(receipt.rows.size(),std::size_t(2));
        QCOMPARE(receipt.rows[0].owner,territorialRef("root"));QVERIFY(!receipt.rows[0].after);QVERIFY(receipt.rows[1].after);
        QVERIFY(!receipt.parentIds.count(territorialRef("root")));QCOMPARE(receipt.parentIds.at(territorialRef("created")),std::string());
        const auto rejected=prepare(project,receipt);QVERIFY(!rejected.ok());QCOMPARE(rejected.detail,std::string("SPLIT_SOURCE_EXHAUSTED"));
        QCOMPARE(projectcodec::encode(project),before);QVERIFY(!project.undo());
    }
    void rootCreationDoesNotIntroduceOverlapWithAnotherCountry() {
        auto document=nested();const GeometryRef geometry{"neighbor",1};document.geometries.insert(geometry,box(5,0,10,10));
        appendTerritory(document,{"neighbor","Neighbor","",UnitKind::General,false},geometry,"","explicit");document.presentation.objectStyles[territorialRef("neighbor")]={};
        Project project;project.replace(document);const auto before=projectcodec::encode(project);
        QVERIFY(!preview(project,"root",box(6,0,4,10)).ok());QCOMPARE(projectcodec::encode(project),before);
    }
    void rootDeletionCleansDisplaySettingsAndUndoRestoresThem() {
        auto document=nested();auto& web=document.presentation.webPresentation;
        web.hiddenItems["subunits"]={"grandchild","child"};web.objectStyles[territorialPresentationKey("grandchild")].opacity=.4;web.objectStyles[territorialPresentationKey("child")].opacity=.7;
        Project project;project.replace(document);const auto before=projectcodec::encode(project);
        const auto receipt=preview(project,"root",box(0,0,4,10));QVERIFY(receipt.ok());auto command=prepare(project,receipt);QVERIFY(command.ok());QVERIFY(CommandProcessor::confirm(project,*command.preview).ok());
        const auto& next=project.document().presentation.webPresentation;
        QVERIFY(!next.hiddenItems.at("subunits").count("grandchild"));QVERIFY(next.hiddenItems.at("subunits").count("child"));
        QVERIFY(!next.objectStyles.count(territorialPresentationKey("grandchild")));QCOMPARE(next.objectStyles.at(territorialPresentationKey("child")).opacity,std::optional<double>{.7});
        const auto after=projectcodec::encode(project);QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),before);QVERIFY(project.redo());QCOMPARE(projectcodec::encode(project),after);
    }
    void survivorDisplayEditsRemainAcrossSplitUndoRedo() {
        auto document=nested();auto& web=document.presentation.webPresentation;
        web.hiddenItems["subunits"]={"grandchild","child"};web.objectStyles[territorialPresentationKey("grandchild")].opacity=.4;web.objectStyles[territorialPresentationKey("child")].opacity=.7;
        Project project;project.replace(document);const auto receipt=preview(project,"root",box(0,0,4,10));QVERIFY(receipt.ok());auto command=prepare(project,receipt);QVERIFY(command.ok());QVERIFY(CommandProcessor::confirm(project,*command.preview).ok());
        QCOMPARE(PresentationCommandProcessor::apply(project,SetScopedVisibility{"subunits",{territorialRef("child")},true}),PresentationResult::Applied);
        PresentationStyle changed;changed.opacity=.2;QCOMPARE(PresentationCommandProcessor::apply(project,PatchGroupPresentation{"subunits",changed}),PresentationResult::Applied);
        const auto checkSurvivor=[&] {const auto& current=project.document().presentation.webPresentation;
            return itemVisible(current,"subunits","child")&&current.objectStyles.at(territorialPresentationKey("child")).opacity==std::optional<double>{.2};};
        QVERIFY(project.undo());QVERIFY(checkSurvivor());
        QVERIFY(project.index().objects.count(territorialRef("grandchild")));QVERIFY(!itemVisible(project.document().presentation.webPresentation,"subunits","grandchild"));
        QCOMPARE(project.document().presentation.webPresentation.objectStyles.at(territorialPresentationKey("grandchild")).opacity,std::optional<double>{.4});
        QVERIFY(project.redo());QVERIFY(checkSurvivor());QVERIFY(!project.index().objects.count(territorialRef("grandchild")));
        QVERIFY(!project.document().presentation.webPresentation.objectStyles.count(territorialPresentationKey("grandchild")));
    }
    void rootDeletionPreservesDescriptiveGenericCompatibilityMetadata() {
        auto document=nested();const GeometryRef geometry{"generic",1};document.geometries.insert(geometry,box(2.5,2.5,.5,.5));
        GenericFeature feature;feature.id="generic";feature.name="Compatibility";feature.geometry=geometry;feature.source.details="{\"legacyGenericSemantics\":{\"ownerId\":\"grandchild\"}}";document.genericFeatures.push_back(feature);
        Project project;project.replace(document);const auto before=projectcodec::encode(project);
        const auto receipt=preview(project,"root",box(0,0,4,10));QVERIFY(receipt.ok());auto command=prepare(project,receipt);QVERIFY(command.ok());QVERIFY(CommandProcessor::confirm(project,*command.preview).ok());
        QCOMPARE(project.document().genericFeatures.size(),std::size_t(1));QCOMPARE(project.document().genericFeatures.front().source.details,feature.source.details);
        const auto after=projectcodec::encode(project);QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),before);QVERIFY(project.redo());QCOMPARE(projectcodec::encode(project),after);
    }
    void rootAndChildUseTheirProductionContainmentPolicies() {
        Project project;project.replace(nested());
        const auto root=preview(project,"root",box(-1e-8,0,5+1e-8,10));
        QVERIFY2(root.ok(),root.detail.c_str());QCOMPARE(planarArea(root.transferredGeometry),50.);
        const auto child=preview(project,"source",box(1-1e-10,1,4+1e-10,8));
        QVERIFY2(child.ok(),child.detail.c_str());
        double west=180;for(const auto& polygon:child.transferredGeometry.polygons)for(const auto& ring:polygon)for(const auto point:ring)west=std::min(west,point.x);
        QCOMPARE(west,1-1e-10);
    }
    void rootRetainsPositiveRemainderWhileChildRequiresSignificantRemainder() {
        Project project;project.replace(nested());
        const auto root=preview(project,"root",box(0,0,10-1e-9,10));QVERIFY2(root.ok(),root.detail.c_str());
        QVERIFY(planarArea(root.remainingGeometry)>0);
        QVERIFY(!preview(project,"source",box(1,1,8-1e-9,8)).ok());
    }
    void independentRegionAndLockedParentAreNotSplitSources() {
        auto document=nested();const GeometryRef geometry{"region",1};document.geometries.insert(geometry,box(20,0,10,10));
        appendTerritory(document,{"region","Region","",UnitKind::Regional,false},geometry,"","explicit");document.presentation.objectStyles[territorialRef("region")]={};
        Project project;project.replace(document);const auto before=projectcodec::encode(project);
        QVERIFY(!preview(project,"region",box(20,0,5,10)).ok());QCOMPARE(projectcodec::encode(project),before);
        document.units.at(validateDocument(document).objects.at(territorialRef("root"))).locked=true;project.replace(document);
        QVERIFY(!preview(project,"source",box(7,1,2,8)).ok());
    }
    void receiptRejectsOutsideSelectionAndExhaustedChildSource() {
        Project project;project.replace(nested());const auto before=projectcodec::encode(project);
        QVERIFY(!preview(project,"source",box(0,0,3,3)).ok());
        QVERIFY(!preview(project,"source",box(1,1,8,8)).ok());
        const auto exhaustedRoot=preview(project,"root",box(0,0,10,10));QVERIFY(exhaustedRoot.ok());QVERIFY(!prepare(project,exhaustedRoot).ok());
        QCOMPARE(projectcodec::encode(project),before);
    }
    void receiptRejectsStaleProjectAndCancelledCalculation() {
        Project project;project.replace(nested());const auto receipt=preview(project,"source",box(7,1,2,8));QVERIFY(receipt.ok());
        Project other;other.replace(project.document());const auto wrong=prepare(other,receipt);QCOMPARE(wrong.error,CommandError::ProjectMismatch);
        auto command=prepare(project,receipt);QVERIFY(command.ok());QVERIFY(CommandProcessor::confirm(project,*command.preview).ok());
        const auto stale=prepare(project,receipt);QCOMPARE(stale.error,CommandError::StaleRevision);
        JobScheduler jobs;auto ticket=jobs.enqueue(project.snapshot(),"cancelled-split");jobs.takeNext();jobs.cancel(ticket.id());
        SplitTerritorialIntent intent{territorialRef("source"),box(1,1,1,8),"next","Next"};
        const auto cancelled=calculateSplitGeometryPreview(project.snapshot(),intent,territorialPreviewCalculators(),ticket.token());
        QCOMPARE(cancelled.status,GeometryOperationStatus::Cancelled);QVERIFY(!cancelled.plan);QVERIFY(cancelled.patch.creations.empty());
    }
};
QTEST_GUILESS_MAIN(SplitGeometryLifecycleTests)
#include "split_geometry_lifecycle_tests.moc"
