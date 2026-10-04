#include "territoryselection.h"
#include "geometrycalculator.h"
#include <pandoeditor/geometrypredicates.h>
#include <QtTest>
#include <limits>
using namespace pandoeditor;
namespace {
Geometry box(double x,double y,double right,double top) {
    Geometry g;g.type="Polygon";
    g.polygons={{{{x,y},{x,top},{right,top},{right,y},{x,y}}}};
    return g;
}
TerritorySelectionSource source(std::string id,Geometry g) {
    TerritorySelectionSource s;s.ref=territorialRef(id);s.geometry=std::move(g);s.name=id;
    s.geometryRef={"geometry-"+id,1};s.propertiesJson="{\"kept\":true}";return s;
}
Geometry withIsland() {auto g=box(0,0,10,10);g.type="MultiPolygon";g.polygons.push_back(box(12,0,14,2).polygons.front());return g;}
Geometry decodedGeometry(const QJsonObject& object) {
    Geometry geometry;geometry.type=object.value("type").toString().toStdString();
    auto polygons=object.value("coordinates").toArray();
    if(geometry.type=="Polygon")polygons=QJsonArray{polygons};
    for(const auto& p:polygons) {
        Polygon polygon;
        for(const auto& r:p.toArray()) {
            Ring ring;for(const auto& point:r.toArray()) {const auto xy=point.toArray();ring.push_back({xy[0].toDouble(),xy[1].toDouble()});}
            polygon.push_back(std::move(ring));
        }
        geometry.polygons.push_back(std::move(polygon));
    }
    return geometry;
}
bool matchesGeometry(const std::optional<Geometry>& actual,const QJsonValue& expected) {
    if(expected.isNull()||expected.isUndefined())return !actual;
    if(!actual)return false;
    const auto geometry=decodedGeometry(expected.toObject());
    const auto left=calculateGeometry({GeometryOperation::Difference,*actual,geometry});
    const auto right=calculateGeometry({GeometryOperation::Difference,geometry,*actual});
    return left.succeeded()&&right.succeeded()&&planarArea(left.geometry)<1e-10&&planarArea(right.geometry)<1e-10;
}
std::vector<TerritorySelectionSource> decodedSources(const QJsonObject& stage) {
    std::vector<TerritorySelectionSource> sources;
    for(const auto& value:stage.value("baseSourceFeatures").toArray()) {
        const auto feature=value.toObject();auto item=source(feature.value("id").toString().toStdString(),decodedGeometry(feature.value("geometry").toObject()));
        item.propertiesJson=QJsonDocument(feature.value("properties").toObject()).toJson(QJsonDocument::Compact).toStdString();sources.push_back(std::move(item));
    }
    return sources;
}
QStringList candidateIndexes(const std::vector<std::string>& ids) {
    QStringList result;for(const auto& id:ids)result.push_back(QString::fromStdString(id).section(':',-1));return result;
}
QStringList candidateIndexes(const QJsonArray& ids) {
    QStringList result;for(const auto& id:ids)result.push_back(id.toString().section(':',-1));return result;
}

}
// Core expectations below were observed through the unmodified production workflow at
// 53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47, session-observations.json.
// Explicitly annotated exceptions cover the approved full-annex fix and the
// native fresh-remainder behavior at the web empty-selection stale-cache edge.
// The helper has no canonical document, preview authorization, stage or UI owner.
class TerritorySelectionTests:public QObject {
    Q_OBJECT
    QJsonArray goldenCases_;
    QJsonObject stages(const char* name) const {
        for(const auto& value:goldenCases_)if(value.toObject().value("case").toString()==QLatin1String(name))return value.toObject().value("stages").toObject();
        return {};
    }
private slots:
    void initTestCase() {
        QFile file(QFINDTESTDATA("fixtures/web-m97/session-observations.json"));
        QVERIFY2(file.open(QIODevice::ReadOnly),qPrintable(file.errorString()));
        QJsonParseError error;const auto root=QJsonDocument::fromJson(file.readAll(),&error).object();
        QCOMPARE(error.error,QJsonParseError::NoError);
        QCOMPARE(root.value("behavioralCommit").toString(),QString("53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47"));
        goldenCases_=root.value("cases").toArray();QVERIFY(!goldenCases_.empty());
    }
    void realWebCandidateGeometryAndOrderedSelectionReplay() {
        const auto golden=stages("line-two-crossing-candidate-toggle-order");QVERIFY(!golden.empty());
        const auto initial=golden.value("initial").toObject();
        TerritorySelection selection;QVERIFY(selection.resetSources(decodedSources(initial)));
        QCOMPARE(selection.requestMethod(TerritorySelectionMethod::Line),TerritoryMethodChange::Activated);
        std::vector<TerritorySelectionCandidate> candidates;
        for(const auto& value:initial.value("candidates").toArray()) {const auto c=value.toObject();candidates.push_back({c.value("id").toString().toStdString(),decodedGeometry(c.value("geometry").toObject()),c.value("area").toDouble()});}
        QVERIFY(selection.setCandidates(std::move(candidates)));
        const auto first=selection.state().candidates[0].id,second=selection.state().candidates[1].id;
        const auto check=[&](const char* name) {
            const auto expected=golden.value(QLatin1String(name)).toObject();
            QCOMPARE(candidateIndexes(selection.state().selectedCandidateIds),candidateIndexes(expected.value("selectedCandidateIds").toArray()));
            QVERIFY(matchesGeometry(selection.state().currentGeometry,expected.value("currentGeometry")));
            QVERIFY(matchesGeometry(selection.state().combinedGeometry,expected.value("combinedGeometry")));
            QVERIFY(matchesGeometry(selection.state().remainingGeometry,expected.value("remainingGeometry")));
        };
        check("initial");QVERIFY(selection.toggleCandidate(second));check("both");
        QVERIFY(selection.toggleCandidate(first));check("secondOnly");QVERIFY(selection.toggleCandidate(first));check("reselectedOrder");
        QVERIFY(selection.undoPart());check("undoSelection");QVERIFY(selection.toggleCandidate(second));
        const auto deselected=golden.value("deselected").toObject();
        QVERIFY(matchesGeometry(selection.state().currentGeometry,deselected.value("currentGeometry")));
        QVERIFY(matchesGeometry(selection.state().combinedGeometry,deselected.value("combinedGeometry")));
        QCOMPARE(candidateIndexes(selection.state().selectedCandidateIds),candidateIndexes(deselected.value("selectedCandidateIds").toArray()));
        // Known web cache defect: the no-parts/no-selection early return retains
        // the preceding 50-area remainder. The value helper recomputes the full
        // 100-area source. This assertion records a deliberate non-parity edge.
        QCOMPARE(planarArea(decodedGeometry(deselected.value("remainingGeometry").toObject())),50.);
        QCOMPARE(planarArea(*selection.state().remainingGeometry),100.);
    }
    void realWebRiverCellsArchiveAndSliverSnapshotReplay() {
        const auto golden=stages("river-partition-provenance-snapshot-slivers");QVERIFY(!golden.empty());
        const auto initial=golden.value("partitioned").toObject();
        TerritorySelection selection;QVERIFY(selection.resetSources(decodedSources(initial)));
        QCOMPARE(selection.requestMethod(TerritorySelectionMethod::Components),TerritoryMethodChange::Activated);
        QVERIFY(selection.toggleRiverBoundaries(true));
        std::vector<TerritorySelectionComponent> items;
        for(const auto& value:initial.value("componentItems").toArray()) {
            const auto c=value.toObject();TerritorySelectionComponent item;
            item.key=c.value("key").toString().toStdString();item.countryId=c.value("countryId").toString().toStdString();
            item.countryName=c.value("countryName").toString().toStdString();item.componentKey=c.value("componentKey").toString().toStdString();
            item.polygonIndex=c.value("polygonIndex").toInt();item.sourcePolygonIndex=c.value("sourcePolygonIndex").toInt();
            item.partitionKind=c.value("partitionKind").toString().toStdString();item.geometry=decodedGeometry(c.value("geometry").toObject());
            item.provenanceJson=QJsonDocument(c).toJson(QJsonDocument::Compact).toStdString();items.push_back(std::move(item));
        }
        QVERIFY(selection.installRiverComponents(items,"actual-web-fixture"));
        const auto selected=golden.value("selected").toObject();
        for(const auto& key:selected.value("selectedComponentKeys").toArray())QVERIFY(selection.toggleComponent(key.toString().toStdString()));
        QVERIFY(matchesGeometry(selection.state().currentGeometry,selected.value("currentGeometry")));
        const auto checkSlivers=[&](const QJsonObject& expected) {
            const auto actual=selection.riverSliverContext();const auto contexts=expected.value("previewPayload").toObject().value("riverSliverContext").toArray();
            QCOMPARE(actual.size(),std::size_t(contexts.size()));
            for(int i=0;i<contexts.size();++i) {
                const auto context=contexts[i].toObject();QCOMPARE(actual[i].donorId,context.value("donorId").toString().toStdString());
                QCOMPARE(actual[i].polygonIndex,std::size_t(context.value("polygonIndex").toInt()));
                const auto cells=context.value("unselectedGeometries").toArray();QCOMPARE(actual[i].unselectedGeometries.size(),std::size_t(cells.size()));
                for(int j=0;j<cells.size();++j)QVERIFY(matchesGeometry(actual[i].unselectedGeometries[j],cells[j]));
            }
        };
        checkSlivers(selected);QVERIFY(selection.addPart());const auto archived=golden.value("archived").toObject();
        QVERIFY(matchesGeometry(selection.state().workingSourceGeometry,archived.value("workingSourceGeometry")));
        QVERIFY(matchesGeometry(selection.state().combinedGeometry,archived.value("combinedGeometry")));checkSlivers(archived);
        QCOMPARE(selection.state().componentSnapshots[0].items.size(),items.size());
        for(std::size_t i=0;i<items.size();++i)QCOMPARE(selection.state().componentSnapshots[0].items[i].provenanceJson,items[i].provenanceJson);
    }
    void sourceSnapshotsPreserveInputOrderAndUntouchedPolygonCoordinates() {
        TerritorySelection selection;
        const auto original=withIsland();
        QVERIFY(selection.resetSources({source(" b ",original),source("a",box(20,0,22,2)),source("b",box(40,0,41,1))}));
        const auto& s=selection.state();
        QCOMPARE(s.sources.size(),std::size_t(2));QCOMPARE(s.sources[0].ref.id,std::string("b"));QCOMPARE(s.sources[1].ref.id,std::string("a"));
        QCOMPARE(s.sources[0].propertiesJson,std::string("{\"kept\":true}"));
        QCOMPARE(s.components.size(),std::size_t(3));QCOMPARE(s.components[0].key,std::string("component:b:0:0"));
        QCOMPARE(s.components[1].key,std::string("component:b:1:0"));
        QCOMPARE(s.components[1].sourcePolygonIndex,std::size_t(1));
        QCOMPARE(s.components[0].geometry.polygons[0][0][1].x,original.polygons[0][0][1].x);
        QCOMPARE(s.components[0].geometry.polygons[0][0][1].y,original.polygons[0][0][1].y);
        QCOMPARE(planarArea(*s.baseSourceGeometry),108.);
    }
    void candidateStrictMinimumAndToggleInsertionOrder() {
        TerritorySelection selection;QVERIFY(selection.resetSources({source("donor",box(0,0,10,10))}));
        QCOMPARE(selection.requestMethod(TerritorySelectionMethod::Line),TerritoryMethodChange::Activated);
        QVERIFY(selection.setCandidates({{"kernel:first",box(0,0,5,10),50.},{"kernel:second",box(5,0,10,10),50.}}));
        auto first=selection.state().candidates[0].id,second=selection.state().candidates[1].id;
        QVERIFY(first!="kernel:first");QCOMPARE(selection.state().selectedCandidateIds,std::vector<std::string>{first});
        QVERIFY(selection.toggleCandidate(second));QVERIFY(selection.toggleCandidate(first));QVERIFY(selection.toggleCandidate(first));
        QCOMPARE(selection.state().selectedCandidateIds,(std::vector<std::string>{second,first}));
        const auto operands=selection.currentOperands();QCOMPARE(operands.size(),std::size_t(2));
        QCOMPARE(operands[0].polygons[0][0][0].x,0.);QCOMPARE(operands[1].polygons[0][0][0].x,5.);
        QVERIFY(selection.undoPart());QCOMPARE(selection.state().selectedCandidateIds,std::vector<std::string>{second});
        QCOMPARE(planarArea(*selection.state().currentGeometry),50.);
        QVERIFY(selection.toggleCandidate(second));QVERIFY(!selection.state().currentGeometry);QVERIFY(!selection.state().combinedGeometry);
        QVERIFY(!selection.toggleCandidate("missing"));
        QVERIFY(selection.setCandidates({{"large",box(0,0,6,10),60.},{"small",box(6,0,10,10),40.}}));
        QCOMPARE(selection.state().selectedCandidateIds,std::vector<std::string>{selection.state().candidates[1].id});
        // An absent first area behaves like JavaScript undefined, not zero/infinity.
        QVERIFY(selection.setCandidates({{"missing-area",box(0,0,6,10),{}},{"small",box(6,0,10,10),40.}}));
        QCOMPARE(selection.state().selectedCandidateIds,std::vector<std::string>{selection.state().candidates[0].id});
    }
    void polygonPreprocessingClipsDonorAndTargetAsOneCandidate() {
        TerritorySelection selection;QVERIFY(selection.resetSources({source("donor",withIsland())}));
        QCOMPARE(selection.requestMethod(TerritorySelectionMethod::Polygon),TerritoryMethodChange::Activated);
        QVERIFY(selection.setDrawnPolygon(box(-2,2,4,8),box(-3,-3,-1,-1)));
        QCOMPARE(selection.state().candidates.size(),std::size_t(1));QCOMPARE(planarArea(*selection.state().currentGeometry),24.);
        QCOMPARE(planarArea(*selection.state().remainingGeometry),80.);
        QVERIFY(selection.setDrawnPolygon(box(-2,-2,20,20),box(0,0,2,10)));
        QCOMPARE(selection.state().candidates.size(),std::size_t(1));QCOMPARE(selection.state().currentGeometry->polygons.size(),std::size_t(2));
        QCOMPARE(planarArea(*selection.state().currentGeometry),84.);
        QVERIFY(selection.setDrawnPolygon(box(30,30,40,40),box(-3,-3,-1,-1)));
        QVERIFY(selection.state().candidates.empty());QVERIFY(!selection.state().currentGeometry);
    }
    void componentRemainingStaysWorkingUntilOrderedArchival() {
        TerritorySelection selection;QVERIFY(selection.resetSources({source("donor",withIsland())}));
        QCOMPARE(selection.requestMethod(TerritorySelectionMethod::Components),TerritoryMethodChange::Activated);
        QVERIFY(selection.toggleComponent("component:donor:1:0"));QVERIFY(selection.toggleComponent("component:donor:0:0"));
        QCOMPARE(selection.state().selectedComponentKeys,(std::vector<std::string>{"component:donor:1:0","component:donor:0:0"}));
        QCOMPARE(planarArea(*selection.state().remainingGeometry),104.);QCOMPARE(planarArea(*selection.state().workingSourceGeometry),104.);
        QCOMPARE(selection.archiveReadiness(),TerritoryArchiveReadiness::Ready);
        QVERIFY(selection.addPart());const auto& s=selection.state();
        QCOMPARE(s.parts.size(),std::size_t(2));QCOMPARE(s.parts[0].component->key,std::string("component:donor:0:0"));
        QCOMPARE(s.parts[1].component->key,std::string("component:donor:1:0"));
        QCOMPARE(s.componentSnapshots.size(),std::size_t(1));QCOMPARE(s.componentSnapshots[0].items.size(),std::size_t(2));
        QVERIFY(!s.workingSourceGeometry);QVERIFY(!s.currentGeometry);QCOMPARE(planarArea(*s.combinedGeometry),104.);
        QCOMPARE(s.activeMethod,TerritorySelectionMethod::None);
        const auto first=s.parts.front().id;QVERIFY(selection.removePart(first));
        QCOMPARE(selection.state().componentSnapshots.size(),std::size_t(1));QCOMPARE(planarArea(*selection.state().archivedGeometry),4.);
        QVERIFY(selection.undoPart());QVERIFY(selection.state().parts.empty());QVERIFY(selection.state().componentSnapshots.empty());
        // The pinned web undoLast fixture retains stale area 100 here; native
        // deliberately rebuilds area 104 rather than preserving the cache bug.
        QCOMPARE(planarArea(*selection.state().workingSourceGeometry),104.);
    }
    void archiveFragmentationKeepsOriginalPolygonProvenance() {
        TerritorySelection selection;QVERIFY(selection.resetSources({source("donor",withIsland())}));
        QCOMPARE(selection.requestMethod(TerritorySelectionMethod::Polygon),TerritoryMethodChange::Activated);
        QVERIFY(selection.setCandidates({{"strip",box(4,0,6,10),20.}}));QVERIFY(selection.addPart());
        QCOMPARE(selection.state().components.size(),std::size_t(3));
        QCOMPARE(selection.state().components[0].key,std::string("component:donor:0:0"));
        QCOMPARE(selection.state().components[1].key,std::string("component:donor:0:1"));
        QCOMPARE(selection.state().components[1].sourcePolygonIndex,std::size_t(0));
        QCOMPARE(selection.state().components[1].polygonIndex,std::size_t(1));
        QCOMPARE(selection.state().components[2].key,std::string("component:donor:1:0"));
        QCOMPARE(selection.state().components[2].componentKey,std::string("donor:2"));
        QCOMPARE(selection.state().componentFeatures[0].sourcePolygonIndices,(std::vector<std::size_t>{0,0,1}));
        QCOMPARE(selection.state().componentFeatures[0].source.propertiesJson,std::string("{\"kept\":true}"));
    }
    void methodCancelPreservesWorkAndConfirmClearsOnlyCurrent() {
        TerritorySelection selection;QVERIFY(selection.resetSources({source("donor",withIsland())}));
        QCOMPARE(selection.requestMethod(TerritorySelectionMethod::Polygon),TerritoryMethodChange::Activated);
        QVERIFY(selection.setCandidates({{"old",box(0,0,2,2),4.}}));QVERIFY(selection.addPart());
        QCOMPARE(selection.requestMethod(TerritorySelectionMethod::Polygon),TerritoryMethodChange::Activated);
        QVERIFY(selection.setCandidates({{"current",box(4,4,6,6),4.}}));
        const auto selected=selection.state().selectedCandidateIds;
        QCOMPARE(selection.requestMethod(TerritorySelectionMethod::Line),TerritoryMethodChange::NeedsConfirmation);
        QCOMPARE(selection.state().activeMethod,TerritorySelectionMethod::Polygon);
        QVERIFY(selection.cancelMethodChange());QCOMPARE(selection.state().selectedCandidateIds,selected);
        QCOMPARE(selection.state().requestedMethod,TerritorySelectionMethod::Polygon);
        QCOMPARE(selection.requestMethod(TerritorySelectionMethod::Line),TerritoryMethodChange::NeedsConfirmation);
        QVERIFY(selection.confirmMethodChange());QVERIFY(selection.state().candidates.empty());QVERIFY(!selection.state().currentGeometry);
        QCOMPARE(selection.state().activeMethod,TerritorySelectionMethod::Line);QCOMPARE(selection.state().parts.size(),std::size_t(1));
        QCOMPARE(planarArea(*selection.state().combinedGeometry),4.);
        QVERIFY(selection.resetSources({source("other",box(20,0,22,2))}));QVERIFY(selection.state().parts.empty());
        QCOMPARE(selection.state().components[0].countryId,std::string("other"));
    }
    void componentSwitchWaitsForOwnerToAuthorizeArchive() {
        TerritorySelection selection;QVERIFY(selection.resetSources({source("donor",withIsland())}));
        QCOMPARE(selection.requestMethod(TerritorySelectionMethod::Components),TerritoryMethodChange::Activated);
        QVERIFY(selection.toggleComponent("component:donor:1:0"));
        QCOMPARE(selection.requestMethod(TerritorySelectionMethod::Line),TerritoryMethodChange::AwaitingComponentArchive);
        QCOMPARE(selection.state().activeMethod,TerritorySelectionMethod::Components);QVERIFY(selection.state().parts.empty());
        const auto requested=selection.state().requestedMethod;QVERIFY(selection.addPart());
        QCOMPARE(selection.requestMethod(requested),TerritoryMethodChange::Activated);QCOMPARE(selection.state().parts.size(),std::size_t(1));
    }
    void riverSlotsRequirePreparedCellsAndRetainSnapshotContext() {
        TerritorySelection selection;QVERIFY(selection.resetSources({source("donor",withIsland())}));
        QCOMPARE(selection.requestMethod(TerritorySelectionMethod::Components),TerritoryMethodChange::Activated);
        QVERIFY(selection.toggleRiverBoundaries(true));QVERIFY(selection.activeComponents().empty());
        QVERIFY(!selection.toggleComponent("component:donor:0:0"));
        auto left=selection.state().components[0],right=left,fallback=selection.state().components[1];
        left.key="donor:river-left";left.geometry=box(0,0,5,10);left.partitionKind="river";left.provenanceJson="{\"riverIds\":[\"river\"]}";
        right.key="donor:river-right";right.geometry=box(5,0,10,10);right.partitionKind="river";fallback.partitionKind="original";
        QVERIFY(selection.installRiverComponents({left,right,fallback},"source-key"));
        QVERIFY(selection.activeComponents()[0].usesRiverBoundary);QVERIFY(!selection.activeComponents()[2].usesRiverBoundary);
        QVERIFY(selection.toggleComponent(left.key));
        auto context=selection.riverSliverContext();QCOMPARE(context.size(),std::size_t(1));QCOMPARE(context[0].polygonIndex,std::size_t(0));
        QCOMPARE(context[0].unselectedGeometries.size(),std::size_t(1));QCOMPARE(planarArea(context[0].unselectedGeometries[0]),50.);
        QVERIFY(selection.addPart());QCOMPARE(selection.state().parts[0].component->sourcePolygonIndex,std::size_t(0));
        QCOMPARE(selection.state().parts[0].component->provenanceJson,left.provenanceJson);
        context=selection.riverSliverContext();QCOMPARE(context.size(),std::size_t(1));QCOMPARE(planarArea(context[0].unselectedGeometries[0]),50.);
        QVERIFY(selection.removePart(selection.state().parts[0].id));QVERIFY(selection.riverSliverContext().empty());
        QVERIFY(selection.state().componentSnapshots.empty());
    }
    void removingArchiveInvalidatesLiveRiverPreparation() {
        TerritorySelection selection;QVERIFY(selection.resetSources({source("donor",withIsland())}));
        QCOMPARE(selection.requestMethod(TerritorySelectionMethod::Components),TerritoryMethodChange::Activated);
        QVERIFY(selection.toggleComponent("component:donor:1:0"));QVERIFY(selection.addPart());
        const auto archivedId=selection.state().parts[0].id;
        QCOMPARE(selection.requestMethod(TerritorySelectionMethod::Components),TerritoryMethodChange::Activated);
        QVERIFY(selection.toggleRiverBoundaries(true));
        auto right=selection.state().components[0];right.key="river:right";right.geometry=box(5,0,10,10);right.partitionKind="river";
        auto left=right;left.key="river:left";left.geometry=box(0,0,5,10);
        QVERIFY(selection.installRiverComponents({right,left},"with-island-archived"));QVERIFY(selection.toggleComponent(right.key));
        QVERIFY(selection.removePart(archivedId));
        QCOMPARE(selection.state().riverStatus,TerritoryRiverStatus::Pending);
        QVERIFY(selection.activeComponents().empty());QVERIFY(!selection.state().currentGeometry);
        QCOMPARE(selection.state().selectedComponentKeys,std::vector<std::string>{right.key});
        QCOMPARE(selection.archiveReadiness(),TerritoryArchiveReadiness::RiverComponentsPending);
        auto island=selection.state().components[1];island.partitionKind="original";
        QVERIFY(selection.installRiverComponents({right,left,island},"with-no-archives"));
        QCOMPARE(planarArea(*selection.state().currentGeometry),50.);
    }
    void annexFullSourceArchivesButBoundedCreationStillNeedsRemainder() {
        // User-approved correction to the pinned web dead-end. Preview/stage
        // authorization remains the GeometryEditSession owner's responsibility.
        TerritorySelection annex(TerritorySelectionKind::Annex);
        QVERIFY(annex.resetSources({source("donor",box(0,0,10,10))}));
        QCOMPARE(annex.requestMethod(TerritorySelectionMethod::Polygon),TerritoryMethodChange::Activated);
        QVERIFY(annex.setCandidates({{"full",box(0,0,10,10),{}}}));
        QVERIFY(!annex.state().remainingGeometry);QCOMPARE(annex.archiveReadiness(),TerritoryArchiveReadiness::Ready);
        QVERIFY(annex.candidateRequiresArchival());QVERIFY(annex.addPart());QVERIFY(!annex.candidateRequiresArchival());
        QCOMPARE(planarArea(*annex.state().combinedGeometry),100.);QVERIFY(!annex.state().workingSourceGeometry);
        TerritorySelection creation(TerritorySelectionKind::BoundedCreation);
        QVERIFY(creation.resetSources({source("donor",box(0,0,10,10))}));
        QCOMPARE(creation.requestMethod(TerritorySelectionMethod::Polygon),TerritoryMethodChange::Activated);
        QVERIFY(creation.setCandidates({{"full",box(0,0,10,10),{}}}));
        QCOMPARE(creation.archiveReadiness(),TerritoryArchiveReadiness::BoundedSourceExhausted);QVERIFY(!creation.addPart());
    }
    void failedCalculationLeavesValueStateUnchanged() {
        TerritorySelection selection;QVERIFY(selection.resetSources({source("donor",withIsland())}));
        QCOMPARE(selection.requestMethod(TerritorySelectionMethod::Polygon),TerritoryMethodChange::Activated);
        QVERIFY(selection.setCandidates({{"valid",box(0,0,2,2),4.}}));
        const auto id=selection.state().selectedCandidateIds[0];
        auto bad=box(0,0,2,2);bad.polygons[0][0][1].x=std::numeric_limits<double>::infinity();
        QVERIFY(!selection.setCandidates({{"invalid",bad,4.}}));QVERIFY(!selection.lastError().empty());
        QCOMPARE(selection.state().selectedCandidateIds[0],id);QCOMPARE(planarArea(*selection.state().currentGeometry),4.);
    }
};
QTEST_GUILESS_MAIN(TerritorySelectionTests)
#include "territory_selection_tests.moc"
