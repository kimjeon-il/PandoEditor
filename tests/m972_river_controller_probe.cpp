#include "territorial_fixture.h"
#include "editorcontroller.h"
#include "projectcodec.h"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QSaveFile>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <stdexcept>

using namespace pandoeditor;
namespace {
void require(bool condition,const QString& detail) {
    if(!condition)throw std::runtime_error(detail.toStdString());
}
QJsonObject readObject(const QString& path) {
    QFile file(path);require(file.open(QIODevice::ReadOnly),"EVIDENCE_INPUT_UNAVAILABLE: "+path);
    QJsonParseError error;const auto json=QJsonDocument::fromJson(file.readAll(),&error);
    require(error.error==QJsonParseError::NoError&&json.isObject(),"INVALID_EVIDENCE_JSON: "+path);return json.object();
}
Geometry decodeGeometry(const QJsonObject& value) {
    Geometry geometry;geometry.type=value["type"].toString().toStdString();
    require(geometry.type=="Polygon"||geometry.type=="MultiPolygon","INVALID_INPUT_GEOMETRY_TYPE");
    auto polygons=value["coordinates"].toArray();if(geometry.type=="Polygon")polygons=QJsonArray{polygons};
    for(const auto& p:polygons) {Polygon polygon;for(const auto& r:p.toArray()) {Ring ring;
        for(const auto& c:r.toArray()) {const auto point=c.toArray();require(point.size()==2,"INVALID_INPUT_COORDINATE");
            ring.push_back({point[0].toDouble(),point[1].toDouble()});}polygon.push_back(std::move(ring));}geometry.polygons.push_back(std::move(polygon));}
    return geometry;
}
QJsonObject encodeGeometry(const Geometry& geometry) {
    QJsonArray polygons;for(const auto& polygon:geometry.polygons) {QJsonArray rings;for(const auto& ring:polygon) {
        QJsonArray points;for(const auto point:ring)points.append(QJsonArray{point.x,point.y});rings.append(points);}polygons.append(rings);}
    return {{"type",QString::fromStdString(geometry.type)},{"coordinates",geometry.type=="Polygon"?polygons[0]:QJsonValue(polygons)}};
}
QJsonObject named(const QJsonArray& rows,const QString& name) {
    for(const auto& row:rows)if(row.toObject()["name"].toString()==name)return row.toObject();return {};
}
QJsonObject keyed(const QJsonArray& rows,const QString& key) {
    for(const auto& row:rows)if(row.toObject()["key"].toString()==key)return row.toObject();return {};
}
QJsonObject ownedObservation(EditorController& controller) {
    return QJsonObject::fromVariantMap(controller.riverSelectionObservation());
}
void progress(const QString& name,const QString& stage) {
    std::fprintf(stderr,"%s: %s\n",name.toUtf8().constData(),stage.toUtf8().constData());std::fflush(stderr);
}
void settle(EditorController& controller,const QString& name,const QString& stage,QJsonArray& timings) {
    progress(name,stage);QElapsedTimer timer;timer.start();qint64 stateNs=0;int observations=0;bool ready=false,expired=false;
    QEventLoop loop;QTimer deadline;deadline.setSingleShot(true);deadline.setInterval(120000);
    const auto observe=[&]{QElapsedTimer boundary;boundary.start();const auto state=controller.geometryEditState();stateNs+=boundary.nsecsElapsed();++observations;
        ready=!state["active"].toBool()||!state["calculating"].toBool();if(ready)loop.quit();};
    // Read the public state at actual production transitions, not every 2ms.
    // The deadline is unchanged. Qt's event loop remains free to deliver worker
    // completions, preview timers, cancellation and newer session requests.
    const auto connection=QObject::connect(&controller,&EditorController::geometryEditChanged,&loop,observe);
    QObject::connect(&deadline,&QTimer::timeout,&loop,[&]{expired=true;loop.quit();});observe();
    if(!ready){deadline.start();loop.exec();}deadline.stop();QObject::disconnect(connection);
    timings.append(QJsonObject{{"kind","settle"},{"stage",stage},{"elapsedMs",timer.elapsed()},{"stateObservationMs",double(stateNs)/1e6},{"stateObservations",observations},{"deadlineExpired",expired}});
    progress(name,QString("%1 settled in %2ms (%3 signal observations, state observation %4ms)").arg(stage).arg(timer.elapsed()).arg(observations).arg(double(stateNs)/1e6));
    if(ready&&!expired)return;
    throw std::runtime_error(("CONTROLLER_DID_NOT_SETTLE: "+stage+": "+QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(controller.geometryEditState())).toJson(QJsonDocument::Compact))).toStdString());
}
struct Checks {
    int matched=0,mismatched=0;
    QJsonArray differences,unobservedFields;
    bool equal(const QString& name,const QJsonValue& actual,const QJsonValue& expected) {
        if(actual==expected){++matched;return true;}
        ++mismatched;differences.append(QJsonObject{{"field",name},{"actual",actual},{"expected",expected}});return false;
    }
    bool truth(const QString& name,bool value) {return equal(name,value,true);}
    void unobserved(const QString& name,const QJsonValue& actual) {
        unobservedFields.append(QJsonObject{{"field",name},{"actual",actual},{"reason","Actual browser workflow did not produce this value at this checkpoint."}});
    }
};
ProjectDocument worldDocument(const QJsonArray& features,const QString& manifest) {
    ProjectDocument document;document.documentId="m972-river-controller-browser-oracle";
    for(const auto& value:features) {const auto feature=value.toObject();const auto id=feature["id"].toString().toStdString();
        require(!id.empty(),"WORLD_FEATURE_WITHOUT_ID");const GeometryRef ref{id,1};document.geometries.insert(ref,decodeGeometry(feature["geometry"].toObject()));
        appendTerritory(document,{id,id,"",UnitKind::General,false},ref);document.presentation.objectStyles[territorialRef(id)]={};}
    document.physicalData.source=manifest.toStdString();
    // Presentation is deliberately hidden. Editing must still load the actual
    // full logical river source, independently of viewport/render visibility.
    document.presentation.webPresentation.visibility["rivers"]=false;
    document.presentation.webPresentation.visibility["lakes"]=false;
    validateDocument(document);return document;
}
QJsonArray documentFeatures(const QByteArray& bytes) {
    const auto document=projectcodec::decode(bytes);QJsonArray features;
    for(const auto& unit:document.units) {const auto geometry=document.geometries.get(staticGeometryBinding(document,unit.id).geometryRef);
        require(bool(geometry),"COMMITTED_FEATURE_WITHOUT_GEOMETRY");features.append(QJsonObject{{"id",QString::fromStdString(unit.id)},{"geometry",encodeGeometry(*geometry)}});}
    return features;
}
QJsonObject featureById(const QJsonArray& rows,const QString& id) {
    for(const auto& row:rows)if(row.toObject()["id"].toString()==id)return row.toObject();return {};
}
QStringList selectedKeys(const QJsonObject& observation) {
    QStringList keys;for(const auto& key:observation["state"].toObject()["selectedComponentKeys"].toArray())keys.append(key.toString());return keys;
}
QJsonArray keyArray(QStringList keys,bool sorted=false) {
    // Selection insertion order is the sole approved transient-ID comparison
    // mapping. Cell geometry and provenance retain their original raw order.
    if(sorted)keys.sort(Qt::CaseSensitive);QJsonArray values;for(const auto& key:keys)values.append(key);return values;
}
void compareComponents(Checks& checks,const QString& prefix,const QJsonArray& actual,const QJsonArray& expected) {
    checks.equal(prefix+"count",actual.size(),expected.size());
    for(int i=0;i<std::min(actual.size(),expected.size());++i) {
        const auto native=actual[i].toObject(),web=expected[i].toObject();const auto row=prefix+QString("[%1].").arg(i);
        for(const auto& field:{"key","countryId","componentKey","polygonIndex","sourcePolygonIndex","geometry"})checks.equal(row+field,native[field],web[field]);
        // Ordinary web prepare items omit these two optional river fields. The
        // native observation represents the same absent values as empty text
        // and an empty object. This convention never changes geometry or keys.
        checks.equal(row+"partitionKind",native["partitionKind"],web.contains("partitionKind")?web["partitionKind"]:QJsonValue(""));
        const auto provenance=QJsonDocument::fromJson(native["provenance"].toString().toUtf8()).object();
        auto expectedProvenance=web.contains("provenance")?web["provenance"].toObject():web;
        if(!web.contains("partitionKind"))expectedProvenance={};
        else if(!web.contains("provenance")) {
            // Installed web archive/snapshot items contain UI-only additions
            // outside the native owned provenance seam. Preserve all geometry
            // and kernel provenance fields; the original browser rows remain
            // present in the evidence and checkpoint report.
            for(const auto& field:{"countryName","areaKm2","usesRiverBoundary","selected","snapshotId"})expectedProvenance.remove(field);
        }
        checks.equal(row+"provenance",provenance,expectedProvenance);
    }
}
void compareComponentFeatures(Checks& checks,const QString& prefix,const QJsonArray& actual,const QJsonArray& expected) {
    checks.equal(prefix+"count",actual.size(),expected.size());
    for(int i=0;i<std::min(actual.size(),expected.size());++i) {
        const auto native=actual[i].toObject(),web=expected[i].toObject();const auto row=prefix+QString("[%1].").arg(i);
        checks.equal(row+"id",native["id"],web["id"]);checks.equal(row+"geometry",native["geometry"],web["geometry"]);
        checks.equal(row+"sourcePolygonIndices",native["sourcePolygonIndices"],web.contains("sourcePolygonIndices")?web["sourcePolygonIndices"]:web["properties"].toObject()["__territorySourceIndices"]);
    }
}
void compareLifecycleCheckpoint(Checks& checks,const QJsonObject& actual,const QJsonObject& expected) {
    const auto prefix="lifecycle."+expected["name"].toString()+".";const auto state=actual["state"].toObject();
    require(expected.contains("components")&&expected.contains("componentFeatures")&&expected.contains("parts")&&expected.contains("riverSliverContext"),"INCOMPLETE_BROWSER_LIFECYCLE_CHECKPOINT");
    compareComponents(checks,prefix+"components.",actual["components"].toArray(),expected["components"].toArray());
    compareComponentFeatures(checks,prefix+"componentFeatures.",actual["componentFeatures"].toArray(),expected["componentFeatures"].toArray());
    const auto nativeParts=actual["parts"].toArray(),webParts=expected["parts"].toArray(),stateParts=state["parts"].toArray();
    checks.equal(prefix+"parts.count",nativeParts.size(),webParts.size());
    for(int i=0;i<std::min(nativeParts.size(),webParts.size());++i) {
        const auto native=nativeParts[i].toObject(),web=webParts[i].toObject();const auto row=prefix+QString("parts[%1].").arg(i);
        // Part identities are generated by separate native/web sessions. The
        // approved ordered-part mapping is applied only to this identity; owned
        // geometry, archive method and component provenance are never remapped.
        checks.equal(row+"geometry",native["geometry"],web["geometry"]);
        checks.equal(row+"method",native.contains("method")?native["method"]:stateParts[i].toObject()["method"],web["method"]);
        if(web.contains("component")) {
            require(native.contains("component"),"CONTROLLER_ARCHIVED_COMPONENT_OBSERVATION_REQUIRED");
            compareComponents(checks,row+"component.",QJsonArray{native["component"]},QJsonArray{web["component"]});
        }
    }
    if(expected.contains("componentSnapshots")) {
        require(actual.contains("componentSnapshots"),"CONTROLLER_COMPONENT_SNAPSHOT_OBSERVATION_REQUIRED");
        const auto native=actual["componentSnapshots"].toArray(),web=expected["componentSnapshots"].toArray();
        checks.equal(prefix+"componentSnapshots.count",native.size(),web.size());
        for(int i=0;i<std::min(native.size(),web.size());++i)compareComponents(checks,prefix+QString("componentSnapshots[%1].items.").arg(i),native[i].toObject()["items"].toArray(),web[i].toObject()["items"].toArray());
    }
    for(const auto& field:{"combinedGeometry","workingSourceGeometry","riverSliverContext","transferredGeometry"}) {
        require(expected.contains(field),"INCOMPLETE_BROWSER_LIFECYCLE_GEOMETRY: "+QString(field));
        if(QString(field)=="riverSliverContext"&&expected[field].isNull())checks.unobserved(prefix+field,actual[field]);
        else checks.equal(prefix+field,actual.contains(field)?actual[field]:QJsonValue::Null,expected[field]);
    }
    for(const auto& field:{"selectedComponentKeys","previewReady","autoIncludedSliverCount","autoIncludedSliverAreaM2","transferAreaKm2"}) {
        require(expected.contains(field),"INCOMPLETE_BROWSER_LIFECYCLE_STATE: "+QString(field));
        if(expected[field].isNull()&&QString(field)!="previewReady"&&QString(field)!="selectedComponentKeys")checks.unobserved(prefix+field,state[field]);
        else checks.equal(prefix+field,state[field],expected[field]);
    }
    if(expected.contains("sourceDiagnostics")) {
        const auto native=actual["sourceDiagnostics"].toObject(),web=expected["sourceDiagnostics"].toObject();
        for(const auto& field:{"loadedRivers","failedLogicalIds","indexSha256","version"})checks.equal(prefix+"sourceDiagnostics."+field,native[field],web[field]);
        for(const auto& field:{"donorRevisionStrings","editedRiverSignature","hydroRevision"})checks.equal(prefix+field,actual[field],expected[field]);
    }
}
QJsonObject runCase(const QJsonObject& evidence,const QJsonObject& browser,const QJsonArray& world,const QString& manifest) {
    const auto name=evidence["name"].toString();const auto scenario=evidence["selection"].toObject();Checks checks;
    QJsonObject report{{"name",name},{"selection",scenario},{"observed",true}};QJsonArray checkpoints,timings;
    QTemporaryDir directory;require(directory.isValid(),"FIXTURE_DIRECTORY_FAILED");
    const auto document=worldDocument(world,manifest);Project project;project.replace(document);MapProjection projection;projection.rebuild(document);
    QFile input(directory.filePath("input.pando.json"));require(input.open(QIODevice::WriteOnly),"FIXTURE_WRITE_FAILED");
    input.write(projectcodec::encode(project));input.close();EditorController controller({false,directory.filePath("private.json")});
    const auto wait=[&](const QString& stage){settle(controller,name,stage,timings);};
    QByteArray before;qulonglong revision=0;bool hadUndo=false,hadRedo=false;
    const auto checkpoint=[&](const QString& stage) {QElapsedTimer timer;timer.start();auto observation=ownedObservation(controller);const auto ownedMs=timer.elapsed();timer.restart();const auto bytes=controller.documentBytes();const auto encodeMs=timer.elapsed();observation["stage"]=stage;
        const bool unchanged=bytes==before;observation["canonicalBytesUnchanged"]=unchanged;observation["revisionUnchanged"]=controller.revision()==revision;
        observation["historyUnchanged"]=controller.canUndo()==hadUndo&&controller.canRedo()==hadRedo;checkpoints.append(observation);
        // Reuse one exact canonical serialization for both the report and the
        // assertion at this same checkpoint. No geometry comparison is weakened.
        checks.truth(stage+".canonicalBytesUnchanged",unchanged);
        checks.truth(stage+".revisionUnchanged",controller.revision()==revision);
        checks.truth(stage+".historyUnchanged",controller.canUndo()==hadUndo&&controller.canRedo()==hadRedo);
        timings.append(QJsonObject{{"kind","checkpoint"},{"stage",stage},{"ownedObservationMs",ownedMs},{"canonicalEncodeMs",encodeMs},{"canonicalByteCount",bytes.size()}});
        progress(name,QString("%1 checkpoint: owned %2ms, canonical encode %3ms (%4 bytes)").arg(stage).arg(ownedMs).arg(encodeMs).arg(bytes.size()));};
    try {
        progress(name,"open configured full source");require(controller.openFile(QUrl::fromLocalFile(input.fileName())),"CONTROLLER_OPEN_FAILED");
        controller.selectCountry(scenario["targetId"].toString());before=controller.documentBytes();revision=controller.revision();hadUndo=controller.canUndo();hadRedo=controller.canRedo();
        require(controller.beginAnnexGeometry(),"CONTROLLER_BEGIN_REJECTED");
        require(controller.geometryToggleProvider({{"domain","territorial"},{"id",scenario["donorId"].toString()}}),"CONTROLLER_DONOR_REJECTED");
        wait("settle source selection");checkpoint("sources");
        require(controller.geometryAdvanceStage(),"CONTROLLER_ADVANCE_TO_SELECTION_REJECTED");
        require(controller.geometrySelectTerritoryMethod("components"),"CONTROLLER_COMPONENT_METHOD_REJECTED");
        wait("settle ordinary components");checkpoint("ordinary-components");
        require(controller.geometryToggleRiverBoundaries(true),"CONTROLLER_RIVER_TOGGLE_REJECTED");
        wait("settle real provider/kernel/components");checkpoint("river-components");
        auto observation=ownedObservation(controller);const auto state=observation["state"].toObject();
        require(state["riverStatus"].toString()=="ready","CONTROLLER_RIVER_NOT_READY: "+state["riverError"].toString());
        checks.equal("riverSourceDispatches",state["riverSourceDispatches"],1);checks.equal("riverKernelDispatches",state["riverKernelDispatches"],1);
        const bool actualEntryChain=scenario["representation"].toString()=="controller-entry-chain";
        const auto presentation=named(browser["presentations"].toArray(),scenario["base"].toString());
        require(actualEntryChain||!presentation.isEmpty(),"BROWSER_PRESENTATION_MISSING");
        report["browserInputContract"]=actualEntryChain?"actual-controller-entry-chain":"isolated-live-Polygon-kernel-fixture";
        const auto expectedComponents=actualEntryChain?evidence["components"].toArray():presentation["composed"].toObject()["items"].toArray();
        const auto actualComponents=observation["components"].toArray();
        require(!expectedComponents.isEmpty(),"BROWSER_COMPONENTS_MISSING");
        report["actualComponents"]=actualComponents;report["expectedComponents"]=expectedComponents;
        checks.equal("componentCount",actualComponents.size(),expectedComponents.size());
        for(int i=0;i<std::min(actualComponents.size(),expectedComponents.size());++i) {
            const auto actual=actualComponents[i].toObject(),expected=expectedComponents[i].toObject();const auto prefix=QString("components[%1].").arg(i);
            for(const auto& field:{"key","countryId","componentKey","polygonIndex","sourcePolygonIndex","partitionKind","geometry"})checks.equal(prefix+field,actual[field],expected[field]);
            const auto provenance=QJsonDocument::fromJson(actual["provenance"].toString().toUtf8()).object();checks.equal(prefix+"provenance",provenance,expected);
        }
        if(actualEntryChain) {
            const auto nativeFeatures=observation["componentFeatures"].toArray(),expectedFeatures=evidence["componentFeatures"].toArray();
            require(!nativeFeatures.isEmpty()&&!expectedFeatures.isEmpty(),"CONTROLLER_COMPONENT_FEATURE_OBSERVATION_REQUIRED");
            checks.equal("componentFeatureCount",nativeFeatures.size(),expectedFeatures.size());
            for(int i=0;i<std::min(nativeFeatures.size(),expectedFeatures.size());++i) {
                const auto actual=nativeFeatures[i].toObject(),expected=expectedFeatures[i].toObject();
                checks.equal(QString("componentFeatures[%1].id").arg(i),actual["id"],expected["id"]);
                checks.equal(QString("componentFeatures[%1].geometry").arg(i),actual["geometry"],expected["geometry"]);
                const auto origins=expected.contains("sourcePolygonIndices")?expected["sourcePolygonIndices"]:expected["properties"].toObject()["__territorySourceIndices"];
                checks.equal(QString("componentFeatures[%1].sourcePolygonIndices").arg(i),actual["sourcePolygonIndices"],origins);
            }
            const auto nativeSource=observation["sourceDiagnostics"].toObject(),expectedSource=evidence["sourceDiagnostics"].toObject();
            require(!nativeSource.isEmpty()&&!expectedSource.isEmpty(),"CONTROLLER_SOURCE_DIAGNOSTIC_OBSERVATION_REQUIRED");
            for(const auto& field:{"loadedRivers","failedLogicalIds","indexSha256","version"})checks.equal(QString("sourceDiagnostics.")+field,nativeSource[field],expectedSource[field]);
            checks.equal("donorRevisionStrings",observation["donorRevisionStrings"],evidence["donorRevisionStrings"]);
            checks.equal("editedRiverSignature",observation["editedRiverSignature"],evidence["editedRiverSignature"]);
            checks.equal("hydroRevision",observation["hydroRevision"],evidence["hydroRevision"]);
        }
        QStringList browserKeys;for(const auto& cell:evidence["selectedCells"].toArray())browserKeys.append(cell.toObject()["key"].toString());
        // Use the real controller hit-test on the browser's geographic samples.
        // Projection is used only to supply mouse coordinates. Observations are
        // read from owned geographic values, never SVG round-trip geometry.
        QJsonArray originalSampleSelections;
        for(const auto& value:scenario["samplePoints"].toArray()) {const auto prior=selectedKeys(ownedObservation(controller));const auto point=value.toArray();const auto xy=projection.project({point[0].toDouble(),point[1].toDouble()});
            require(controller.geometryPickTerritorySelection(xy.x,xy.y),"CONTROLLER_SAMPLE_SELECTION_REJECTED");wait("settle sample selection");checkpoint("sample-selected");
            QStringList added;for(const auto& key:selectedKeys(ownedObservation(controller)))if(!prior.contains(key))added.append(key);
            require(added.size()==1,"ORIGINAL_SAMPLE_MUST_SELECT_ONE_NEW_CONTROLLER_CELL");originalSampleSelections.append(QJsonObject{{"point",value},{"key",added[0]}});}
        observation=ownedObservation(controller);const auto nativeKeys=selectedKeys(observation);
        checks.equal("selectedKeysSorted",keyArray(nativeKeys,true),keyArray(browserKeys,true));
        QJsonArray actualSelected;for(const auto& key:nativeKeys)actualSelected.append(keyed(actualComponents,key));report["actualSelectedCells"]=actualSelected;
        for(const auto& cell:evidence["selectedCells"].toArray()) {const auto expected=cell.toObject();const auto actual=keyed(actualComponents,expected["key"].toString());
            checks.truth("selectedCellPresent."+expected["key"].toString(),!actual.isEmpty());
            if(!actual.isEmpty())checks.equal("selectedCellGeometry."+expected["key"].toString(),actual["geometry"],expected["geometry"]);}
        if(evidence.contains("initialCheckpoint")) {
            auto expected=evidence["initialCheckpoint"].toObject();if(!expected.contains("name"))expected["name"]="initial-selected";
            compareLifecycleCheckpoint(checks,observation,expected);report["actualInitialCheckpoint"]=observation;report["expectedInitialCheckpoint"]=expected;
        }
        QJsonArray lifecycle;QJsonObject removedByOrderedIndex;
        for(const auto& value:evidence["lifecycleActions"].toArray()) {
            const auto action=value.toObject();const auto op=action["op"].toString(),stage=action["name"].toString();bool accepted=false;
            require(!stage.isEmpty()&&action["checkpoint"].isObject(),"LIFECYCLE_ACTION_CHECKPOINT_REQUIRED");
            if(op=="archive")accepted=controller.geometryAddTerritoryPart();
            else if(op=="components") {
                accepted=controller.geometrySelectTerritoryMethod("components");require(accepted,"LIFECYCLE_COMPONENT_METHOD_REJECTED");
                wait(stage+" ordinary components");checkpoint(stage+" ordinary components");
                accepted=controller.geometryToggleRiverBoundaries(true);
            } else if(op=="removePart") {
                const auto parts=controller.geometryEditState()["parts"].toList();const auto index=action["index"].toInt(-1);
                require(index>=0&&index<parts.size(),"LIFECYCLE_ORDERED_PART_NOT_OBSERVED");
                const auto ownedParts=ownedObservation(controller)["parts"].toArray();require(index<ownedParts.size(),"OWNED_LIFECYCLE_PART_NOT_OBSERVED");
                removedByOrderedIndex[QString::number(index)]=ownedParts[index];
                accepted=controller.geometryRemoveTerritoryPart(parts[index].toMap()["id"].toString());
            } else if(op=="river")accepted=controller.geometryToggleRiverBoundaries(action["enabled"].toBool());
            else if(op=="component")accepted=controller.geometryToggleTerritoryComponent(action["key"].toString());
            else if(op=="sample"||op=="sampleRemovedPart") {
                const auto point=action[op=="sample"?"point":"resolvedPoint"].toArray();require(point.size()==2,"INVALID_LIFECYCLE_SAMPLE");
                if(op=="sampleRemovedPart") {
                    const auto removed=removedByOrderedIndex[QString::number(action["index"].toInt(-1))].toObject();
                    require(!removed.isEmpty()&&action["removedPart"].isObject(),"REMOVED_PART_SAMPLE_EVIDENCE_REQUIRED");
                    const auto key=removed["component"].toObject()["key"].toString();QJsonArray resolved;
                    for(const auto& sample:originalSampleSelections)if(sample.toObject()["key"].toString()==key)resolved.append(sample.toObject()["point"]);
                    // This association comes from the initial public controller
                    // hit-test. No centroid, geometry approximation or cell-key
                    // remapping is used to choose the restored sample point.
                    require(resolved.size()==1,"REMOVED_PART_REQUIRES_ONE_ORIGINAL_CONTROLLER_SAMPLE");
                    checks.equal("lifecycle."+stage+".resolvedPoint",point,resolved[0]);
                    checks.equal("lifecycle."+stage+".removedPart.geometry",removed["geometry"],action["removedPart"].toObject()["geometry"]);
                    compareComponents(checks,"lifecycle."+stage+".removedPart.component.",QJsonArray{removed["component"]},QJsonArray{action["removedPart"].toObject()["component"]});
                }
                const auto xy=projection.project({point[0].toDouble(),point[1].toDouble()});accepted=controller.geometryPickTerritorySelection(xy.x,xy.y);
            } else throw std::runtime_error(("UNSUPPORTED_LIFECYCLE_ACTION: "+op).toStdString());
            checks.truth("lifecycle."+stage+".accepted",accepted);require(accepted,"CONTROLLER_LIFECYCLE_ACTION_REJECTED: "+stage);
            wait(stage);checkpoint(stage);observation=ownedObservation(controller);
            auto expected=action["checkpoint"].toObject();if(!expected.contains("name"))expected["name"]=stage;
            const auto differencesBefore=checks.differences.size();compareLifecycleCheckpoint(checks,observation,expected);
            QJsonObject observed{{"op",op},{"name",stage},{"accepted",accepted},{"actualCheckpoint",observation},{"expectedCheckpoint",expected},{"exact",checks.differences.size()==differencesBefore}};
            if(action.contains("knownDivergence"))observed["knownDivergence"]=action["knownDivergence"];
            // Known stale-web checkpoints retain their raw exact differences and
            // still fail parity. Only an approved source correction can remove
            // the mismatch; no checkpoint allowlist or masking is applied.
            lifecycle.append(observed);
        }
        if(!lifecycle.isEmpty())report["lifecycleActions"]=lifecycle;
        const auto expectedResult=evidence["result"].toObject(),previewState=observation["state"].toObject();
        report["actualSelectionAndPreview"]=observation;
        checks.truth("previewReady",previewState["previewReady"].toBool());
        checks.equal("combinedGeometry",observation["combinedGeometry"],evidence["input"].toObject()["transferredGeometry"]);
        if(actualEntryChain) {
            require(observation.contains("riverSliverContext"),"CONTROLLER_SLIVER_CONTEXT_OBSERVATION_REQUIRED");
            checks.equal("riverSliverContext",observation["riverSliverContext"],evidence["input"].toObject()["riverSliverContext"]);
        }
        checks.equal("transferredGeometry",observation["transferredGeometry"],expectedResult["transferredGeometry"]);
        checks.equal("autoIncludedSliverCount",previewState["autoIncludedSliverCount"],expectedResult["autoIncludedSlivers"].toObject()["count"]);
        checks.equal("autoIncludedSliverAreaM2",previewState["autoIncludedSliverAreaM2"],expectedResult["autoIncludedSlivers"].toObject()["areaM2"]);
        if(actualEntryChain) {
            require(evidence.contains("expectedTransferAreaKm2"),"BROWSER_AUTHORITATIVE_TRANSFER_AREA_REQUIRED");
            checks.equal("transferAreaKm2",previewState["transferAreaKm2"],evidence["expectedTransferAreaKm2"]);
        }
        require(controller.geometryAdvanceStage(),"CONTROLLER_REVIEW_REJECTED: "+previewState["error"].toString());checkpoint("review");
        require(controller.confirmGeometryEdit(),"CONTROLLER_APPLY_REJECTED");wait("settle strict receipt Apply");
        require(!controller.geometryEditState()["active"].toBool(),"CONTROLLER_STRICT_APPLY_FAILED: "+controller.geometryEditState()["error"].toString());
        const auto after=controller.documentBytes();const auto afterFeatures=documentFeatures(after);report["actualChangedFeatures"]=QJsonArray{};
        QJsonArray changed;for(const auto& expected:evidence["after"].toArray()) {const auto feature=expected.toObject();const auto actual=featureById(afterFeatures,feature["id"].toString());
            changed.append(actual);checks.equal("committedGeometry."+feature["id"].toString(),actual["geometry"],feature["geometry"]);}
        report["actualChangedFeatures"]=changed;
        for(const auto& id:expectedResult["removedIds"].toArray())checks.truth("removedFeature."+id.toString(),featureById(afterFeatures,id.toString()).isEmpty());
        const auto beforeFeatures=documentFeatures(before);int unrelated=0;
        for(const auto& feature:beforeFeatures) {const auto id=feature.toObject()["id"].toString();if(id==scenario["targetId"].toString()||id==scenario["donorId"].toString())continue;
            ++unrelated;checks.equal("unrelatedGeometry."+id,featureById(afterFeatures,id)["geometry"],feature.toObject()["geometry"]);}
        report["unrelatedFeaturesChecked"]=unrelated;
        checks.truth("applyChangedCanonicalBytes",after!=before);checks.equal("applyRevisionIncrement",double(controller.revision()-revision),1);
        checks.truth("secondApplyRejected",!controller.confirmGeometryEdit());checks.truth("secondApplyBytesUnchanged",controller.documentBytes()==after);
        checks.truth("undoAvailable",controller.canUndo());controller.undo();checks.truth("undoCanonicalBytesExact",controller.documentBytes()==before);
        checks.truth("redoAvailable",controller.canRedo());controller.redo();checks.truth("redoCanonicalBytesExact",controller.documentBytes()==after);
        report["beforeSha256"]=QString::fromLatin1(QCryptographicHash::hash(before,QCryptographicHash::Sha256).toHex());
        report["afterSha256"]=QString::fromLatin1(QCryptographicHash::hash(after,QCryptographicHash::Sha256).toHex());
    } catch(const std::exception& error) {report["error"]=error.what();checks.truth("controllerLifecycleCompleted",false);report["lastOwnedObservation"]=ownedObservation(controller);}
    controller.cancelGeometryEdit();report["checkpoints"]=checkpoints;report["timings"]=timings;report["matched"]=checks.matched;report["mismatched"]=checks.mismatched;
    report["differences"]=checks.differences;report["unobservedFields"]=checks.unobservedFields;report["passed"]=checks.mismatched==0;
    progress(name,QString("matched=%1 mismatched=%2").arg(checks.matched).arg(checks.mismatched));return report;
}
int compare(const QString& browserPath,const QString& payloadPath,const QString& outputPath,const QString& onlyCase) {
    QJsonArray observations;QJsonObject identity;bool passed=true,entryChainObserved=false,complete=false;int matched=0,mismatched=0,unobserved=0,expectedCases=0;
    const auto save=[&](bool finished,const QString& runningCase=QString()) {
        const QJsonObject report{{"schema","native-controller-browser-annex-v1"},{"identity",identity},{"actualEntryChainOracle",entryChainObserved},{"complete",finished},{"passed",finished&&passed},
            {"status",finished?"complete":"incomplete"},{"runningCase",runningCase},{"expectedCases",expectedCases},{"completedCases",observations.size()},{"selectedCase",onlyCase},
            {"matched",matched},{"mismatched",mismatched},{"unobserved",unobserved},{"observations",observations},
            {"knownBoundedDivergence","Any observed web empty-selection or stale-river cache difference remains an explicit failing difference; no tolerance or checkpoint allowlist is applied."}};
        QSaveFile output(outputPath);if(!output.open(QIODevice::WriteOnly))return false;
        const auto bytes=QJsonDocument(report).toJson(QJsonDocument::Compact);
        return output.write(bytes)==bytes.size()&&output.commit();
    };
    try {
        const auto browser=readObject(browserPath),payload=readObject(payloadPath);identity=payload["identity"].toObject();
        require(!identity.isEmpty()&&browser["identity"].toObject()==identity,"BROWSER_NATIVE_IDENTITY_MISMATCH");
        require(payload["provenance"].toObject()["webCommit"].toString()=="53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47","UNPINNED_WEB_SOURCE");
        const auto worldSource=payload["world"].toObject()["source"].toString().toUtf8();
        require(QString::fromLatin1(QCryptographicHash::hash(worldSource,QCryptographicHash::Sha256).toHex())==identity["worldSha256"].toString(),"WORLD_SOURCE_DIGEST_MISMATCH");
        const auto world=QJsonDocument::fromJson(worldSource).object()["features"].toArray();
        require(world.size()==258,"INCOMPLETE_BROWSER_WORLD_CORPUS");
        const bool actualEntryChain=browser.contains("controllerAnnex");
        entryChainObserved=actualEntryChain;
        auto corpus=actualEntryChain?browser["controllerAnnex"].toArray():browser["annex"].toArray();
        require(actualEntryChain?corpus.size()>=2:corpus.size()==4,"INCOMPLETE_BROWSER_ANNEX_CORPUS");
        if(actualEntryChain)for(const auto& lifecycle:browser["controllerLifecycle"].toArray())corpus.append(lifecycle);
        for(const auto& value:corpus)if(onlyCase.isEmpty()||value.toObject()["name"].toString()==onlyCase)++expectedCases;
        if(actualEntryChain) {
            require(browser["controllerSourceCommit"].toString()=="53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47","UNPINNED_CONTROLLER_BROWSER_SOURCE");
            const auto behavior=browser["controllerBehavioralCommit"].toString();
            require(behavior=="12cd8c8ec47c83cfb8c650e8f44c81cdfac10043"||behavior=="6c3f930b8573fa09991885b661879ea36725472e","UNAPPROVED_CONTROLLER_BROWSER_CORRECTION");
            if(behavior=="6c3f930b8573fa09991885b661879ea36725472e")
                require(identity["sourceHashes"].toObject()["controller/app-territory-selection-workflow.js"].toString()=="626d2dd6c0a8263224272adacaa3303f41a28a83cf22214bdedfffd11a9bc44d","APPROVED_CONTROLLER_WORKFLOW_SOURCE_HASH_REQUIRED");
            const auto chromium=identity["runtimePin"].toObject()["chromium"].toString();
            require(!chromium.isEmpty()&&browser["runtime"].toObject()["userAgent"].toString().contains("Chrome/"+chromium),"CONTROLLER_ORACLE_REQUIRES_PINNED_CHROMIUM");
        }
        const auto manifest=qEnvironmentVariable("PANDOEDITOR_HYDRO_FULL_MANIFEST");require(!manifest.isEmpty()&&QFileInfo::exists(manifest),"CONFIGURED_FULL_MANIFEST_REQUIRED");
        int lifecycleCases=0;
        for(const auto& value:corpus) {const auto evidence=value.toObject();const auto name=evidence["name"].toString();
            if(!onlyCase.isEmpty()&&name!=onlyCase)continue;
            require(save(false,name),"ATOMIC_PARTIAL_CONTROLLER_REPORT_WRITE_FAILED");
            const auto representation=evidence["selection"].toObject()["representation"].toString();
            if(representation!="normalized-filtered-presentation"&&representation!="controller-entry-chain") {
                ++unobserved;observations.append(QJsonObject{{"name",name},{"observed",false},{"reason","Controller uses live-coordinate revisions and installed normalized presentation; raw fixtures are covered by the separate real preview differential."}});continue;}
            auto row=runCase(evidence,browser,world,manifest);matched+=row["matched"].toInt();mismatched+=row["mismatched"].toInt();passed=passed&&row["passed"].toBool();observations.append(row);
            if(!evidence["lifecycleActions"].toArray().isEmpty())++lifecycleCases;
            require(save(false),"ATOMIC_PARTIAL_CONTROLLER_REPORT_WRITE_FAILED");
        }
        require(!observations.isEmpty(),"REQUESTED_BROWSER_CASE_MISSING");
        require(matched>0,"NO_CONTROLLER_CASE_OBSERVED");
        require(actualEntryChain,"ISOLATED_BROWSER_FIXTURES_DO_NOT_ESTABLISH_CONTROLLER_ENTRYCHAIN_PARITY");
        if(onlyCase.isEmpty())require(lifecycleCases>0,"MISSING_FULL_COUNTRY_CONTROLLER_ARCHIVE_RESIDUAL_MIDDLE_REMOVAL_EVIDENCE");
        complete=true;
    } catch(const std::exception& error) {observations.append(QJsonObject{{"error",error.what()},{"passed",false}});passed=false;++mismatched;}
    if(!save(complete))return 2;return complete&&passed?0:1;
}
}
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    if(argc!=5&&argc!=7){std::fprintf(stderr,"Usage: m972_river_controller_probe --browser-annex browser-report.json native-payload.json output.json [--case exact-name]\n");return 2;}
    if(QString::fromLocal8Bit(argv[1])!="--browser-annex"||(argc==7&&QString::fromLocal8Bit(argv[5])!="--case"))return 2;
    return compare(QString::fromLocal8Bit(argv[2]),QString::fromLocal8Bit(argv[3]),QString::fromLocal8Bit(argv[4]),argc==7?QString::fromLocal8Bit(argv[6]):QString());
}
