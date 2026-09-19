#include "retainedreferencerewriter.h"
#include "projectcodec.h"
#include <pandoeditor/commands.h>
#include <pandoeditor/project.h>
#include <QtTest>
using namespace pandoeditor;
class RetainedReferenceTests: public QObject { Q_OBJECT
private slots:
 void rewriteDistributionPreservesScalars() {
  ProjectDocument before;TerritorialMutationPlan plan;plan.rewrites={{"/distributionEntries",ReferenceRewriteOperation::ReplaceId,"A","B"}};
  PreservedExtension e;e.id="distribution";e.jsonPointer="/distributionEntries";e.payload=R"([{"territorialUnitId":"A","large":9007199254740993,"nil":null},{"territorialUnitId":"Z"}])";std::vector<PreservedExtension> candidate{e};
  const auto result=retainedrefs::rewrite(before,plan,candidate);QVERIFY(result.ok);QCOMPARE(QString::fromStdString(candidate.front().payload),QString(R"([{"large":9007199254740993,"nil":null,"territorialUnitId":"B"},{"territorialUnitId":"Z"}])"));QCOMPARE(candidate.front().dependencies.size(),std::size_t(2));
 }
 void unknownPathRejects(){ProjectDocument before;TerritorialMutationPlan plan;plan.rewrites={{"/unsafe",ReferenceRewriteOperation::DeleteKey,"A",""}};PreservedExtension e;e.id="unsafe";e.jsonPointer="/unsafe";e.payload="{}";std::vector<PreservedExtension> candidate{e};QVERIFY(!retainedrefs::rewrite(before,plan,candidate).ok);}
 void deleteRewritesPresentationKeys() {
  ProjectDocument before;TerritorialMutationPlan plan;plan.rewrites={{"/itemVisibility",ReferenceRewriteOperation::DeleteKey,"S",""},{"/labelSettings",ReferenceRewriteOperation::DeleteKey,"S",""}};
  PreservedExtension visibility;visibility.id="visibility";visibility.jsonPointer="/itemVisibility";visibility.payload=R"({"subunits":{"S":false,"other":true},"countries":{"S":false}})";
  PreservedExtension labels;labels.id="labels";labels.jsonPointer="/labelSettings";labels.payload=R"({"subunit:S":{"pinned":true},"territorial:subunit:S":{"pinned":false},"label:S":{"pinned":true}})";
  std::vector<PreservedExtension> candidate{visibility,labels};const auto result=retainedrefs::rewrite(before,plan,candidate);QVERIFY(result.ok);
  QCOMPARE(QString::fromStdString(candidate[0].payload),QString(R"({"countries":{},"subunits":{"other":true}})"));
  QCOMPARE(QString::fromStdString(candidate[1].payload),QString(R"({"label:S":{"pinned":true}})"));
 }
 void deleteRewritesCandidateAndUndoRestores() {
  Geometry g;g.type="Polygon";g.polygons.push_back(Polygon{Ring{{0,0},{3,0},{3,3},{0,3},{0,0}}});ProjectDocument d({{"A","A",g.polygons,0x112233,"",1,"countries"}},{{"countries","Countries"}});
  auto geo=GeometryRef{"s",1};d.geometries.insert(geo,g);d.units.push_back({"S","S","",UnitKind::Region,geo});d.presentation.membership[territorialRef("S")]="countries";d.presentation.objectStyles[territorialRef("S")]={};
  PreservedExtension ext;ext.id="distribution";ext.jsonPointer="/distributionEntries";ext.payload=R"([{"territorialUnitId":"S","share":9007199254740993}])";ext.dependencyKnowledge="known";ext.dependencies={territorialRef("S")};ext.forbiddenEffects={"delete"};d.extensions.push_back(ext);
  Project project;project.replace(d);auto plan=CommandProcessor::planTerritorial(project,DeleteTerritorialIntent{{territorialRef("S")}});QVERIFY(plan.ok());CommandArguments args;args.action=ApplyTerritorialMutation{*plan.plan,{}};auto request=CommandProcessor::makeRequest(project,"territorial.delete",args);
  auto callback=[](const ProjectDocument& before,const TerritorialMutationPlan& mutation,std::vector<PreservedExtension>& candidate){auto r=retainedrefs::rewrite(before,mutation,candidate);return ExtensionRewriteResult{r.ok,r.detail,r.handledExtensionIds};};auto prepared=CommandProcessor::prepare(project,request,callback);QVERIFY(prepared.ok());QVERIFY(CommandProcessor::confirm(project,*prepared.preview).ok());QVERIFY(!project.index().objects.count(territorialRef("S")));QCOMPARE(project.document().extensions.front().payload,std::string("[]"));QVERIFY(project.undo());QVERIFY(project.index().objects.count(territorialRef("S")));
 }
 void staleDeletePreviewLeavesDocumentUntouched() {
  Geometry g;g.type="Polygon";g.polygons.push_back(Polygon{Ring{{0,0},{3,0},{3,3},{0,3},{0,0}}});ProjectDocument d({{"A","A",g.polygons,0x112233,"",1,"countries"}},{{"countries","Countries"}});auto geo=GeometryRef{"s",1};d.geometries.insert(geo,g);d.units.push_back({"S","S","",UnitKind::Region,geo});d.presentation.membership[territorialRef("S")]="countries";d.presentation.objectStyles[territorialRef("S")]={};Project project;project.replace(d);
  auto plan=CommandProcessor::planTerritorial(project,DeleteTerritorialIntent{{territorialRef("S")}});QVERIFY(plan.ok());CommandArguments args;args.action=ApplyTerritorialMutation{*plan.plan,{}};auto deletion=CommandProcessor::makeRequest(project,"territorial.delete",args);auto prepared=CommandProcessor::prepare(project,deletion);QVERIFY(prepared.ok());QVERIFY(project.setColor("A",0x445566));const auto revision=project.revision();const auto result=CommandProcessor::confirm(project,*prepared.preview);QCOMPARE(result.error,CommandError::StaleRevision);QCOMPARE(project.revision(),revision);QVERIFY(project.index().objects.count(territorialRef("S")));
 }
 void deleteThenV4RoundTripPreservesRewrittenExtension() {
  Geometry g;g.type="Polygon";g.polygons.push_back(Polygon{Ring{{0,0},{3,0},{3,3},{0,3},{0,0}}});ProjectDocument d({{"A","A",g.polygons,0x112233,"",1,"countries"}},{{"countries","Countries"}});
  for(const auto id:{"S","R"}) {auto ref=GeometryRef{std::string("geometry-")+id,1};d.geometries.insert(ref,g);d.units.push_back({id,id,"",UnitKind::Region,ref});d.presentation.membership[territorialRef(id)]="countries";d.presentation.objectStyles[territorialRef(id)]={};}
  PreservedExtension ext;ext.id="distribution";ext.jsonPointer="/distributionEntries";ext.payload=R"([{"territorialUnitId":"S","share":0.1234567890123456789012345},{"territorialUnitId":"R","share":9007199254740993123456789}])";ext.dependencyKnowledge="known";ext.dependencies={territorialRef("R"),territorialRef("S")};ext.forbiddenEffects={"delete"};d.extensions.push_back(ext);
  Project project;project.replace(d);auto planned=CommandProcessor::planTerritorial(project,DeleteTerritorialIntent{{territorialRef("S")}});QVERIFY(planned.ok());CommandArguments args;args.action=ApplyTerritorialMutation{*planned.plan,{}};auto request=CommandProcessor::makeRequest(project,"territorial.delete",args);
  auto callback=[](const ProjectDocument& before,const TerritorialMutationPlan& mutation,std::vector<PreservedExtension>& candidate){auto r=retainedrefs::rewrite(before,mutation,candidate);return ExtensionRewriteResult{r.ok,r.detail,r.handledExtensionIds};};auto prepared=CommandProcessor::prepare(project,request,callback);QVERIFY(prepared.ok());QVERIFY(CommandProcessor::confirm(project,*prepared.preview).ok());
  const auto saved=projectcodec::encode(project);QVERIFY(saved.contains("9007199254740993123456789"));pandoeditor::Project reopened;reopened.replace(projectcodec::decode(saved));QCOMPARE(projectcodec::encode(reopened),saved);QVERIFY(!reopened.index().objects.count(territorialRef("S")));QVERIFY(reopened.index().objects.count(territorialRef("R")));QCOMPARE(reopened.document().extensions.front().payload,std::string(R"([{"share":9007199254740993123456789,"territorialUnitId":"R"}])"));
 }
 void unknownAndArchiveDependenciesBlockStructuralDelete() {
  Geometry g;g.type="Polygon";g.polygons.push_back(Polygon{Ring{{0,0},{3,0},{3,3},{0,3},{0,0}}});
  for(const auto status:{std::string("unsupported"),std::string("migrationArchive")}) {
   ProjectDocument d({{"A","A",g.polygons,0x112233,"",1,"countries"}},{{"countries","Countries"}});auto geo=GeometryRef{"s",1};d.geometries.insert(geo,g);d.units.push_back({"S","S","",UnitKind::Region,geo});d.presentation.membership[territorialRef("S")]="countries";d.presentation.objectStyles[territorialRef("S")]={};
   PreservedExtension ext;ext.id="opaque";ext.status=status;ext.jsonPointer="/future";ext.payload=R"({"ownerId":"S"})";ext.dependencyKnowledge="unknown";ext.envelopeExtras=status=="migrationArchive"?R"({"future":true})":"{}";d.extensions.push_back(ext);
   Project project;project.replace(d);const auto revision=project.revision();auto planned=CommandProcessor::planTerritorial(project,DeleteTerritorialIntent{{territorialRef("S")}});QCOMPARE(planned.error,CommandError::UnsupportedDependency);QCOMPARE(project.revision(),revision);QVERIFY(project.index().objects.count(territorialRef("S")));
  }
 }
 void conversionRewritesAllKnownPathsLosslessly() {
  ProjectDocument before;TerritorialMutationPlan plan;plan.rewrites={{"/distributionEntries",ReferenceRewriteOperation::ReplaceId,"A","B"},{"/genericFeatures",ReferenceRewriteOperation::ReplaceId,"A","B"},{"/itemVisibility",ReferenceRewriteOperation::ReplaceId,"A","B"},{"/labelSettings",ReferenceRewriteOperation::ReplaceId,"A","B"}};
  std::vector<PreservedExtension> candidate;auto add=[&](std::string id,std::string path,std::string payload){PreservedExtension e;e.id=std::move(id);e.jsonPointer=std::move(path);e.payload=std::move(payload);candidate.push_back(std::move(e));};
  add("d","/distributionEntries",R"([{"territorialUnitId":"A","n":9007199254740993}])");add("g","/genericFeatures",R"([{"properties":{"ownerId":"A"},"raw":1e999}])");add("v","/itemVisibility",R"({"subunits":{"A":false}})");add("l","/labelSettings",R"({"territorial:subunit:A":{"pinned":true}})");
  const auto rewritten=retainedrefs::rewrite(before,plan,candidate);QVERIFY(rewritten.ok);QCOMPARE(rewritten.handledExtensionIds.size(),std::size_t(4));QVERIFY(QByteArray::fromStdString(candidate[0].payload).contains("9007199254740993"));QVERIFY(QByteArray::fromStdString(candidate[1].payload).contains("1e999"));QVERIFY(QByteArray::fromStdString(candidate[2].payload).contains("\"B\""));QVERIFY(QByteArray::fromStdString(candidate[3].payload).contains("territorial:subunit:B"));
 }
};
QTEST_MAIN(RetainedReferenceTests)
#include "retained_reference_tests.moc"
