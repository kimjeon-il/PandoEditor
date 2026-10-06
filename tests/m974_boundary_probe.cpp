#include "territorialpreviewruntime.h"
#include "editorcontroller.h"
#include "projectcodec.h"
#include "territorial_fixture.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace pandoeditor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Geometry decodeGeometry(const QJsonObject& value){Geometry geometry;geometry.type=value["type"].toString().toStdString();require(geometry.type=="Polygon"||geometry.type=="MultiPolygon","Expected polygon fixture");auto polygons=value["coordinates"].toArray();if(geometry.type=="Polygon"){QJsonArray wrapped;wrapped.append(polygons);polygons=wrapped;}for(const auto& p:polygons){Polygon polygon;for(const auto& r:p.toArray()){Ring ring;for(const auto& c:r.toArray()){const auto xy=c.toArray();require(xy.size()==2&&xy[0].isDouble()&&xy[1].isDouble(),"Invalid fixture coordinate");ring.push_back({xy[0].toDouble(),xy[1].toDouble()});}polygon.push_back(std::move(ring));}geometry.polygons.push_back(std::move(polygon));}return geometry;}
QJsonObject encodeGeometry(const Geometry& geometry){QJsonArray polygons;for(const auto& p:geometry.polygons){QJsonArray rings;for(const auto& r:p){QJsonArray coordinates;for(const auto c:r)coordinates.append(QJsonArray{c.x,c.y});rings.append(coordinates);}polygons.append(rings);}return {{"type",QString::fromStdString(geometry.type)},{"coordinates",geometry.type=="Polygon"?QJsonValue(polygons.first()):QJsonValue(polygons)}};}
ProjectDocument fixture(const QJsonObject& definition){
    ProjectDocument document;document.documentId="m974-boundary-controller";DistributionLayer distribution;distribution.id="distribution";distribution.name="Fixture";document.distributionLayers.push_back(distribution);
    for(const auto& value:definition["features"].toArray()) {
        const auto feature=value.toObject(),properties=feature["properties"].toObject();const auto id=feature["id"].toString().toStdString();require(!id.empty(),"Missing fixture identity");const GeometryRef ref{id,1};document.geometries.insert(ref,decodeGeometry(feature["geometry"].toObject()));
        TerritorialUnit unit;unit.id=id;unit.name=properties["name"].toString().toStdString();unit.baseName=unit.name;unit.kind=properties["entityKind"]=="regional"?UnitKind::Regional:UnitKind::General;unit.locked=properties["locked"].toBool();unit.notes=properties["notes"].toString().toStdString();unit.metadata=QJsonDocument(properties["metadata"].toObject()).toJson(QJsonDocument::Compact).toStdString();unit.sourceFolderId=properties["sourceFolderId"].toString().toStdString();unit.sourceLibraryId=properties["sourceLibraryId"].toString().toStdString();unit.sourceGeometryVersion=properties["sourceGeometryVersion"].toString().toStdString();
        require(properties["validFrom"].isNull()&&properties["validTo"].isNull(),"Dated fixture needs timeline-enabled probe");require(properties["style"].toObject().empty(),"Nonempty territorial style fixture needs explicit native mapping");
        const auto parent=properties["parentId"].toString().toStdString();appendTerritory(document,std::move(unit),ref,parent,properties["coverageMode"].toString().toStdString());document.presentation.objectStyles[territorialRef(id)]={0,1,false};
        if(!parent.empty()) {
            DistributionEntry entry;entry.id="entry-"+id;entry.layerId="distribution";entry.territory=territorialRef(id);entry.value=1;document.distributionEntries.push_back(entry);
            document.presentation.webPresentation.hiddenItems["subunits"].insert(id);document.presentation.webPresentation.labelSettings[territorialRef(id)]={};document.presentation.webPresentation.objectStyles[territorialPresentationKey(id)].opacity=.5;
        }
    }
    validateDocument(document);return document;
}
// Source-only input bridge. Search at most four representable doubles around
// the native forward projection, and require the PUBLIC inverse to equal the
// intended geographic values exactly. No browser result enters this bridge.
struct ExactScreen {Point naive,chosen;int dx=0,dy=0;};
std::optional<ExactScreen> exactScreen(const MapProjection& projection,Point target){
    if(!std::isfinite(target.x)||!std::isfinite(target.y))return {};ExactScreen value;value.naive=projection.project(target);value.chosen=value.naive;
    const auto solve=[&](bool x,double base,double wanted,double& chosen,int& steps){const auto inverse=[&](double v){const auto point=x?projection.unproject(v,value.naive.y):projection.unproject(value.naive.x,v);return x?point.x:point.y;};if(inverse(base)==wanted)return true;double below=base,above=base;for(int i=1;i<=4;++i){below=std::nextafter(below,-std::numeric_limits<double>::infinity());above=std::nextafter(above,std::numeric_limits<double>::infinity());for(const auto& trial:{std::pair<double,int>{below,-i},{above,i}})if(inverse(trial.first)==wanted){chosen=trial.first;steps=trial.second;return true;}}return false;};
    if(!solve(true,value.naive.x,target.x,value.chosen.x,value.dx)||!solve(false,value.naive.y,target.y,value.chosen.y,value.dy))return {};const auto inverse=projection.unproject(value.chosen.x,value.chosen.y);if(inverse.x!=target.x||inverse.y!=target.y)return {};return value;
}
void settle(EditorController& controller){QElapsedTimer timer;timer.start();while(timer.elapsed()<30000){QCoreApplication::processEvents(QEventLoop::AllEvents,10);if(!controller.geometryEditState().value("calculating").toBool())return;QThread::msleep(2);}throw std::runtime_error("Boundary controller job did not settle");}
void drain(){QElapsedTimer timer;timer.start();while(timer.elapsed()<150){QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(2);}}
QJsonObject unobserved(const QString& reason){return {{"observed",false},{"reason",reason}};}
QJsonArray entityRows(const ProjectDocument& document){QJsonArray entities;
    for(const auto& unit:document.units){const auto& relation=staticParentRelation(document,unit.id);const auto owner=territorialRef(unit.id);QJsonObject style;const auto nativeStyle=document.presentation.objectStyles.at(owner);if(nativeStyle.explicitColor)style["color"]=QString("#%1").arg(nativeStyle.color,6,16,QChar('0'));
        QJsonObject properties{{"schemaVersion",5},{"entityKind",unit.kind==UnitKind::General?"general":"regional"},{"name",QString::fromStdString(unit.name)},{"parentId",QString::fromStdString(relation.parentId)},{"coverageMode",QString::fromStdString(relation.coverageMode)},{"style",style},{"locked",unit.locked},{"validFrom",QJsonValue::Null},{"validTo",QJsonValue::Null},{"notes",QString::fromStdString(unit.notes)},{"metadata",QJsonDocument::fromJson(QByteArray::fromStdString(unit.metadata)).object()},{"sourceFolderId",QString::fromStdString(unit.sourceFolderId)},{"sourceLibraryId",QString::fromStdString(unit.sourceLibraryId)},{"sourceGeometryVersion",QString::fromStdString(unit.sourceGeometryVersion)}};
        entities.append(QJsonObject{{"id",QString::fromStdString(unit.id)},{"parentId",properties["parentId"]},{"coverageMode",properties["coverageMode"]},{"entityKind",properties["entityKind"]},{"properties",properties},{"geometry",encodeGeometry(*document.geometries.get(staticGeometryBinding(document,unit.id).geometryRef))}});
    }
    return entities;
}
QJsonObject topologyHelper(const ProjectSnapshot& snapshot,const std::vector<ObjectRef>& owners,const QJsonObject& definition){
    const auto seed=definition["entryMode"]=="auto-child"?definition["seedId"].toString().toStdString():std::string{};
    const auto session=sharedboundary::Session::prepare(snapshot.document(),snapshot.index(),owners,{},seed);
    QJsonArray handles,segments,selected,inputOwners;
    for(const auto& owner:owners){inputOwners.append(QString::fromStdString(owner.id));if(session->drafts().count(owner.id))selected.append(QString::fromStdString(owner.id));}
    const auto reference=[](const sharedboundary::Reference& ref,bool virtualRef){QJsonObject row{{"featureId",QString::fromStdString(ref.owner)},{"polygonIndex",double(ref.polygon)},{"ringIndex",double(ref.ring)}};if(virtualRef){row["segmentIndex"]=double(ref.index);row["t"]=ref.t;}else row["vertexIndex"]=double(ref.index);return row;};
    for(const auto& node:session->nodes()){
        QJsonArray refs,virtualRefs,rawRefs,rawVirtualRefs,ownerIds,incident;
        for(const auto& ref:node.refs){const auto row=reference(ref,false);rawRefs.append(row);if(session->drafts().count(ref.owner))refs.append(row);}
        for(const auto& ref:node.virtualRefs){const auto row=reference(ref,true);rawVirtualRefs.append(row);if(session->drafts().count(ref.owner))virtualRefs.append(row);}
        for(const auto& owner:node.owners)ownerIds.append(QString::fromStdString(owner));
        for(const auto& [a,b]:node.incidentSegments)incident.append(QJsonObject{{"start",QJsonArray{a.x,a.y}},{"end",QJsonArray{b.x,b.y}}});
        QJsonObject row{{"nodeKey",QString::fromStdString(sharedboundary::nodeKeyText(node.coordinate))},{"coordinate",QJsonArray{node.coordinate.x,node.coordinate.y}},{"quantizedKey",QJsonArray{node.key.first,node.key.second}},{"refs",refs},{"virtualRefs",virtualRefs},{"rawRefs",rawRefs},{"rawVirtualRefs",rawVirtualRefs},{"ownerIds",ownerIds},{"fixed",node.fixed},{"segments",incident}};
        if(!refs.empty()){const auto first=refs.first().toObject();row["polygonIndex"]=first["polygonIndex"];row["ringIndex"]=first["ringIndex"];row["index"]=first["vertexIndex"];}else if(!virtualRefs.empty()){const auto first=virtualRefs.first().toObject();row["polygonIndex"]=first["polygonIndex"];row["ringIndex"]=first["ringIndex"];}
        handles.append(row);
    }
    for(const auto& segment:session->segments()) {const auto a=session->nodes().at(segment.start).coordinate,b=session->nodes().at(segment.end).coordinate;const auto left=sharedboundary::nodeKeyText(a),right=sharedboundary::nodeKeyText(b);QJsonArray owners;for(const auto& owner:segment.owners)owners.append(QString::fromStdString(owner));segments.append(QJsonObject{{"key",QString::fromStdString(left<right?left+"|"+right:right+"|"+left)},{"start",QJsonArray{a.x,a.y}},{"end",QJsonArray{b.x,b.y}},{"ownerIds",owners},{"startNodeIndex",double(segment.start)},{"endNodeIndex",double(segment.end)}});}
    QJsonObject result{{"observed",true},{"scope","topology-and-receipt-helper-only"},{"entrypoint","sharedboundary::Session::prepare/move → calculateBoundaryGeometryPreview → prepareBoundaryGeometryCommit"},{"representationMapping","Native vectors retain their returned order. Reference fields map owner→featureId, polygon→polygonIndex, ring→ringIndex, index→vertexIndex or segmentIndex. refs/virtualRefs are selected-owner-filtered in original vector order as web handles are; unfiltered originals remain rawRefs/rawVirtualRefs. selectedIdsInInputOrder filters input order by actual helper draft membership; no controller observation is inferred."},{"inputOwnerIds",inputOwners},{"selectedIdsInInputOrder",selected},{"preparation",QJsonObject{{"valid",session->valid()},{"error",QString::fromStdString(session->error())},{"handles",handles},{"segments",segments}}}};
    if(definition.contains("inspectCoordinate")) {
        const auto coordinate=definition["inspectCoordinate"].toArray();require(coordinate.size()==2&&coordinate[0].isDouble()&&coordinate[1].isDouble()&&std::isfinite(coordinate[0].toDouble())&&std::isfinite(coordinate[1].toDouble()),"Invalid supplemental inspect coordinate");
        const Point inspected{coordinate[0].toDouble(),coordinate[1].toDouble()};const auto key=sharedboundary::nodeKey(inspected);
        result["inspect"]=QJsonObject{{"coordinate",coordinate},{"nodeKey",QString::fromStdString(sharedboundary::nodeKeyText(inspected))},{"quantizedKey",QJsonArray{key.first,key.second}}};
    }
    if(!session->valid())return result;
    const auto key=definition["move"].toObject()["nodeKey"].toString().toStdString();const auto node=std::find_if(session->nodes().begin(),session->nodes().end(),[&](const auto& candidate){return sharedboundary::nodeKeyText(candidate.coordinate)==key;});const bool began=node!=session->nodes().end()&&session->beginDrag(std::size_t(node-session->nodes().begin()));
    result["gesture"]=QJsonObject{{"ok",began}};if(!began)return result;const auto coordinate=definition["move"].toObject()["coordinate"].toArray();const bool moved=session->move(std::size_t(node-session->nodes().begin()),{coordinate[0].toDouble(),coordinate[1].toDouble()});QJsonArray active;for(const auto& [a,b]:session->activeSegments())active.append(QJsonObject{{"start",QJsonArray{a.x,a.y}},{"end",QJsonArray{b.x,b.y}}});result["activeSegments"]=active;session->endDrag(false);
    QJsonArray drafts,movedIds;const auto changed=session->changedDrafts();for(const auto& draft:changed){movedIds.append(QString::fromStdString(draft.owner.id));drafts.append(QJsonObject{{"id",QString::fromStdString(draft.owner.id)},{"geometry",encodeGeometry(draft.geometry)}});}result["move"]=QJsonObject{{"changed",moved},{"movedOwnerIds",movedIds},{"features",drafts},{"coordinate",coordinate}};if(changed.size()<2)return result;
    JobScheduler jobs;const auto ticket=jobs.enqueue(snapshot,"boundary-helper-probe");jobs.takeNext();const auto receipt=calculateBoundaryGeometryPreview(snapshot,SharedBoundaryIntent{changed},territorialPreviewCalculators(),ticket.token());QJsonArray rows,reparented,removed,impacts;
    for(const auto& row:receipt.rows)rows.append(QJsonObject{{"id",QString::fromStdString(row.owner.id)},{"before",encodeGeometry(row.before)},{"after",row.after?QJsonValue(encodeGeometry(*row.after)):QJsonValue(QJsonValue::Null)}});
    for(const auto& relation:receipt.reparented)reparented.append(QJsonObject{{"id",QString::fromStdString(relation.owner.id)},{"from",QString::fromStdString(relation.from.id)},{"to",QString::fromStdString(relation.to.id)}});for(const auto& ref:receipt.patch().removedGeometryOwners)removed.append(QString::fromStdString(ref.id));if(receipt.plan())for(const auto& impact:receipt.plan()->impacts)impacts.append(QJsonObject{{"id",QString::fromStdString(impact.target.id)},{"kind",QString::fromStdString(impact.kind)},{"messageKey",QString::fromStdString(impact.messageKey)}});
    QJsonObject preview{{"ok",receipt.ok()},{"error",QString::fromStdString(receipt.detail)},{"rows",rows},{"reparented",reparented},{"removedIds",removed},{"impacts",impacts}};
    if(receipt.ok()){auto prepared=prepareBoundaryGeometryCommit(snapshot,receipt,ticket.token());preview["prepareOk"]=prepared.ok();preview["prepareError"]=QString::fromStdString(prepared.detail);if(prepared.preview){preview["entities"]=entityRows(prepared.preview->change().after());Project candidate;candidate.replace(prepared.preview->change().after());preview["nativeCanonicalDocument"]=QJsonDocument::fromJson(projectcodec::encode(candidate)).object();}}
    result["preview"]=preview;return result;
}
QJsonObject state(EditorController& controller,const QByteArray& before){
    const auto bytes=controller.documentBytes();const auto document=projectcodec::decode(bytes);QJsonArray entities,refs,hidden,styles,labels,entries;
    entities=entityRows(document);
    for(const auto& entry:document.distributionEntries){QJsonObject row{{"id",QString::fromStdString(entry.id)},{"layerId",QString::fromStdString(entry.layerId)},{"value",entry.value}};if(entry.territory){row["territorialUnitId"]=QString::fromStdString(entry.territory->id);refs.append(QJsonObject{{"kind","distribution"},{"id",QString::fromStdString(entry.id)},{"target",QString::fromStdString(entry.territory->id)}});}entries.append(row);}
    for(const auto& [ref,settings]:document.presentation.webPresentation.labelSettings){refs.append(QJsonObject{{"kind","label-settings"},{"id",QString::fromStdString(ref.id)},{"target",QString::fromStdString(ref.id)}});labels.append(QJsonObject{{"id",QString::fromStdString(ref.id)},{"pinned",settings.pinned},{"collisionGroup",QString::fromStdString(settings.collisionGroup)}});}
    for(const auto& [group,ids]:document.presentation.webPresentation.hiddenItems)for(const auto& id:ids)hidden.append(QJsonObject{{"group",QString::fromStdString(group)},{"id",QString::fromStdString(id)}});
    for(const auto& [key,style]:document.presentation.webPresentation.objectStyles){QJsonObject row{{"key",QString::fromStdString(key)}};if(style.opacity)row["opacity"]=*style.opacity;styles.append(row);}
    return {{"document",QJsonObject{{"entities",entities},{"distributionEntries",entries}}},{"references",refs},{"presentation",QJsonObject{{"hiddenItems",hidden},{"objectStyles",styles},{"labelSettings",labels}}},{"history",QJsonObject{{"canUndo",controller.canUndo()},{"canRedo",controller.canRedo()}}},{"nativeCanonicalDocument",QJsonDocument::fromJson(bytes).object()},{"canonicalBytesBase64",QString::fromLatin1(bytes.toBase64())},{"documentSha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())},{"unchangedFromBefore",bytes==before}};
}
QJsonObject observe(EditorController& controller,const QByteArray& before,const QJsonObject& outcome){return {{"observed",true},{"state",state(controller,before)},{"outcome",outcome},{"edit",QJsonObject::fromVariantMap(controller.geometryEditState())},{"draftPaths",QJsonArray::fromVariantList(controller.geometryDraftPaths())},{"selection",QJsonObject::fromVariantMap(controller.primaryObject())},{"selectionItems",QJsonArray::fromVariantList(controller.selectionItems())},{"revision",double(controller.revision())}};}
QJsonObject run(const QJsonObject& definition){
    QJsonObject output{{"case",definition["id"]},{"input",definition},{"entrypoint","EditorController public selection → beginSharedBoundaryGeometry → geometryBeginVertexDrag/geometryMoveSelectedVertex/geometryEndVertexDrag → prepareTerritorialGeometry → geometryBack/confirmGeometryEdit → undo/redo"}};
    QJsonObject stages;for(const auto* name:{"before","cold","pending","prepared","drag","preview","cancel","impactCancel","confirm","undo","redo","settled"})stages[name]=unobserved("The actual native workflow did not reach this stage.");
    QJsonArray inputObservations,selectionRequests,events,replayGestures;
    output["observationLimits"]=QJsonObject{{"browserPointerProjection",false},{"nativeProjectionInput","Exact public MapProjection inverse preimage within four ULPs; absent preimages remain unobserved."},{"nativeRawPreviewGeometry",false},{"nativeRawPreviewGeometryReason","Public controller exposes projected preview paths; complete raw canonical geometry is observed on confirm/undo/redo."},{"completedWorkerOwnerDeliveryWithheld",false},{"workerResultInterception",false},{"workerResultInterceptionReason","The probe does not intercept worker payloads. Applicable async cases wait for the actual global pool to finish while withholding owner-thread event delivery."},{"labelSettingVisible",false},{"labelSettingVisibleReason","Native LabelSettings has no visible property. Reference existence and every supported native field are recorded; web visible:false is not fabricated."},{"historyDepth",false},{"historyDepthReason","Public canUndo/canRedo and exact canonical undo/redo are observed; private numeric history depth is not fabricated."}};
    try {
        QTemporaryDir directory;require(directory.isValid(),"Temporary directory unavailable");Project source;source.replace(fixture(definition));const auto original=source.document();MapProjection projection;projection.rebuild(original);QFile file(directory.filePath("input.json"));require(file.open(QIODevice::WriteOnly),"Fixture file unavailable");file.write(projectcodec::encode(source));file.close();
        EditorController controller({false,directory.filePath("private.json")});require(controller.openFile(QUrl::fromLocalFile(file.fileName())),"Native fixture failed to open");
        QJsonArray requested=definition["selectedRefs"].toArray();if(requested.empty())for(const auto& id:definition["selectedIds"].toArray())requested.append(QJsonObject{{"domain","territorial"},{"id",id}});
        QVariantList nativeRefs;for(const auto& item:requested){const auto ref=item.toObject();nativeRefs.append(QVariantMap{{"domain",ref["domain"].toString()},{"id",ref["id"].toString()}});}
        const QVariantMap primary{{"domain","territorial"},{"id",definition["seedId"].toString()}};const bool selectionAccepted=controller.setSelection(nativeRefs,primary);selectionRequests.append(QJsonObject{{"requested",requested},{"nativeRefs",QJsonArray::fromVariantList(nativeRefs)},{"primary",QJsonObject::fromVariantMap(primary)},{"accepted",selectionAccepted}});
        const auto before=controller.documentBytes();const auto observeStage=[&](const QString& name,const QJsonObject& outcome){stages[name]=observe(controller,before,outcome);};
        QObject::connect(&controller,&EditorController::geometryEditChanged,&controller,[&]{const auto s=controller.geometryEditState();events.append(QJsonObject{{"active",s.value("active").toBool()},{"boundaryStatus",s.value("boundaryStatus").toString()},{"calculating",s.value("calculating").toBool()},{"previewReady",s.value("previewReady").toBool()},{"boundaryImpactConfirmation",s.value("boundaryImpactConfirmation").toBool()}});});
        observeStage("before",{});observeStage("cold",{{"canEnter",controller.canBeginSharedBoundaryGeometry()}});const bool entered=controller.beginSharedBoundaryGeometry();output["entry"]=QJsonObject{{"ok",entered}};observeStage("pending",{{"ok",entered}});
        std::vector<ObjectRef> helperOwners;const auto helperScope=definition["entryMode"]=="auto-child"?controller.geometryEditState().value("targets").toList():controller.selectionItems();for(const auto& value:helperScope){const auto ref=value.toMap();helperOwners.push_back({ref.value("domain").toString().toStdString(),ref.value("id").toString().toStdString()});}
        const auto scenario=definition["scenario"].toString();const auto other=definition["features"].toArray().last().toObject()["id"].toString();
        const auto holdCompletedDelivery=[&] {
            require(controller.geometryEditState().value("calculating").toBool(),"Expected an actual pending boundary worker");
            // QtConcurrent::run uses this existing pool. waitForDone does not
            // process owner-thread events, so QFutureWatcher completion remains
            // queued until the real public cancellation/selection action below.
            require(QThreadPool::globalInstance()->waitForDone(30000),"Actual boundary worker did not finish within the bounded delivery barrier");
            require(controller.geometryEditState().value("calculating").toBool(),"Worker result was delivered before the cancellation barrier");
            require(controller.documentBytes()==before,"Worker mutated canonical state before owner delivery");
            output["deliveryBarrier"]=QJsonObject{{"mechanism","native-worker-completed-owner-delivery-withheld"},{"timeoutMs",30000},{"completed",true},{"ownerEventsProcessed",false}};
            auto limits=output["observationLimits"].toObject();limits["completedWorkerOwnerDeliveryWithheld"]=true;output["observationLimits"]=limits;
            observeStage("delayed",{{"ok",true},{"workerCompleted",true},{"ownerDeliveryWithheld",true}});
        };
        if(entered&&(scenario=="pending-cancel"||scenario=="stale-preparation")){holdCompletedDelivery();if(scenario=="pending-cancel")controller.cancelGeometryEdit();else controller.selectCountry(other);settle(controller);drain();observeStage("settled",{{"ok",controller.geometryEditState().value("boundaryStatus").toString()=="ready"},{"trigger",scenario=="pending-cancel"?"cancelGeometryEdit before owner delivery":"selection revision change before owner delivery"}});}
        else if(!entered){observeStage("settled",{{"ok",false}});}
        else {
            settle(controller);const bool prepared=controller.geometryEditState().value("boundaryStatus").toString()=="ready";observeStage("prepared",{{"ok",prepared}});output["topologyHelper"]=topologyHelper(source.snapshot(),helperOwners,definition);
            if(!prepared)observeStage("settled",{{"ok",false}});
            else {
                QVariantMap node;for(const auto& path:controller.geometryDraftPaths())for(const auto& vertex:path.toMap().value("vertices").toList())if(vertex.toMap().value("nodeKey").toString()==definition["move"].toObject()["nodeKey"].toString())node=vertex.toMap();
                output["requestedHandle"]=QJsonObject::fromVariantMap(node);const bool picked=!node.empty()&&controller.geometrySelectNearest(node.value("x").toDouble(),node.value("y").toDouble(),.001);const bool began=picked&&controller.geometryBeginVertexDrag();output["gesture"]=QJsonObject{{"ok",began},{"picked",picked}};
                if(!began)observeStage("settled",{{"ok",false}});
                else {
                    const auto coordinate=definition["move"].toObject()["coordinate"].toArray();const Point intended{coordinate[0].toDouble(),coordinate[1].toDouble()};const auto screen=exactScreen(projection,intended);
                    if(!screen){stages["drag"]=unobserved("No exact public native projection preimage within four ULPs for the source fixture coordinate.");controller.geometryEndVertexDrag(true);observeStage("settled",{{"ok",false},{"inputObserved",false}});}
                    else {
                        const auto inverse=projection.unproject(screen->chosen.x,screen->chosen.y);inputObservations.append(QJsonObject{{"intended",coordinate},{"naive",QJsonArray{screen->naive.x,screen->naive.y}},{"chosen",QJsonArray{screen->chosen.x,screen->chosen.y}},{"inverse",QJsonArray{inverse.x,inverse.y}},{"xUlpSteps",screen->dx},{"yUlpSteps",screen->dy},{"maximumUlpRadius",4},{"exact",inverse.x==intended.x&&inverse.y==intended.y}});
                        const bool moved=controller.geometryMoveSelectedVertex(screen->chosen.x,screen->chosen.y,0,QString());observeStage("drag",{{"ok",true},{"changed",moved},{"coordinate",coordinate}});controller.geometryEndVertexDrag(false);
                        if(!scenario.isEmpty()) {
                            if(scenario=="stale-move")stages["delayed"]=unobserved("Native boundary-move is synchronous; no completed boundary-move worker reply exists to withhold.");
                            else holdCompletedDelivery();
                            if(scenario=="pending-preview-cancel")controller.cancelGeometryEdit();else controller.selectCountry(other);settle(controller);drain();observeStage("settled",{{"ok",controller.geometryEditState().value("previewReady").toBool()},{"trigger",scenario=="pending-preview-cancel"?"cancelGeometryEdit before preview delivery":"selection revision change before preview delivery"}});if(scenario=="stale-move")output["scenarioLimit"]="Native node fan-out is synchronous; asynchronous web boundary-move response interception has no native counterpart.";
                        } else {
                            settle(controller);const bool preview=controller.geometryEditState().value("previewReady").toBool();
                            if(!preview){stages["preview"]=unobserved("The actual controller created no canonical preview for this move.");observeStage("settled",{{"ok",false}});}
                            else {
                                const auto replayOriginalGesture=[&](const QString& afterCancel) {
                                    const auto originalKey=definition["move"].toObject()["nodeKey"].toString();QVariantMap originalNode;
                                    const auto pathsBefore=controller.geometryDraftPaths();for(const auto& path:pathsBefore)for(const auto& vertex:path.toMap().value("vertices").toList())if(vertex.toMap().value("nodeKey").toString()==originalKey)originalNode=vertex.toMap();
                                    QJsonObject attempt{{"afterCancel",afterCancel},{"requestedNodeKey",originalKey},{"foundOriginalHandle",!originalNode.empty()},{"handle",QJsonObject::fromVariantMap(originalNode)},{"before",observe(controller,before,{})},{"coordinate",coordinate},{"screen",QJsonArray{screen->chosen.x,screen->chosen.y}}};
                                    const bool selected=!originalNode.empty()&&controller.geometrySelectNearest(originalNode.value("x").toDouble(),originalNode.value("y").toDouble(),.001);const bool beganAgain=selected&&controller.geometryBeginVertexDrag();
                                    const bool movedAgain=beganAgain&&controller.geometryMoveSelectedVertex(screen->chosen.x,screen->chosen.y,0,QString());if(beganAgain)controller.geometryEndVertexDrag(false);settle(controller);
                                    const bool ready=controller.geometryEditState().value("previewReady").toBool();attempt["picked"]=selected;attempt["began"]=beganAgain;attempt["moved"]=movedAgain;attempt["automaticPreviewReady"]=ready;attempt["after"]=observe(controller,before,{{"ok",ready}});replayGestures.append(attempt);
                                    require(selected,"Cancellation replay could not select the original source nodeKey");require(beganAgain,"Cancellation replay could not begin the original-node gesture");require(movedAgain,"Cancellation replay did not move from the original prepared geometry");require(ready,"Cancellation replay did not automatically prepare a preview on release");
                                };
                                observeStage("preview",{{"ok",true}});const bool cancelled=controller.geometryBack();observeStage("cancel",{{"ok",cancelled}});require(cancelled,"Actual native preview cancellation failed");
                                replayOriginalGesture("cancel");
                                if(definition["impactCancel"].toBool()) {
                                    const bool unexpectedCommit=controller.confirmGeometryEdit();const bool confirmation=controller.geometryEditState().value("boundaryImpactConfirmation").toBool();bool declined=false;if(confirmation){require(QMetaObject::invokeMethod(&controller,"geometryCancelBoundaryImpacts"),"Missing actual native impact-cancel method");declined=!controller.geometryEditState().value("previewReady").toBool()&&!controller.geometryEditState().value("boundaryImpactConfirmation").toBool();}observeStage("impactCancel",{{"ok",unexpectedCommit},{"confirmationShown",confirmation},{"cancelled",declined}});
                                    require(!unexpectedCommit,"Impact cancel unexpectedly committed before the decline decision");require(confirmation&&declined,"Actual native impact cancellation failed");replayOriginalGesture("impactCancel");
                                }
                                bool confirmed=controller.confirmGeometryEdit();if(controller.geometryEditState().value("boundaryImpactConfirmation").toBool())require(QMetaObject::invokeMethod(&controller,"geometryConfirmBoundaryImpacts",Q_RETURN_ARG(bool,confirmed)),"Missing actual native impact-confirm method");settle(controller);observeStage("confirm",{{"ok",confirmed}});
                                if(confirmed){const bool undo=controller.canUndo();controller.undo();observeStage("undo",{{"ok",undo}});const bool redo=controller.canRedo();controller.redo();observeStage("redo",{{"ok",redo}});}observeStage("settled",{{"ok",confirmed}});
                            }
                        }
                    }
                }
            }
        }
        controller.cancelGeometryEdit();drain();
    } catch(const std::exception& error){output["error"]=QString::fromUtf8(error.what());}
    output["stages"]=stages;output["inputObservations"]=inputObservations;output["selectionRequests"]=selectionRequests;output["events"]=events;output["replayGestures"]=replayGestures;return output;
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{require(argc==1,"Unexpected native boundary probe argument");QFile input;require(input.open(stdin,QIODevice::ReadOnly),"Probe stdin unavailable");QJsonParseError error;const auto payload=QJsonDocument::fromJson(input.readAll(),&error);require(error.error==QJsonParseError::NoError,"Invalid probe JSON");const auto cases=payload.isArray()?payload.array():payload.object()["cases"].toArray();require(!cases.empty(),"Empty boundary corpus");QJsonArray rows;for(const auto& definition:cases)rows.append(run(definition.toObject()));const auto bytes=QJsonDocument(QJsonObject{{"schema","pando-m974-native-boundary-workflows"},{"version",1},{"rows",rows}}).toJson(QJsonDocument::Compact);std::fwrite(bytes.constData(),1,bytes.size(),stdout);return 0;}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
