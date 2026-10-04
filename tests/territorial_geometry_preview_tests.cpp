#include "territorial_fixture.h"
#include "territorialgeometry.h"
#include "projectcodec.h"
#include <pandoeditor/project.h>
#include <pandoeditor/geometrypredicates.h>
#include <QtTest>
#include <algorithm>
using namespace pandoeditor;
namespace {
Geometry rectangle(double x,double y,double width,double height) {
    Geometry geometry;geometry.polygons={{{{x,y},{x+width,y},{x+width,y+height},{x,y+height},{x,y}}}};return geometry;
}
ProjectDocument fixture() {
    return ProjectDocument({{"A","A",rectangle(0,0,10,10).polygons,0xabcdef},
                            {"B","B",rectangle(10,0,10,10).polygons,0x123456}},{{"countries","Countries"}});
}
void addUnit(ProjectDocument& document,const std::string& id,Geometry geometry,const std::string& parent="") {
    const GeometryRef ref{id,1};document.geometries.insert(ref,std::move(geometry));
    appendTerritory(document,{id,id,"",UnitKind::General,false},ref,parent);
    document.presentation.objectStyles[territorialRef(id)]={};
}
AnnexGeometryPreviewResult preview(Project& project,const AnnexGeometryPreviewRequest& request) {
    JobScheduler jobs;const auto ticket=jobs.enqueue(project.snapshot(),"annex-preview");jobs.takeNext();
    return calculateAnnexGeometryPreview(project.snapshot(),request,ticket.token());
}
PrepareResult prepare(Project& project,const AnnexGeometryPreviewResult& result) {
    JobScheduler jobs;const auto ticket=jobs.enqueue(project.snapshot(),"annex-commit");jobs.takeNext();
    return prepareAnnexGeometryCommit(project.snapshot(),result,ticket.token());
}
}
class TerritorialGeometryPreviewTests:public QObject {
    Q_OBJECT
private slots:
    // A regression that eagerly creates a CommandPreview would reject each of
    // these before there is real geometry available to render or archive.
    void danglingReferenceHasRealPreviewButStrictCommitRejects_data() {
        QTest::addColumn<QString>("kind");
        for(const auto kind:{"distribution","child-distribution","label","label-settings"})QTest::newRow(kind)<<QString(kind);
    }
    void danglingReferenceHasRealPreviewButStrictCommitRejects() {
        QFETCH(QString,kind);auto document=fixture();
        if(kind=="child-distribution")addUnit(document,"child",rectangle(11,1,2,2),"B");
        if(kind=="distribution"||kind=="child-distribution") {
            DistributionLayer layer;layer.id="statistics";layer.name="Statistics";document.distributionLayers.push_back(layer);
            DistributionEntry entry;entry.id="entry";entry.layerId=layer.id;entry.territory=territorialRef(kind=="child-distribution"?"child":"B");entry.value=1;document.distributionEntries.push_back(entry);
        } else if(kind=="label") {
            Geometry point;point.type="Point";point.points={{15,5}};document.geometries.insert({"label",1},point);
            PlaceLabel label;label.id="label";label.name="B label";label.geometry={"label",1};label.territory=territorialRef("B");document.labels.push_back(label);
        } else document.presentation.webPresentation.labelSettings[territorialRef("B")]={};
        Project project;project.replace(document);const auto before=projectcodec::encode(project);const auto revision=project.revision();
        const auto result=preview(project,{territorialRef("A"),{territorialRef("B")},rectangle(10,0,10,10)});
        QVERIFY2(result.ok(),result.detail.c_str());QVERIFY(!result.blocking());
        QCOMPARE(planarArea(result.transferredGeometry),100.);QCOMPARE(result.affectedDonors,std::vector<ObjectRef>{territorialRef("B")});
        QCOMPARE(result.removedRoots,std::vector<ObjectRef>{territorialRef("B")});QVERIFY(result.rows.size()>=2);
        QCOMPARE(result.rows[0].owner,territorialRef("A"));QVERIFY(result.rows[0].after);QCOMPARE(planarArea(*result.rows[0].after),200.);
        QCOMPARE(result.rows[1].owner,territorialRef("B"));QVERIFY(!result.rows[1].after);
        QCOMPARE(projectcodec::encode(project),before);QCOMPARE(project.revision(),revision);
        const auto committed=prepare(project,result);QVERIFY(!committed.ok());QVERIFY(!committed.preview);
        QVERIFY(!committed.detail.empty());QCOMPARE(projectcodec::encode(project),before);QCOMPARE(project.revision(),revision);QVERIFY(!project.undo());
    }
    void validReceiptCommitsOnceAndUndoRestores() {
        Project project;project.replace(fixture());const auto before=projectcodec::encode(project);
        const auto result=preview(project,{territorialRef("A"),{territorialRef("B")},rectangle(10,0,5,10)});
        QVERIFY2(result.ok(),result.detail.c_str());QCOMPARE(planarArea(result.transferredGeometry),50.);
        auto prepared=prepare(project,result);QVERIFY2(prepared.ok(),prepared.detail.c_str());QCOMPARE(projectcodec::encode(project),before);
        QVERIFY(CommandProcessor::confirm(project,*prepared.preview).ok());QVERIFY(!CommandProcessor::confirm(project,*prepared.preview).ok());
        const auto after=projectcodec::encode(project);QVERIFY(after!=before);QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),before);
        QVERIFY(project.redo());QCOMPARE(projectcodec::encode(project),after);
    }
    void shapeFailureBlocksWithoutCandidateDocument() {
        Project project;project.replace(fixture());const auto before=projectcodec::encode(project);auto shape=rectangle(10,0,5,10);shape.polygons[0][0].pop_back();
        const auto result=preview(project,{territorialRef("A"),{territorialRef("B")},shape});
        QVERIFY(!result.ok());QVERIFY(result.blocking());QVERIFY(!result.issues.empty());QVERIFY(!prepare(project,result).ok());QCOMPARE(projectcodec::encode(project),before);
    }
    void selectedOrderAndUnaffectedDonorsSurviveCalculation() {
        auto document=fixture();addUnit(document,"remote",rectangle(60,0,10,10));addUnit(document,"C",rectangle(20,0,10,10));
        Project project;project.replace(document);
        const auto result=preview(project,{territorialRef("A"),{territorialRef("remote"),territorialRef("C"),territorialRef("B"),territorialRef("C")},rectangle(10,0,15,10)});
        QVERIFY2(result.ok(),result.detail.c_str());
        QCOMPARE(result.selectedDonors,(std::vector<ObjectRef>{territorialRef("remote"),territorialRef("C"),territorialRef("B")}));
        QCOMPARE(result.affectedDonors,(std::vector<ObjectRef>{territorialRef("C"),territorialRef("B")}));
        QCOMPARE(result.rows.size(),std::size_t(3));QCOMPARE(result.rows[1].owner,territorialRef("C"));QCOMPARE(result.rows[2].owner,territorialRef("B"));
        QCOMPARE(result.removedRoots,std::vector<ObjectRef>{territorialRef("B")});QVERIFY(prepare(project,result).ok());
    }
    void untouchedPolygonVerticesAndBaselineIssuesArePreserved() {
        auto target=rectangle(0,0,10,10);auto targetIsland=rectangle(40,0,2,2).polygons.front();
        targetIsland.front().insert(targetIsland.front().begin()+1,targetIsland.front().front());
        target.polygons.push_back(targetIsland);
        auto donor=rectangle(10,0,10,10);const auto donorIsland=rectangle(60,0,2,2).polygons.front();donor.polygons.push_back(donorIsland);
        ProjectDocument document({{"A","A",target.polygons,0xabcdef},{"B","B",donor.polygons,0x123456}},{{"countries","Countries"}});
        Project project;project.replace(document);
        const auto result=preview(project,{territorialRef("A"),{territorialRef("B")},rectangle(10,0,5,10)});
        QVERIFY2(result.ok(),result.detail.c_str());QVERIFY(result.issues.empty());
        const auto& targetAfter=result.rows[0].after->polygons;const auto& donorAfter=result.rows[1].after->polygons;
        QCOMPARE(targetAfter.size(),std::size_t(2));QCOMPARE(donorAfter.size(),std::size_t(2));
        QCOMPARE(targetAfter.front().front().size(),std::size_t(6));
        for(std::size_t i=0;i<targetIsland.front().size();++i) {
            QCOMPARE(targetAfter.front().front()[i].x,targetIsland.front()[i].x);
            QCOMPARE(targetAfter.front().front()[i].y,targetIsland.front()[i].y);
        }
        for(std::size_t i=0;i<donorIsland.front().size();++i) {
            QCOMPARE(donorAfter.back().front()[i].x,donorIsland.front()[i].x);
            QCOMPARE(donorAfter.back().front()[i].y,donorIsland.front()[i].y);
        }
    }
    void rawSelectionClampsOnlyNumericalFringe() {
        Project project;project.replace(fixture());
        const auto fringe=preview(project,{territorialRef("A"),{territorialRef("B")},rectangle(10-1e-9,0,5+1e-9,10)});
        QVERIFY2(fringe.ok(),fringe.detail.c_str());QCOMPARE(planarArea(fringe.transferredGeometry),50.);
        const auto outside=preview(project,{territorialRef("A"),{territorialRef("B")},rectangle(9,0,6,10)});
        QVERIFY(!outside.ok());QCOMPARE(outside.detail,std::string("SELECTION_OUTSIDE_DONOR"));QVERIFY(!prepare(project,outside).ok());
    }
    void rootPreviewRejectsSiblingAndLockedSources() {
        auto document=fixture();addUnit(document,"child",rectangle(11,1,2,2),"B");Project project;project.replace(document);
        const auto sibling=preview(project,{territorialRef("A"),{territorialRef("child")},rectangle(11,1,2,2)});
        QVERIFY(!sibling.ok());QCOMPARE(sibling.detail,std::string("ANNEX_REQUIRES_ROOT_GENERAL"));
        auto locked=fixture();locked.units[1].locked=true;Project lockedProject;lockedProject.replace(locked);
        const auto forbidden=preview(lockedProject,{territorialRef("A"),{territorialRef("B")},rectangle(10,0,5,10)});
        QVERIFY(!forbidden.ok());QCOMPARE(forbidden.detail,std::string("LOCKED"));
    }
    void staleAndCancelledReceiptsCannotPrepare() {
        Project project;project.replace(fixture());const auto result=preview(project,{territorialRef("A"),{territorialRef("B")},rectangle(10,0,5,10)});QVERIFY(result.ok());
        QVERIFY(project.renameCountry("A","renamed"));const auto before=projectcodec::encode(project);const auto stale=prepare(project,result);
        QVERIFY(!stale.ok());QCOMPARE(stale.error,CommandError::StaleRevision);QCOMPARE(projectcodec::encode(project),before);
        JobScheduler jobs;const auto ticket=jobs.enqueue(project.snapshot(),"cancelled-annex");jobs.takeNext();jobs.cancel(ticket.id());
        const auto fresh=preview(project,{territorialRef("A"),{territorialRef("B")},rectangle(10,0,5,10)});QVERIFY(fresh.ok());
        const auto refused=prepareAnnexGeometryCommit(project.snapshot(),fresh,ticket.token());QVERIFY(!refused.ok());QVERIFY(!refused.preview);QCOMPARE(refused.detail,std::string("CANCELLED"));
        const auto cancelled=calculateAnnexGeometryPreview(project.snapshot(),{territorialRef("A"),{territorialRef("B")},rectangle(10,0,5,10)},ticket.token());
        QCOMPARE(cancelled.status,GeometryOperationStatus::Cancelled);QVERIFY(!cancelled.ok());QVERIFY(!cancelled.plan);QCOMPARE(projectcodec::encode(project),before);
    }
    void baselineOverlapIsAllowedButNewOverlapBlocks() {
        auto document=fixture();addUnit(document,"C",rectangle(0,0,5,5));Project project;project.replace(document);
        const auto existing=preview(project,{territorialRef("A"),{territorialRef("B")},rectangle(10,0,5,10)});
        QVERIFY2(existing.ok(),existing.detail.c_str());QVERIFY(!existing.blocking());
        // C overlaps B initially. Transferring that area would introduce A/C
        // overlap, even though total land union is unchanged.
        auto overlapping=fixture();addUnit(overlapping,"C",rectangle(10,0,5,5));Project other;other.replace(overlapping);
        const auto introduced=preview(other,{territorialRef("A"),{territorialRef("B")},rectangle(10,0,5,10)});
        QVERIFY(!introduced.ok());QVERIFY(introduced.blocking());QVERIFY(!introduced.issues.empty());QVERIFY(!prepare(other,introduced).ok());
    }
};
QTEST_GUILESS_MAIN(TerritorialGeometryPreviewTests)
#include "territorial_geometry_preview_tests.moc"
