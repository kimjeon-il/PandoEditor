#include "editorcontroller.h"
#include "projectcodec.h"
#include "territorial_fixture.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEvent>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <cmath>
#include <cstdio>
#include <stdexcept>

using namespace pandoeditor;
namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
QString sha256(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
QJsonObject unavailable(const QString& reason) {
    return {{"observed",false},{"reason",reason}};
}
double number(const QJsonValue& value) {
    require(value.isDouble()&&std::isfinite(value.toDouble()),"Missing finite corpus number");
    return value.toDouble();
}
QJsonArray vector(const QJsonObject& object,const char* key,int size) {
    require(object[key].isArray(),"Missing corpus vector");
    const auto result=object[key].toArray();
    require(result.size()==size,"Invalid corpus vector size");
    for(const auto value:result)number(value);
    return result;
}
ProjectDocument document(const QJsonObject& corpus) {
    ProjectDocument result;
    result.documentId="m977-pending-input-controller";
    for(const auto value:corpus["features"].toArray()) {
        const auto feature=value.toObject();
        const auto id=feature["id"].toString().toStdString();
        const auto bounds=vector(feature,"bounds",4);
        require(!id.empty(),"Missing source identity");
        const double x0=number(bounds[0]),y0=number(bounds[1]);
        const double x1=number(bounds[2]),y1=number(bounds[3]);
        require(x0<x1&&y0<y1,"Invalid source bounds");
        Geometry geometry;
        geometry.type="Polygon";
        geometry.polygons={{{{x0,y0},{x1,y0},{x1,y1},{x0,y1},{x0,y0}}}};
        const GeometryRef reference{id,1};
        result.geometries.insert(reference,std::move(geometry));
        appendTerritory(result,{id,id,"",UnitKind::General,false},reference);
        result.presentation.objectStyles[territorialRef(id)]={};
    }
    validateDocument(result);
    return result;
}
void configureView(EditorController& controller,const QJsonObject& definition) {
    require(definition["view"].isObject(),"Missing explicit native view");
    const auto view=definition["view"].toObject();
    const auto translate=vector(view,"translate",2),rotate=vector(view,"rotate",3),center=vector(view,"center",2);
    const auto size=view["size"].toObject(),snap=view["snapDistance"].toObject();
    require(number(snap["mouse"])==10&&number(snap["touch"])==18,"Unsupported snap policy");
    require(view["coarsePointer"].isBool()&&view["coarsePointer"].toBool()==controller.mobileMode(),"Pointer profile mismatch");
    require(controller.setProjectionMode(view["kind"].toString()),"Projection rejected");
    require(controller.publishMapView({{"scale",number(view["scale"])},
        {"translateX",number(translate[0])},{"translateY",number(translate[1])},
        {"rotationLongitude",number(rotate[0])},{"rotationLatitude",number(rotate[1])},{"rotationRoll",number(rotate[2])},
        {"centerLongitude",number(center[0])},{"centerLatitude",number(center[1])},
        {"viewportWidth",number(size["width"])},{"viewportHeight",number(size["height"])}}),"Explicit native view rejected");
}
void settle(EditorController& controller) {
    QElapsedTimer timeout;timeout.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents,10);
        if(!controller.geometryEditState().value("calculating").toBool())return;
        QThread::msleep(2);
    } while(timeout.elapsed()<10000);
    throw std::runtime_error("Actual native selection did not settle");
}
void deliverCompletedEvents(EditorController& controller) {
    require(QThreadPool::globalInstance()->waitForDone(30000),"Native pool did not drain");
    // A held worker has already completed. Pump its posted QFutureWatcher
    // notification and the cancellation/pump MetaCalls; never inject a result.
    QCoreApplication::sendPostedEvents();
    settle(controller);
    require(QThreadPool::globalInstance()->waitForDone(30000),"Replacement native pool did not drain");
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents(QEventLoop::AllEvents,10);
    settle(controller);
}
QJsonObject observation(EditorController& controller,const QByteArray& baseline,const MapProjection& projection,const QJsonObject& outcome) {
    const auto state=controller.geometryEditState();
    const auto paths=controller.geometryDraftPaths();
    const auto owned=controller.riverSelectionObservation();
    const bool active=state.value("active").toBool();
    const bool rawLine=active&&state.value("activeMethod").toString()=="line";
    QJsonArray coordinates,projectedVertices;
    if(rawLine)coordinates=QJsonArray::fromVariantList(owned.value("inputLine").toList());
    for(const auto value:paths) {
        const auto path=value.toMap();
        for(const auto vertexValue:path.value("vertices").toList()) {
            const auto vertex=vertexValue.toMap();
            const double x=vertex.value("x").toDouble(),y=vertex.value("y").toDouble();
            projectedVertices.append(QJsonArray{x,y});
            if(!rawLine) {
                const auto point=projection.unproject(x,y);
                coordinates.append(QJsonArray{point.x,point.y});
            }
        }
    }
    const auto confirmationKind=state.value("confirmationKind").toString();
    const auto bytes=controller.documentBytes();
    return {{"observed",true},{"state",QJsonObject::fromVariantMap(state)},
        {"method",active?QJsonValue(state.value("activeMethod").toString()):QJsonValue::Null},
        {"stage",active?QJsonValue(state.value("stage").toString()):QJsonValue::Null},
        {"phase",active?QJsonValue(state.value("selectionPhase").toString()):QJsonValue::Null},
        {"pending",state.value("selectionPending").toBool()},
        {"confirmation",confirmationKind.isEmpty()?QJsonValue::Null:QJsonValue(QJsonObject{{"kind",confirmationKind},{"requestedMethod",state.value("requestedMethod").toString()}})},
        {"coordinates",coordinates},{"coordinateObservation",QJsonObject{{"raw",rawLine},{"mechanism",rawLine?"riverSelectionObservation.inputLine":"geometryDraftPaths.vertices + MapProjection.unproject"},{"projectedVertices",projectedVertices},{"roundTripCanLosePrecision",!rawLine}}},
        {"draftUndoAvailable",state.value("canUndoDraft").toBool()},{"draftRedoAvailable",state.value("canRedo").toBool()},
        {"projectCanUndo",controller.canUndo()},{"projectCanRedo",controller.canRedo()},
        {"revision",double(controller.revision())},{"canonicalBytesBase64",QString::fromLatin1(bytes.toBase64())},
        {"canonicalSha256",sha256(bytes)},{"unchangedFromBefore",bytes==baseline},
        {"draftPaths",QJsonArray::fromVariantList(paths)},{"selectionObservation",QJsonObject::fromVariantMap(owned)},
        {"mapViewState",QJsonObject::fromVariantMap(controller.mapViewState())},
        {"snapState",QJsonObject::fromVariantMap(controller.geometrySnapState())},{"outcome",outcome}};
}
QJsonObject run(const QJsonObject& corpus,const QJsonObject& definition) {
    QTemporaryDir directory;require(directory.isValid(),"Temporary fixture directory unavailable");
    Project source;source.replace(document(corpus));MapProjection projection;projection.rebuild(source.document());
    QFile file(directory.filePath("input.json"));require(file.open(QIODevice::WriteOnly),"Fixture file unavailable");
    file.write(projectcodec::encode(source));file.close();
    const auto profile=definition["profile"].toObject();
    const bool mobile=definition["view"].toObject()["coarsePointer"].toBool();
    EditorController controller({mobile,directory.filePath("private.json")});
    require(controller.openFile(QUrl::fromLocalFile(file.fileName())),"Native fixture failed to open");
    controller.selectCountry("target");configureView(controller,definition);
    require(projection.hydroParameters()==controller.hydroProjection(),"Public probe/controller projection must agree");
    const auto baseline=controller.documentBytes();const auto baselineRevision=controller.revision();
    QJsonArray events,inputObservations;QJsonObject stages;
    for(const auto name:definition["stages"].toArray()) {
        require(name.isString()&&!name.toString().isEmpty()&&!stages.contains(name.toString()),"Invalid or duplicate stage");
        stages[name.toString()]=unavailable("The native workflow did not reach this requested stage.");
    }
    QJsonObject result{{"case",definition["id"]},{"input",definition},
        {"projectionParameters",QJsonObject::fromVariantMap(projection.hydroParameters())},
        {"corpusInput",QJsonObject{{"features",corpus["features"]},{"points",corpus["points"]}}},
        {"baseline",QJsonObject{{"canonicalBytesBase64",QString::fromLatin1(baseline.toBase64())},{"canonicalSha256",sha256(baseline)},{"revision",double(baselineRevision)},{"projectCanUndo",controller.canUndo()},{"projectCanRedo",controller.canRedo()}}},
        {"limits",QJsonObject{{"rawPolygonDraftCoordinates",unavailable("The public controller exposes projected polygon vertices; inverse-projected observations retain IEEE round-trip error without rounding.")},
            {"historyDepth",unavailable("Only public draft and project Undo/Redo availability is exposed; no numeric depth is inferred.")},
            {"requestAndEpochIds",unavailable("No public selection request identity, computation epoch, or generation observer exists.")},
            {"privateWorkerPayload",unavailable("Only actual worker completion with owner delivery withheld is observed; payloads are never intercepted or synthesized.")},
            {"browserOrNativePointerDispatch",unavailable("This native route invokes geometryAddPoint with projected map coordinates and explicit pointer type; it does not dispatch GUI mouse/touch events.")},
            {"projectHistoryReplay",unavailable("This slice never Applies. Project Undo/Redo are not invoked; canonical bytes, revision and availability must remain unchanged.")}}}};
    QObject::connect(&controller,&EditorController::geometryEditChanged,&controller,[&] {
        const auto state=controller.geometryEditState();
        events.append(QJsonObject{{"sequence",events.size()},{"state",QJsonObject::fromVariantMap(state)}});
    });
    auto observe=[&](const QString& stage,const QJsonObject& outcome=QJsonObject{}) {
        require(stages.contains(stage),"Unlisted native stage");
        stages[stage]=observation(controller,baseline,projection,outcome);
    };
    auto tap=[&](const QString& stage) {
        const auto declaredIndex=corpus["tapPointIndexes"].toObject()[stage];
        require(declaredIndex.isDouble()&&declaredIndex.toDouble()==declaredIndex.toInt(),"Missing declared tap index");
        const int index=declaredIndex.toInt();
        const auto points=corpus["points"].toArray();require(index>=0&&index<points.size(),"Missing tap coordinate");
        const auto xy=points[index].toArray();require(xy.size()==2,"Invalid tap coordinate");
        const Point intended{number(xy[0]),number(xy[1])};const auto projected=projection.project(intended);
        const auto inverse=projection.unproject(projected.x,projected.y);
        const bool accepted=controller.geometryAddPoint(projected.x,projected.y,0,profile["pointerType"].toString());
        inputObservations.append(QJsonObject{{"stage",stage},{"intended",xy},{"mapCoordinate",QJsonArray{projected.x,projected.y}},
            {"inverseBeforeInput",QJsonArray{inverse.x,inverse.y}},{"exactInputRoundTrip",inverse.x==intended.x&&inverse.y==intended.y},
            {"pointerType",profile["pointerType"]},{"tolerance",0},{"accepted",accepted}});
        observe(stage,{{"action","geometryAddPoint"},{"accepted",accepted}});
    };
    auto hold=[&] {
        require(controller.geometryEditState().value("selectionPending").toBool(),"Selection must be pending before dispatch");
        const auto eventCount=events.size();
        // QTimer::singleShot(0, controller, ...) is a queued context MetaCall.
        // Deliver only the controller's queued call. The QFutureWatcher and
        // CommandJobRunner have different receivers, so their completion is
        // withheld even when the worker is already finished. Require the real
        // timer-tail public signal; a missing dispatch cannot become evidence.
        QCoreApplication::sendPostedEvents(&controller,QEvent::MetaCall);
        const auto emitted=events.size()-eventCount;
        require(emitted==1,"Targeted native selection timer dispatch not observed exactly once");
        require(controller.geometryEditState().value("selectionPending").toBool(),"Native result delivered before barrier");
        require(QThreadPool::globalInstance()->waitForDone(30000),"Actual native selection worker did not complete");
        require(controller.geometryEditState().value("selectionPending").toBool(),"Native completion was adopted during barrier");
        require(controller.documentBytes()==baseline,"Worker changed canonical bytes before owner delivery");
        QJsonObject barrier{{"mechanism","controller-zero-timer-dispatched-worker-completed-owner-delivery-withheld"},
            {"targetedControllerMetaCalls",true},{"timerTailSignals",emitted},{"globalPoolCompleted",true},
            {"ownerCompletionEventsProcessed",false},{"selectionPending",true},{"timeoutMs",30000}};
        result["deliveryBarrier"]=barrier;observe("held",barrier);
    };
    try {
        require(controller.beginAnnexGeometry(),"Native annex entry rejected");
        require(controller.geometryToggleProvider({{"domain","territorial"},{"id","donor"}}),"Native source rejected");
        settle(controller);require(controller.geometryAdvanceStage(),"Native selection advance rejected");
        settle(controller);
        require(controller.geometrySelectTerritoryMethod("polygon"),"Native polygon activation rejected");
        observe("activation",{{"action","geometrySelectTerritoryMethod"},{"method","polygon"},{"accepted",true},
            {"boundary","selectionPending before zero-timer dispatch"}});
        const auto scenario=definition["scenario"].toString();
        if(scenario=="pending-switch") {
            const bool switched=controller.geometrySelectTerritoryMethod("line");
            observe("switchPending",{{"action","geometrySelectTerritoryMethod"},{"method","line"},{"accepted",switched},
                {"boundary","both selection zero-timers still undelivered"}});
            deliverCompletedEvents(controller);observe("afterLateCompletion");
        } else {
            if(scenario=="two-pending-empty-switch"||scenario=="pending-ready-switch-history")tap("firstPending");
            if(scenario=="two-pending-empty-switch")tap("secondPending");
            hold();
            if(scenario.startsWith("held-")) {
                bool accepted=true;QString action;
                if(scenario=="held-clear") {action="cancelGeometryEdit";controller.cancelGeometryEdit();}
                else if(scenario=="held-back") {action="geometryBack";accepted=controller.geometryBack();}
                else if(scenario=="held-supersede") {action="geometrySelectTerritoryMethod(line)";accepted=controller.geometrySelectTerritoryMethod("line");}
                else throw std::runtime_error("Unknown held-result scenario");
                observe("afterAction",{{"action",action},{"accepted",accepted}});
                deliverCompletedEvents(controller);observe("afterLateCompletion");
                if(scenario=="held-back")tap("tapAfterBack");
            } else {
                deliverCompletedEvents(controller);observe("ready");
                if(scenario=="pending-ready-switch-history"||scenario=="ready-same-method") {
                    tap("firstReady");tap("secondReady");
                }
                if(scenario=="ready-same-method") {
                    const bool accepted=controller.geometrySelectTerritoryMethod("polygon");
                    observe("sameMethod",{{"action","geometrySelectTerritoryMethod"},{"method","polygon"},{"accepted",accepted}});
                } else {
                    require(scenario=="two-pending-empty-switch"||scenario=="pending-ready-switch-history","Unknown pending-input scenario");
                    if(scenario=="pending-ready-switch-history") {
                        const bool undone=controller.geometryUndoDraft();observe("undoDraft",{{"action","geometryUndoDraft"},{"accepted",undone}});
                        const bool redone=controller.geometryRedoDraft();observe("redoDraft",{{"action","geometryRedoDraft"},{"accepted",redone}});
                    }
                    const bool switched=controller.geometrySelectTerritoryMethod("line");
                    observe("switch",{{"action","geometrySelectTerritoryMethod"},{"method","line"},{"accepted",switched}});
                    if(controller.geometryEditState().value("confirmationKind").toString()=="method") {
                        const bool cancelled=controller.geometryCancelTerritoryChange();
                        observe("cancelSwitch",{{"action","geometryCancelTerritoryChange"},{"accepted",cancelled}});
                        const bool switchedAgain=controller.geometrySelectTerritoryMethod("line");
                        observe("switchAgain",{{"action","geometrySelectTerritoryMethod"},{"method","line"},{"accepted",switchedAgain}});
                        if(controller.geometryEditState().value("confirmationKind").toString()=="method") {
                            const bool confirmed=controller.geometryConfirmTerritoryChange();
                            observe("confirmSwitch",{{"action","geometryConfirmTerritoryChange"},{"accepted",confirmed}});
                        } else stages["confirmSwitch"]=unavailable("The second native method request did not open confirmation; no confirmation action was invoked.");
                    } else {
                        for(const auto* name:{"cancelSwitch","switchAgain","confirmSwitch"})
                            stages[name]=unavailable("The native empty-ready method switch opened no confirmation; dependent cancel/re-request/confirm actions were not invoked.");
                    }
                }
            }
        }
        require(controller.documentBytes()==baseline&&controller.revision()==baselineRevision,"Pending-input slice mutated canonical state");
        require(!controller.canUndo()&&!controller.canRedo(),"Pending-input slice changed project history");
    } catch(const std::exception& error) {result["error"]=QString::fromUtf8(error.what());}
    controller.cancelGeometryEdit();deliverCompletedEvents(controller);
    result["stages"]=stages;result["events"]=events;result["inputObservations"]=inputObservations;
    return result;
}
}
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    try {
        require(argc==2,"Usage: m977_pending_input_probe corpus.json");
        QFile file(QString::fromLocal8Bit(argv[1]));require(file.open(QIODevice::ReadOnly),"Corpus file unavailable");
        const auto bytes=file.readAll();QJsonParseError error;
        const auto input=QJsonDocument::fromJson(bytes,&error);
        require(error.error==QJsonParseError::NoError&&input.isObject(),"Invalid pending-input corpus JSON");
        const auto corpus=input.object();require(corpus["schema"]=="pando-m977-pending-input-corpus"&&corpus["version"]==1,"Unsupported corpus schema");
        const auto definitions=corpus["cases"].toArray();require(!definitions.empty(),"Empty pending-input corpus");
        QJsonArray cases;for(const auto value:definitions)cases.append(run(corpus,value.toObject()));
        const auto output=QJsonDocument(QJsonObject{{"schema","pando-m977-native-pending-input"},{"version",1},
            {"runtime",QJsonObject{{"qt",qVersion()}}},{"corpusSha256",sha256(bytes)},{"cases",cases}}).toJson(QJsonDocument::Compact);
        std::fwrite(output.constData(),1,output.size(),stdout);return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"%s\n",error.what());return 1;}
}
