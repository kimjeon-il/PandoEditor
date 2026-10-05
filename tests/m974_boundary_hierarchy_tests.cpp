#include "territorial_fixture.h"
#include "territorialgeometry.h"
#include "projectcodec.h"
#include "retainedreferencerewriter.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <type_traits>
#include <pandoeditor/project.h>
#include <pandoeditor/geometrypredicates.h>
#include <pandoeditor/presentationcommands.h>
#include <QtTest>
using namespace pandoeditor;
static_assert(std::is_same_v<decltype(std::declval<BoundaryGeometryPreviewResult&>().plan()),const std::optional<TerritorialMutationPlan>&>);
static_assert(std::is_same_v<decltype(std::declval<BoundaryGeometryPreviewResult&>().patch()),const GeometryPatch&>);
namespace {
Geometry box(double x,double y,double w,double h) {Geometry g;g.polygons={{{{x,y},{x+w,y},{x+w,y+h},{x,y+h},{x,y}}}};return g;}
void child(ProjectDocument& d,const std::string& id,const std::string& parent,Geometry shape,bool locked=false) {
    const GeometryRef ref{id,1};d.geometries.insert(ref,std::move(shape));appendTerritory(d,{id,id,"",UnitKind::General,locked},ref,parent,"partition");d.presentation.objectStyles[territorialRef(id)]={};
}
ProjectDocument roots() {return ProjectDocument({{"A","A",box(0,0,10,10).polygons,0},{"B","B",box(10,0,10,10).polygons,0}},{{"countries","Countries"}});}
SharedBoundaryIntent shifted() {return {{{territorialRef("A"),box(0,0,8,10)},{territorialRef("B"),box(8,0,12,10)}}};}
PrepareResult prepare(Project& p,const SharedBoundaryIntent& intent) {const auto plan=CommandProcessor::planTerritorial(p,intent);if(!plan.ok()){PrepareResult failure;failure.detail=plan.detail;failure.error=plan.error;return failure;}JobScheduler jobs;const auto ticket=jobs.enqueue(p.snapshot(),"boundary-test");jobs.takeNext();return prepareTerritorialGeometry(p.snapshot(),*plan.plan,ticket.token());}
const Geometry& geometry(const ProjectDocument& d,const std::string& id) {return *d.geometries.get(staticGeometryBinding(d,id).geometryRef);}
bool exists(const ProjectDocument& d,const std::string& id) {return std::any_of(d.units.begin(),d.units.end(),[&](const auto& unit){return unit.id==id;});}
ProjectDocument historyFixture() {
    ProjectDocument d({{"A","A",box(0,0,10,10).polygons,0},{"B","B",box(10,0,5,10).polygons,0},{"C","C",box(15,0,5,10).polygons,0}},{{"countries","Countries"}});child(d,"gone","A",box(7,1,2,8));child(d,"gone-deep","gone",box(7.5,2,1,1));
    d.presentation.webPresentation.labelSettings[territorialRef("gone")].priority=3;d.presentation.webPresentation.labelSettings[territorialRef("gone-deep")].priority=4;d.presentation.webPresentation.labelSettings[territorialRef("A")].priority=5;d.presentation.webPresentation.hiddenItems["subunits"].insert("gone");d.presentation.webPresentation.objectStyles[territorialPresentationKey("gone")].opacity=.3;
    Geometry point;point.type="Point";point.points={{1,1}};d.geometries.insert({"custom-label",1},point);PlaceLabel label;label.id="territorial:gone";label.name="Custom";label.geometry={"custom-label",1};d.labels.push_back(label);d.presentation.webPresentation.labelSettings[{"label",label.id}].priority=6;return d;
}
SharedBoundaryIntent deletingBoundary(){return {{{territorialRef("A"),box(0,0,6,10)},{territorialRef("B"),box(6,0,2,10)},{territorialRef("C"),box(8,0,12,10)}}};}
PrepareResult prepareAny(Project& p,const TerritorialMutationIntent& intent){auto plan=CommandProcessor::planTerritorial(p,intent);if(!plan.ok()){PrepareResult failed;failed.error=plan.error;failed.detail=plan.detail;return failed;}JobScheduler jobs;const auto ticket=jobs.enqueue(p.snapshot(),"history-test");jobs.takeNext();return prepareTerritorialGeometry(p.snapshot(),*plan.plan,ticket.token());}
}
class BoundaryHierarchyTests:public QObject {
    Q_OBJECT
private slots:
    void lockedUnchangedDescendantIsAllowed() {
        auto d=roots();child(d,"fixed","A",box(1,1,2,2),true);Project p;p.replace(d);const auto before=projectcodec::encode(p);
        auto result=prepare(p,shifted());QVERIFY2(result.ok(),result.detail.c_str());QVERIFY(result.preview);QCOMPARE(projectcodec::encode(p),before);
        QCOMPARE(staticGeometryBinding(result.preview->change().after(),"fixed").geometryRef,staticGeometryBinding(d,"fixed").geometryRef);
    }
    void recursiveClipAndDeepTransfer() {
        auto d=roots();child(d,"partial","A",box(6,1,3,8));child(d,"clipped","partial",box(7,2,1.5,2));child(d,"deep","partial",box(8.2,6,.5,1));child(d,"deep-child","deep",box(8.3,6.2,.2,.2));Project p;p.replace(d);
        auto result=prepare(p,shifted());QVERIFY2(result.ok(),result.detail.c_str());const auto& after=result.preview->change().after();
        QCOMPARE(planarArea(geometry(after,"partial")),16.);QCOMPARE(planarArea(geometry(after,"clipped")),2.);
        QCOMPARE(staticParentRelation(after,"deep").parentId,std::string("B"));QCOMPARE(staticParentRelation(after,"deep-child").parentId,std::string("deep"));
    }
    void rootMoveRejectsLockedInheritedRootChange() {
        auto d=roots();child(d,"whole","A",box(8.2,1,1,3));child(d,"locked","whole",box(8.4,2,.2,.2),true);Project p;p.replace(d);const auto before=projectcodec::encode(p);
        auto result=prepare(p,shifted());QVERIFY(!result.ok());QCOMPARE(projectcodec::encode(p),before);
    }
    void rootRemovalCanRescueGrandchildToAnotherRoot() {
        ProjectDocument d({{"A","A",box(0,0,10,10).polygons,0},{"B","B",box(10,0,5,10).polygons,0},{"C","C",box(15,0,5,10).polygons,0}},{{"countries","Countries"}});
        child(d,"removed","A",box(7,1,2,8));child(d,"rescued","removed",box(8.2,2,.4,1));child(d,"also-removed","removed",box(7.5,4,1,1));
        d.presentation.webPresentation.hiddenItems["subunits"].insert("removed");d.presentation.webPresentation.objectStyles[territorialPresentationKey("removed")]={};d.presentation.webPresentation.objectOrder.push_back(territorialPresentationKey("removed"));
        Project p;p.replace(d);const auto before=projectcodec::encode(p);SharedBoundaryIntent in{{{territorialRef("A"),box(0,0,6,10)},{territorialRef("B"),box(6,0,2,10)},{territorialRef("C"),box(8,0,12,10)}}};
        auto result=prepare(p,in);QVERIFY2(result.ok(),result.detail.c_str());const auto& after=result.preview->change().after();QVERIFY(!exists(after,"removed"));QVERIFY(!exists(after,"also-removed"));QVERIFY(exists(after,"rescued"));QCOMPARE(staticParentRelation(after,"rescued").parentId,std::string("C"));
        QVERIFY(!after.presentation.webPresentation.hiddenItems.count("subunits")||!after.presentation.webPresentation.hiddenItems.at("subunits").count("removed"));QVERIFY(!after.presentation.webPresentation.objectStyles.count(territorialPresentationKey("removed")));QVERIFY(std::find(after.presentation.webPresentation.objectOrder.begin(),after.presentation.webPresentation.objectOrder.end(),territorialPresentationKey("removed"))==after.presentation.webPresentation.objectOrder.end());
        QVERIFY(CommandProcessor::confirm(p,*result.preview).ok());const auto committed=projectcodec::encode(p);QVERIFY(p.undo());QCOMPARE(projectcodec::encode(p),before);QVERIFY(p.redo());QCOMPARE(projectcodec::encode(p),committed);
    }
    void siblingMoveKeepsLockedGrandchild() {
        ProjectDocument d({{"P","P",box(0,0,20,10).polygons,0}},{{"countries","Countries"}});child(d,"A","P",box(0,0,10,10));child(d,"B","P",box(10,0,10,10));child(d,"whole","A",box(8.2,1,1,3));child(d,"locked","whole",box(8.4,2,.2,.2),true);Project p;p.replace(d);
        auto result=prepare(p,shifted());QVERIFY2(result.ok(),result.detail.c_str());const auto& after=result.preview->change().after();QCOMPARE(staticParentRelation(after,"whole").parentId,std::string("B"));QCOMPARE(staticParentRelation(after,"locked").parentId,std::string("whole"));QCOMPARE(staticGeometryBinding(after,"locked").geometryRef,staticGeometryBinding(d,"locked").geometryRef);
    }
    void gainedOverlapAgainstUnselectedRootRejects() {
        auto d=roots();const GeometryRef g{"C",1};d.geometries.insert(g,box(9,1,2,2));appendTerritory(d,{"C","C","",UnitKind::General,false},g);d.presentation.objectStyles[territorialRef("C")]={};Project p;p.replace(d);
        auto result=prepare(p,shifted());QVERIFY(!result.ok());QCOMPARE(result.detail,std::string("BOUNDARY_OWNER_OVERLAP"));
    }
    void siblingRemovalDropsSubtreeAndRewritesReferences() {
        ProjectDocument d({{"P","P",box(0,0,20,10).polygons,0}},{{"countries","Countries"}});
        child(d,"A","P",box(0,0,10,10));child(d,"B","P",box(10,0,5,10));child(d,"C","P",box(15,0,5,10));child(d,"removed","A",box(7,1,2,8));child(d,"deep","removed",box(8.2,2,.4,1));
        DistributionLayer layer;layer.id="distribution";layer.name="Distribution";d.distributionLayers.push_back(layer);DistributionEntry entry;entry.id="entry";entry.layerId=layer.id;entry.territory=territorialRef("removed");entry.value=1;d.distributionEntries.push_back(entry);
        Geometry point;point.type="Point";point.points={{8,2}};d.geometries.insert({"label",1},point);PlaceLabel label;label.id="label";label.name="Label";label.geometry={"label",1};label.territory=territorialRef("removed");d.labels.push_back(label);d.presentation.webPresentation.labelSettings[territorialRef("removed")]={};
        PreservedExtension extension;extension.id="generic";extension.jsonPointer="/genericFeatures";extension.payload=R"([{"properties":{"ownerId":"removed"}},{"properties":{"ownerId":"P"},"number":1e+09}])";extension.dependencyKnowledge="known";extension.dependencies={territorialRef("removed"),territorialRef("P")};extension.forbiddenEffects={"geometry","relation","delete"};d.extensions.push_back(extension);
        Project p;p.replace(d);const auto before=projectcodec::encode(p);SharedBoundaryIntent in{{{territorialRef("A"),box(0,0,6,10)},{territorialRef("B"),box(6,0,2,10)},{territorialRef("C"),box(8,0,12,10)}}};
        auto result=prepare(p,in);QVERIFY2(result.ok(),result.detail.c_str());const auto& after=result.preview->change().after();QVERIFY(!exists(after,"removed"));QVERIFY(!exists(after,"deep"));QVERIFY(after.distributionEntries.empty());QVERIFY(!after.labels.front().territory);QVERIFY(!after.presentation.webPresentation.labelSettings.count(territorialRef("removed")));QCOMPARE(after.extensions.front().payload,std::string(R"([{"number":1e+09,"properties":{"ownerId":"P"}}])"));QCOMPARE(projectcodec::encode(p),before);
    }
    void recursiveSiblingClipRemovesGrandchildWithoutReparentingIt() {
        ProjectDocument d({{"P","P",box(0,0,20,10).polygons,0}},{{"countries","Countries"}});child(d,"A","P",box(0,0,10,10));child(d,"B","P",box(10,0,10,10));child(d,"partial","A",box(6,1,3,8));child(d,"deep","partial",box(8.2,2,.4,1));Project p;p.replace(d);
        auto result=prepare(p,shifted());QVERIFY2(result.ok(),result.detail.c_str());const auto& after=result.preview->change().after();QCOMPARE(planarArea(geometry(after,"partial")),16.);QVERIFY(!exists(after,"deep"));
    }
    void receiptIsDetachedAndRejectsStaleOrCancelledWork() {
        auto d=roots();child(d,"whole","A",box(8.2,1,1,3));Project p;p.replace(d);const auto before=projectcodec::encode(p);JobScheduler jobs;const auto ticket=jobs.enqueue(p.snapshot(),"test");jobs.takeNext();
        const auto receipt=calculateBoundaryGeometryPreview(p.snapshot(),shifted(),ticket.token());QVERIFY2(receipt.ok(),receipt.detail.c_str());QCOMPARE(receipt.rows.size(),std::size_t(3));QCOMPARE(receipt.reparented.size(),std::size_t(1));QCOMPARE(receipt.reparented.front().to,territorialRef("B"));QCOMPARE(projectcodec::encode(p),before);
        QVERIFY(p.renameCountry("A","changed"));auto stale=prepareBoundaryGeometryCommit(p.snapshot(),receipt,ticket.token());QCOMPARE(stale.error,CommandError::StaleRevision);
        jobs.close();auto cancelled=calculateBoundaryGeometryPreview(p.snapshot(),shifted(),ticket.token());QCOMPARE(cancelled.status,GeometryOperationStatus::Cancelled);QVERIFY(!cancelled.plan());QVERIFY(cancelled.patch().replacements.empty());
    }
    void coreRejectsRemovingAnUnchangedContainedDescendant() {
        auto d=roots();child(d,"unchanged","A",box(1,1,2,2));Project p;p.replace(d);JobScheduler jobs;const auto ticket=jobs.enqueue(p.snapshot(),"test");jobs.takeNext();auto receipt=calculateBoundaryGeometryPreview(p.snapshot(),shifted(),ticket.token());QVERIFY2(receipt.ok(),receipt.detail.c_str());
        auto patch=receipt.patch();patch.replacements.erase(std::remove_if(patch.replacements.begin(),patch.replacements.end(),[](const auto& row){return row.owner==territorialRef("unchanged");}),patch.replacements.end());patch.removedGeometryOwners.push_back(territorialRef("unchanged"));
        CommandArguments args;args.action=ApplyTerritorialMutation{*receipt.plan(),patch};const auto result=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"territorial.geometry.commit",args));QVERIFY(!result.ok());
    }
    void actualLockedGeometryOrParentChangeRejects() {
        for(const bool transfer:{false,true}){auto d=roots();child(d,"locked","A",transfer?box(8.2,1,1,3):box(7,1,2,3),true);Project p;p.replace(d);const auto before=projectcodec::encode(p);auto result=prepare(p,shifted());QVERIFY(!result.ok());QCOMPARE(result.error,CommandError::Locked);QCOMPARE(projectcodec::encode(p),before);}
    }
    void transferredChildCannotOverlapDestinationPartitionSibling() {
        ProjectDocument d({{"A","A",box(0,0,11,10).polygons,0},{"B","B",box(10,0,10,10).polygons,0}},{{"countries","Countries"}});child(d,"moved","A",box(10.2,1,.6,2));child(d,"resident","B",box(10.3,1,1,2));Project p;p.replace(d);
        SharedBoundaryIntent in{{{territorialRef("A"),box(0,0,11,10)},{territorialRef("B"),box(10,0,10,10)}}};auto result=prepare(p,in);QVERIFY(!result.ok());
    }
    void coreIndependentlyRejectsInheritedRootLockChange() {
        auto d=roots();child(d,"whole","A",box(8.2,1,1,3));child(d,"locked","whole",box(8.4,2,.2,.2),true);Project p;p.replace(d);const auto before=projectcodec::encode(p);const auto intent=shifted();auto plan=CommandProcessor::planTerritorial(p,intent);QVERIFY2(plan.ok(),plan.detail.c_str());
        GeometryPatch patch;patch.sourceRevision=p.revision();for(const auto& mapping:plan.plan->geometry.replacements){const auto draft=std::find_if(intent.drafts.begin(),intent.drafts.end(),[&](const auto& value){return value.owner==mapping.source;});patch.replacements.push_back({mapping.result,draft==intent.drafts.end()?geometry(d,mapping.source.id):draft->geometry});}
        CommandArguments args;args.action=ApplyTerritorialMutation{*plan.plan,patch};const auto result=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"territorial.geometry.commit",args));QVERIFY(!result.ok());QCOMPARE(result.error,CommandError::Locked);QCOMPARE(projectcodec::encode(p),before);
    }
    void coreRejectsForgedUnchangedDescendantGeometry_data() {
        QTest::addColumn<bool>("overlap");QTest::newRow("new sibling overlap")<<true;QTest::newRow("silent shrink")<<false;
    }
    void coreRejectsForgedUnchangedDescendantGeometry() {
        QFETCH(bool,overlap);auto d=roots();child(d,"child","A",box(1,1,2,2));child(d,"resident","A",box(4,1,2,2));Project p;p.replace(d);const auto before=projectcodec::encode(p);const auto intent=shifted();auto plan=CommandProcessor::planTerritorial(p,intent);QVERIFY(plan.ok());GeometryPatch patch;patch.sourceRevision=p.revision();
        for(const auto& mapping:plan.plan->geometry.replacements){const auto draft=std::find_if(intent.drafts.begin(),intent.drafts.end(),[&](const auto& value){return value.owner==mapping.source;});patch.replacements.push_back({mapping.result,mapping.source==territorialRef("child")?(overlap?box(4,1,2,2):box(1.5,1.5,.1,.1)):draft==intent.drafts.end()?geometry(d,mapping.source.id):draft->geometry});}
        CommandArguments args;args.action=ApplyTerritorialMutation{*plan.plan,patch};const auto result=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"territorial.geometry.commit",args));QVERIFY(!result.ok());QCOMPARE(projectcodec::encode(p),before);
    }
    void coreRejectsBoundaryOperationSpoofing() {
        auto d=roots();child(d,"child","A",box(1,1,2,2));Project p;p.replace(d);const auto before=projectcodec::encode(p);const auto intent=shifted();auto plan=CommandProcessor::planTerritorial(p,intent);QVERIFY(plan.ok());GeometryPatch patch;patch.sourceRevision=p.revision();for(const auto& draft:intent.drafts)patch.replacements.push_back({draft.owner,draft.geometry});patch.removedGeometryOwners={territorialRef("child")};plan.plan->geometry.operation="coast";
        CommandArguments args;args.action=ApplyTerritorialMutation{*plan.plan,patch};const auto result=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"territorial.geometry.commit",args));QVERIFY(!result.ok());QCOMPARE(projectcodec::encode(p),before);
    }
    void coreRejectsClipGrowthOutsideOriginal() {
        auto d=roots();child(d,"child","A",box(7,1,2,2));Project p;p.replace(d);const auto intent=shifted();auto plan=CommandProcessor::planTerritorial(p,intent);QVERIFY(plan.ok());GeometryPatch patch;patch.sourceRevision=p.revision();for(const auto& draft:intent.drafts)patch.replacements.push_back({draft.owner,draft.geometry});patch.replacements.push_back({territorialRef("child"),box(5,1,3,2)});
        CommandArguments args;args.action=ApplyTerritorialMutation{*plan.plan,patch};const auto result=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"territorial.geometry.commit",args));QVERIFY(!result.ok());
    }
    void coreRejectsTransferredPartitionSiblingOverlap() {
        ProjectDocument d({{"A","A",box(0,0,11,10).polygons,0},{"B","B",box(10,0,10,10).polygons,0}},{{"countries","Countries"}});child(d,"moved","A",box(10.2,1,.6,2));child(d,"resident","B",box(10.3,1,1,2));Project p;p.replace(d);SharedBoundaryIntent intent{{{territorialRef("A"),box(0,0,11,10)},{territorialRef("B"),box(10,0,10,10)}}};auto plan=CommandProcessor::planTerritorial(p,intent);QVERIFY(plan.ok());GeometryPatch patch;patch.sourceRevision=p.revision();for(const auto& mapping:plan.plan->geometry.replacements)patch.replacements.push_back({mapping.result,geometry(d,mapping.source.id)});
        CommandArguments args;args.action=ApplyTerritorialMutation{*plan.plan,patch};const auto result=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"territorial.geometry.commit",args));QVERIFY(!result.ok());
    }
    void defaultReceiptHasNoCommitAuthority() {
        Project p;p.replace(roots());JobScheduler jobs;const auto ticket=jobs.enqueue(p.snapshot(),"test");jobs.takeNext();BoundaryGeometryPreviewResult forged;forged.status=GeometryOperationStatus::Completed;forged.error=CommandError::None;QVERIFY(!forged.ok());QVERIFY(forged.blocking());QVERIFY(!forged.plan());QVERIFY(forged.patch().replacements.empty());QVERIFY(!prepareBoundaryGeometryCommit(p.snapshot(),forged,ticket.token()).ok());
    }
    void rejectedReceiptCannotBePromotedByDiagnosticFields() {
        Project p;p.replace(roots());JobScheduler jobs;const auto ticket=jobs.enqueue(p.snapshot(),"test");jobs.takeNext();auto invalid=shifted();invalid.drafts.front().geometry=box(0,0,7,10);auto receipt=calculateBoundaryGeometryPreview(p.snapshot(),invalid,ticket.token());QVERIFY(!receipt.ok());receipt.status=GeometryOperationStatus::Completed;receipt.error=CommandError::None;receipt.detail.clear();receipt.issues.clear();QVERIFY(!receipt.ok());QVERIFY(!prepareBoundaryGeometryCommit(p.snapshot(),receipt,ticket.token()).ok());
    }
    void receiptDiagnosticsCannotChangeValidatedCommit() {
        Project p;p.replace(roots());JobScheduler jobs;const auto ticket=jobs.enqueue(p.snapshot(),"test");jobs.takeNext();auto receipt=calculateBoundaryGeometryPreview(p.snapshot(),shifted(),ticket.token());QVERIFY(receipt.ok());receipt.rows.front().after=box(0,0,1,1);receipt.rows.clear();receipt.reparented.clear();receipt.status=GeometryOperationStatus::Failed;receipt.error=CommandError::Locked;receipt.detail="untrusted diagnostic";receipt.issues.push_back({"diagnostic",{},"untrusted",true});
        auto result=prepareBoundaryGeometryCommit(p.snapshot(),receipt,ticket.token());QVERIFY2(result.ok(),result.detail.c_str());QVERIFY(result.preview);QCOMPARE(planarArea(geometry(result.preview->change().after(),"A")),80.);QCOMPARE(planarArea(geometry(result.preview->change().after(),"B")),120.);
    }
    void retainedCanonicalKeysRewriteExactly_data() {QTest::addColumn<bool>("replace");QTest::newRow("delete")<<false;QTest::newRow("replace")<<true;}
    void retainedCanonicalKeysRewriteExactly() {
        QFETCH(bool,replace);ProjectDocument d;TerritorialMutationPlan plan;const auto operation=replace?ReferenceRewriteOperation::ReplaceId:ReferenceRewriteOperation::DeleteKey;plan.rewrites={{"/labelSettings",operation,"gone",replace?"next":""},{"/itemVisibility",operation,"gone",replace?"next":""}};
        PreservedExtension labels;labels.id="labels";labels.jsonPointer="/labelSettings";labels.dependencyKnowledge="known";labels.dependencies={territorialRef("gone"),territorialRef("keep")};labels.payload=R"({"territorial:gone":{"pinned":true},"territorial:entity:gone":{"pinned":false},"country:gone":{"priority":2},"territorial:keep":{"pinned":false},"label:gone":{"pinned":true},"territorial:gone-extra":{"pinned":false}})";
        PreservedExtension visibility;visibility.id="visibility";visibility.jsonPointer="/itemVisibility";visibility.dependencyKnowledge="known";visibility.dependencies={territorialRef("gone"),territorialRef("keep")};visibility.payload=R"({"subunits":{"gone":false,"keep":false},"countryLabels":{"territorial:gone":false,"territorial:keep":false,"territorial:gone-extra":false,"label:gone":false}})";std::vector<PreservedExtension> candidate{labels,visibility};const auto result=retainedrefs::rewrite(d,plan,candidate);QVERIFY2(result.ok,result.detail.c_str());const auto labelPayload=QJsonDocument::fromJson(QByteArray::fromStdString(candidate[0].payload)).object(),visibilityPayload=QJsonDocument::fromJson(QByteArray::fromStdString(candidate[1].payload)).object();
        QVERIFY(!labelPayload.contains("territorial:gone"));QVERIFY(!labelPayload.contains("territorial:entity:gone"));QVERIFY(!labelPayload.contains("country:gone"));QVERIFY(labelPayload.contains("territorial:keep"));QVERIFY(labelPayload.contains("territorial:gone-extra"));QVERIFY(labelPayload.contains("label:gone"));const auto hidden=visibilityPayload["countryLabels"].toObject();QVERIFY(!hidden.contains("territorial:gone"));QVERIFY(hidden.contains("territorial:keep"));QVERIFY(hidden.contains("territorial:gone-extra"));QVERIFY(hidden.contains("label:gone"));QVERIFY(!visibilityPayload["subunits"].toObject().contains("gone"));
        if(replace){QVERIFY(labelPayload["territorial:next"].toObject()["pinned"].toBool());QVERIFY(labelPayload.contains("territorial:entity:next"));QVERIFY(labelPayload.contains("country:next"));QVERIFY(hidden.contains("territorial:next"));QVERIFY(visibilityPayload["subunits"].toObject().contains("next"));}
        for(const auto& extension:candidate){QVERIFY(std::find(extension.dependencies.begin(),extension.dependencies.end(),territorialRef("keep"))!=extension.dependencies.end());QVERIFY(std::find(extension.dependencies.begin(),extension.dependencies.end(),territorialRef("gone"))==extension.dependencies.end());if(replace)QVERIFY(std::find(extension.dependencies.begin(),extension.dependencies.end(),territorialRef("next"))!=extension.dependencies.end());}
    }
    void acceptedTinyPartitionOverlapRemainsCommittable_data(){QTest::addColumn<bool>("hole");QTest::newRow("rectangle")<<false;QTest::newRow("hole defers")<<true;}
    void acceptedTinyPartitionOverlapRemainsCommittable() {
        QFETCH(bool,hole);ProjectDocument d({{"A","A",box(0,0,50,40).polygons,0},{"B","B",box(50,0,50,40).polygons,0}},{{"countries","Countries"}});auto left=box(1,1,10,10);if(hole)left.polygons.front().push_back(box(2,2,1,1).polygons.front().front());child(d,"left","A",left);child(d,"right","A",box(11-5e-9,1,10,10));Project p;p.replace(d);JobScheduler jobs;const auto ticket=jobs.enqueue(p.snapshot(),"test");jobs.takeNext();SharedBoundaryIntent intent{{{territorialRef("A"),box(0,0,45,40)},{territorialRef("B"),box(45,0,55,40)}}};auto receipt=calculateBoundaryGeometryPreview(p.snapshot(),intent,ticket.token());QVERIFY2(receipt.ok(),receipt.detail.c_str());auto prepared=prepareBoundaryGeometryCommit(p.snapshot(),receipt,ticket.token());QVERIFY2(prepared.ok(),prepared.detail.c_str());
    }
    void acceptedTinyWrappedPartitionOverlapRemainsCommittable() {
        auto country=box(160,0,20,40);country.type="MultiPolygon";country.polygons.push_back(box(-180,0,5,40).polygons.front());ProjectDocument d({{"A","A",country.polygons,0},{"B","B",box(-175,0,25,40).polygons,0}},{{"countries","Countries"}});auto left=box(175,1,5,10);left.type="MultiPolygon";left.polygons.push_back(box(-180,1,5,10).polygons.front());child(d,"left","A",left);child(d,"right","A",box(165,1,10+5e-9,10));Project p;p.replace(d);JobScheduler jobs;const auto ticket=jobs.enqueue(p.snapshot(),"test");jobs.takeNext();auto expanded=box(160,0,20,40);expanded.type="MultiPolygon";expanded.polygons.push_back(box(-180,0,6,40).polygons.front());SharedBoundaryIntent intent{{{territorialRef("A"),expanded},{territorialRef("B"),box(-174,0,24,40)}}};auto receipt=calculateBoundaryGeometryPreview(p.snapshot(),intent,ticket.token());QVERIFY2(receipt.ok(),receipt.detail.c_str());auto prepared=prepareBoundaryGeometryCommit(p.snapshot(),receipt,ticket.token());QVERIFY2(prepared.ok(),prepared.detail.c_str());
    }
    void retainedVisibilityPreservesLiteralCustomLabelIds() {
        ProjectDocument d;TerritorialMutationPlan plan;plan.rewrites={{"/itemVisibility",ReferenceRewriteOperation::DeleteKey,"gone",""}};PreservedExtension visibility;visibility.id="visibility";visibility.jsonPointer="/itemVisibility";visibility.dependencyKnowledge="known";visibility.dependencies={territorialRef("gone"),{"label","territorial:gone"}};visibility.payload=R"({"countryLabels":{"territorial:gone":false},"labels":{"territorial:gone":false}})";std::vector<PreservedExtension> candidate{visibility};const auto result=retainedrefs::rewrite(d,plan,candidate);QVERIFY2(result.ok,result.detail.c_str());const auto payload=QJsonDocument::fromJson(QByteArray::fromStdString(candidate.front().payload)).object();QVERIFY(!payload["countryLabels"].toObject().contains("territorial:gone"));QVERIFY(payload["labels"].toObject().contains("territorial:gone"));QCOMPARE(candidate.front().dependencies,std::vector<ObjectRef>({{"label","territorial:gone"}}));
    }
    void boundaryHistoryKeepsTerritorialLabelSettingsLive_data(){QTest::addColumn<bool>("presentationEdit");QTest::newRow("no presentation rebase")<<false;QTest::newRow("surviving presentation edits")<<true;}
    void boundaryHistoryKeepsTerritorialLabelSettingsLive() {
        QFETCH(bool,presentationEdit);Project p;p.replace(historyFixture());auto prepared=prepare(p,deletingBoundary());QVERIFY2(prepared.ok(),prepared.detail.c_str());QVERIFY(CommandProcessor::confirm(p,*prepared.preview).ok());QVERIFY(!p.document().presentation.webPresentation.labelSettings.count(territorialRef("gone")));
        if(presentationEdit){LabelSettings territorial;territorial.priority=9;QCOMPARE(PresentationCommandProcessor::apply(p,SetLabelSettings{territorialRef("A"),territorial}),PresentationResult::Applied);LabelSettings custom;custom.priority=8;QCOMPARE(PresentationCommandProcessor::apply(p,SetLabelSettings{{"label","territorial:gone"},custom}),PresentationResult::Applied);QCOMPARE(PresentationCommandProcessor::apply(p,SetScopedVisibility{"countries",{territorialRef("C")},false}),PresentationResult::Applied);}
        QVERIFY(p.undo());QVERIFY(exists(p.document(),"gone"));QVERIFY(exists(p.document(),"gone-deep"));const auto& undo=p.document().presentation.webPresentation;QVERIFY(!undo.labelSettings.count(territorialRef("gone")));QVERIFY(!undo.labelSettings.count(territorialRef("gone-deep")));QCOMPARE(*undo.labelSettings.at(territorialRef("A")).priority,presentationEdit?9.:5.);QCOMPARE(*undo.labelSettings.at({"label","territorial:gone"}).priority,presentationEdit?8.:6.);QVERIFY(!itemVisible(undo,"subunits","gone"));QCOMPARE(*undo.objectStyles.at(territorialPresentationKey("gone")).opacity,.3);if(presentationEdit)QVERIFY(!itemVisible(undo,"countries","C"));
        QVERIFY(p.redo());QVERIFY(!exists(p.document(),"gone"));QVERIFY(!p.document().presentation.webPresentation.labelSettings.count(territorialRef("gone")));QCOMPARE(*p.document().presentation.webPresentation.labelSettings.at(territorialRef("A")).priority,presentationEdit?9.:5.);QVERIFY(p.undo());QVERIFY(!p.document().presentation.webPresentation.labelSettings.count(territorialRef("gone")));
    }
    void nonBoundaryCustomLabelHistoryStillRestoresSettings() {
        Project p;p.replace(historyFixture());CommandArguments args;args.action=ContentEdit{{"label","territorial:gone"},{},{},false};auto result=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"content.edit",args));QVERIFY2(result.ok(),result.detail.c_str());QVERIFY(CommandProcessor::confirm(p,*result.preview).ok());QVERIFY(!p.document().presentation.webPresentation.labelSettings.count({"label","territorial:gone"}));QVERIFY(!p.nextUndoTerritorialMutationKind());QVERIFY(p.undo());QCOMPARE(*p.document().presentation.webPresentation.labelSettings.at({"label","territorial:gone"}).priority,6.);QVERIFY(p.redo());QVERIFY(!p.document().presentation.webPresentation.labelSettings.count({"label","territorial:gone"}));
    }
    void nonBoundaryAnnexAndSplitHistoryRemainUnchanged() {
        ProjectDocument d({{"P","P",box(0,0,20,10).polygons,0}},{{"countries","Countries"}});child(d,"A","P",box(0,0,10,10));child(d,"B","P",box(10,0,10,10));d.presentation.webPresentation.labelSettings[territorialRef("B")].priority=7;Project p;p.replace(d);const auto before=projectcodec::encode(p);auto annex=prepareAny(p,AnnexTerritoryIntent{territorialRef("A"),{territorialRef("B")},box(10,0,10,10)});QVERIFY2(annex.ok(),annex.detail.c_str());QVERIFY(CommandProcessor::confirm(p,*annex.preview).ok());QCOMPARE(p.nextUndoTerritorialMutationKind(),std::optional<TerritorialMutationKind>(TerritorialMutationKind::AnnexTerritory));QVERIFY(p.undo());QCOMPARE(projectcodec::encode(p),before);QCOMPARE(*p.document().presentation.webPresentation.labelSettings.at(territorialRef("B")).priority,7.);
        auto split=prepareAny(p,SplitTerritorialIntent{territorialRef("B"),box(15,0,5,10),"new","New"});QVERIFY2(split.ok(),split.detail.c_str());QVERIFY(CommandProcessor::confirm(p,*split.preview).ok());QCOMPARE(p.nextUndoTerritorialMutationKind(),std::optional<TerritorialMutationKind>(TerritorialMutationKind::SplitTerritorial));QVERIFY(p.undo());QCOMPARE(projectcodec::encode(p),before);
    }
    void historyKindQueriesFollowStoredActionCursor() {
        Project p;p.replace(roots());QVERIFY(!p.nextUndoTerritorialMutationKind());QVERIFY(!p.nextRedoTerritorialMutationKind());auto prepared=prepare(p,shifted());QVERIFY(prepared.ok());QVERIFY(CommandProcessor::confirm(p,*prepared.preview).ok());QCOMPARE(p.nextUndoTerritorialMutationKind(),std::optional<TerritorialMutationKind>(TerritorialMutationKind::ReconcileSharedBoundary));QVERIFY(p.undo());QVERIFY(!p.nextUndoTerritorialMutationKind());QCOMPARE(p.nextRedoTerritorialMutationKind(),std::optional<TerritorialMutationKind>(TerritorialMutationKind::ReconcileSharedBoundary));QVERIFY(p.redo());QVERIFY(p.renameCountry("A","renamed"));QVERIFY(!p.nextUndoTerritorialMutationKind());QVERIFY(p.undo());QCOMPARE(p.nextUndoTerritorialMutationKind(),std::optional<TerritorialMutationKind>(TerritorialMutationKind::ReconcileSharedBoundary));QVERIFY(!p.nextRedoTerritorialMutationKind());
    }
    void existingRootOverlapWithoutGainIsNotNewOverlap() {
        ProjectDocument d({{"A","A",box(0,0,11,10).polygons,0},{"B","B",box(10,0,10,10).polygons,0}},{{"countries","Countries"}});Project p;p.replace(d);
        SharedBoundaryIntent in{{{territorialRef("A"),box(0,0,11,10)},{territorialRef("B"),box(10,0,10,10)}}};auto result=prepare(p,in);QVERIFY2(result.ok(),result.detail.c_str());QCOMPARE(result.status,CommandStatus::NoOp);
    }
};
QTEST_GUILESS_MAIN(BoundaryHierarchyTests)
#include "m974_boundary_hierarchy_tests.moc"
