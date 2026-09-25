#include "editorcontroller.h"
#include "webimport.h"
#include <pandoeditor/project.h>
#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QFile>
using namespace pandoeditor;
static ProjectDocument fixture() {
 ProjectDocument d({{"A","Alpha",{{{{0,0},{8,0},{8,8},{0,8},{0,0}}}},0x336699}},{{"countries","국가"}});
 auto g=d.units.front().geometry;
 d.units.push_back({"S","Child","",UnitKind::Subunit,g});
 d.units.push_back({"R","Region","",UnitKind::Region,g});
 for(auto id:{"S","R"}){d.presentation.membership[territorialRef(id)]="countries";d.presentation.objectStyles[territorialRef(id)]={0,1,false};}
 d.relations.push_back({"S-base",territorialRef("S"),territorialRef("A"),territorialRef("A")});
 d.relations.push_back({"R-base",territorialRef("R"),territorialRef("S"),territorialRef("A")});
 return d;
}
static CommandResult run(Project& p,const std::string& id,CommandAction action) {
 CommandArguments args;args.action=std::move(action);
 auto prepared=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,id,std::move(args)));
 if(prepared.preview)return CommandProcessor::confirm(p,*prepared.preview);
 return {prepared.status,prepared.error,prepared.detail};
}
class PropertyTests: public QObject {
 Q_OBJECT
private slots:
 void defaultColorPreviewAndLegacyMigrationNotice() {
  QTemporaryDir dir;EditorController editor;
  auto json=QJsonDocument::fromJson(editor.documentBytes()).object();json["version"]=3;
  auto units=json["units"].toArray();for(int i=0;i<units.size();++i){auto u=units[i].toObject();u.remove("baseName");u.remove("nameExplicit");units[i]=u;}json["units"]=units;
  const auto path=dir.filePath("legacy-v3.json");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(json).toJson());file.close();
  QVERIFY(editor.openFile(QUrl::fromLocalFile(path)));QVERIFY2(editor.documentNotice().contains(QStringLiteral("상속 의도")),"Flattened legacy colours must report uncertainty, not pretend to recover inheritance");
  editor.selectCountry(editor.countryRows().front().toMap()["id"].toString());
  QCOMPARE(editor.objectProperties()["defaultColor"].toString(),QString("#cccccc"));
 }
 void screenPickerCapabilityIsExplicit() {
  EditorController editor;
  auto picker=editor.property("screenColorPicker").value<QObject*>();
  QVERIFY2(picker,"The screen sampler must expose capability rather than pretending map pixels are whole-screen sampling");
  QVERIFY(picker->property("available").isValid());QVERIFY(!picker->property("busy").toBool());
 }
 void historyAndFileCanonicalizationMatchWebPruning() {
  Project p;p.replace(fixture());auto ref=territorialRef("A");
  QVERIFY(run(p,"territorial.field",TerritorialFieldEdit{ref,TerritorialField::Name,""}).changed());
  QVERIFY(p.document().units[0].nameExplicit);
  QVERIFY(!projectcodec::decode(projectcodec::encode(p)).units[0].nameExplicit);
  QVERIFY(p.undo());QVERIFY(p.redo());QVERIFY(!p.document().units[0].nameExplicit);
  QVERIFY(run(p,"territorial.field",TerritorialFieldEdit{ref,TerritorialField::Notes,"  raw\nnotes  "}).changed());
  p.markSaved();const auto bytes=projectcodec::encode(p);const auto geometry=p.document().units[0].geometry;
  QVERIFY(run(p,"territorial.field",TerritorialFieldEdit{ref,TerritorialField::Name,"Next"}).changed());
  QVERIFY(p.undo());QCOMPARE(p.document().units[0].notes,std::string("raw\nnotes"));
  QCOMPARE(projectcodec::encode(p),bytes);QVERIFY(!p.dirty());QVERIFY(p.document().units[0].geometry==geometry);
  QVERIFY(p.redo());QCOMPARE(p.document().units[0].notes,std::string("raw\nnotes"));
 }
 void emptyTerritorialNamesAreDataNotInvalidUnits() {
  ProjectDocument d({{"A","Alpha",{{{{0,0},{8,0},{8,8},{0,8},{0,0}}}},0xcccccc}},{{"countries","국가"}});
  d.units.front().name.clear();
  bool accepted=true;try{validateDocument(d);}catch(...){accepted=false;}
  QVERIFY2(accepted,"Web empty name must use display fallback, not fail document validation");
 }
 void writesNewSemanticVersion() {
  EditorController editor;
  QCOMPARE(QJsonDocument::fromJson(editor.documentBytes()).object()["version"].toInt(),7);
 }
 void automaticAndExplicitColorHaveDifferentMeaning() {
  Project p;p.replace(fixture());auto ref=territorialRef("S");auto g=p.document().geometries.get(p.document().units[1].geometry);
  QCOMPARE(p.propertyView(ref)->effectiveColor,0x336699u);
  QVERIFY(run(p,"territorial.color",TerritorialColorEdit{{ref},0x336699}).changed());
  QVERIFY(run(p,"territorial.color",TerritorialColorEdit{{territorialRef("A")},0xff0000}).changed());
  QCOMPARE(p.propertyView(ref)->effectiveColor,0x336699u);
  QVERIFY(run(p,"territorial.color.reset",TerritorialColorEdit{{ref},{}}).changed());
  QCOMPARE(p.propertyView(ref)->effectiveColor,0xff0000u);
  QCOMPARE(p.propertyView(territorialRef("R"))->effectiveColor,0xff0000u);
  QVERIFY(p.undo());QCOMPARE(p.propertyView(ref)->effectiveColor,0x336699u);
  QVERIFY(p.redo());QCOMPARE(p.propertyView(ref)->effectiveColor,0xff0000u);
  QVERIFY(p.document().geometries.get(p.document().units[1].geometry)==g);
  auto decoded=projectcodec::decode(projectcodec::encode(p));
  QVERIFY(!decoded.presentation.objectStyles.at(ref).explicitColor);
 }
 void fieldsNormalizePerKindAndPreserveRelations() {
  Project p;p.replace(fixture());const auto rel=p.document().relations;const auto geometry=p.document().units[2].geometry;
  QVERIFY(run(p,"territorial.field",TerritorialFieldEdit{territorialRef("S"),TerritorialField::Name,"  "}).changed());
  QCOMPARE(p.propertyView(territorialRef("S"))->displayName,std::string("이름 없는 하위단위"));
  QVERIFY(run(p,"territorial.field",TerritorialFieldEdit{territorialRef("A"),TerritorialField::Name,""}).changed());
  QCOMPARE(p.propertyView(territorialRef("A"))->displayName,std::string("Alpha"));
  for(auto id:{"A","S","R"})QVERIFY(run(p,"territorial.field",TerritorialFieldEdit{territorialRef(id),TerritorialField::Notes,"  one\ntwo  "}).changed());
  QCOMPARE(p.document().units[0].notes,std::string("  one\ntwo  "));
  QCOMPARE(p.document().units[1].notes,std::string("one\ntwo"));
  QCOMPARE(projectcodec::decode(projectcodec::encode(p)).units[0].notes,std::string("one\ntwo"));
  QVERIFY(run(p,"territorial.field",TerritorialFieldEdit{territorialRef("R"),TerritorialField::ValidFrom,"-0001"}).changed());
  const auto before=projectcodec::encode(p);const auto revision=p.revision();
  QVERIFY(!run(p,"territorial.field",TerritorialFieldEdit{territorialRef("R"),TerritorialField::ValidTo,"0000"}).ok());
  QVERIFY(!run(p,"territorial.field",TerritorialFieldEdit{territorialRef("S"),TerritorialField::ValidTo,"2000"}).ok());
  QCOMPARE(p.revision(),revision);QCOMPARE(projectcodec::encode(p),before);
  QVERIFY(p.document().units[2].geometry==geometry);QCOMPARE(p.document().relations.size(),rel.size());
  QCOMPARE(trimWebText("\xef\xbb\xbf  text\xe3\x80\x80"),std::string("text"));
 }
 void batchColorPreservesWebCheckpointAndLockException() {
  Project p;p.replace(fixture());const std::vector<ObjectRef> refs={territorialRef("A"),territorialRef("S")};
  QVERIFY(run(p,"territorial.lock",TerritorialLockEdit{refs,true}).changed());
  QVERIFY(!run(p,"territorial.color",TerritorialColorEdit{{refs[0]},0x112233}).ok());
  QVERIFY(run(p,"territorial.batch-color",TerritorialColorEdit{refs,0x112233}).changed());
  p.markSaved();const auto bytes=projectcodec::encode(p);const auto rev=p.revision();
  QVERIFY(run(p,"territorial.batch-color",TerritorialColorEdit{refs,0x112233}).changed());
  QCOMPARE(projectcodec::encode(p),bytes);QCOMPARE(p.revision(),rev+1);QVERIFY(p.dirty());
  QVERIFY(p.undo());QVERIFY(!p.dirty());QVERIFY(p.canRedo());
  QVERIFY(run(p,"territorial.batch-color",TerritorialColorEdit{refs,0x112233}).changed());QVERIFY(!p.canRedo());
  QVERIFY(run(p,"territorial.lock",TerritorialLockEdit{refs,false}).changed());
  QCOMPARE(p.document().units[0].locked,false);QCOMPARE(p.document().units[1].locked,false);
  QCOMPARE(run(p,"territorial.lock",TerritorialLockEdit{refs,false}).status,CommandStatus::NoOp);
 }
 void batchExceptionsNeverBypassLayerOrPreservationProtection() {
  auto d=fixture();d.presentation.userLayers[0].locked=true;Project p;p.replace(d);
  const std::vector<ObjectRef> refs={territorialRef("A"),territorialRef("S")};
  auto bytes=projectcodec::encode(p);
  QCOMPARE(run(p,"territorial.batch-color",TerritorialColorEdit{refs,0x112233}).error,CommandError::Locked);
  QCOMPARE(projectcodec::encode(p),bytes);QVERIFY(!p.canUndo());
  d.presentation.userLayers[0].locked=false;
  PreservedExtension e;e.id="opaque";e.payload="9007199254740993";e.dependencies=refs;d.extensions.push_back(e);p.replace(d);
  QCOMPARE(run(p,"territorial.batch-color",TerritorialColorEdit{refs,0x112233}).error,CommandError::UnsupportedDependency);
  QVERIFY(!p.canUndo());
 }
 void staleAndCancelledPropertyPreviewsRemainAtomic() {
  Project p;p.replace(fixture());CommandArguments args;args.action=TerritorialColorEdit{{territorialRef("S")},0x111111};
  auto preview=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"territorial.color",args));QVERIFY(preview.preview);
  auto bytes=projectcodec::encode(p);CommandProcessor::cancel(*preview.preview);
  QCOMPARE(CommandProcessor::confirm(p,*preview.preview).error,CommandError::PreviewConsumed);QCOMPARE(projectcodec::encode(p),bytes);
  preview=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"territorial.color",args));
  QVERIFY(run(p,"territorial.lock",TerritorialLockEdit{{territorialRef("R")},true}).changed());
  QCOMPARE(CommandProcessor::confirm(p,*preview.preview).error,CommandError::StaleRevision);
 }
 void controllerDraftOwnershipIndependentFieldsAndSave() {
  QTemporaryDir dir;Project p;p.replace(fixture());const auto path=QUrl::fromLocalFile(dir.filePath("fixture.json"));
  QFile f(path.toLocalFile());QVERIFY(f.open(QIODevice::WriteOnly));f.write(projectcodec::encode(p));f.close();
  EditorController c;QVERIFY(c.openFile(path));
  QVERIFY(c.selectObject({{"domain","territorial"},{"type","subunit"},{"id","S"}}));
  auto token=c.beginPropertyEdit("notes");QVERIFY(!token.isEmpty());QVERIFY(c.updatePropertyEdit(token,"  parked S  "));
  const auto bytes=c.documentBytes();const auto revision=c.revision();
  c.selectCountry("A");QVERIFY(!c.confirmPropertyEdit(token));QCOMPARE(c.documentBytes(),bytes);QCOMPARE(c.revision(),revision);
  QVERIFY(c.beginColorEdit());QVERIFY(c.confirmColorEdit("#ff0000"));
  QVERIFY(c.hasPendingEdits());QVERIFY(c.saveFile(path));
  auto saved=projectcodec::decode(c.documentBytes());QCOMPARE(saved.units[1].notes,std::string("parked S"));
  QVERIFY(!saved.presentation.objectStyles.at(territorialRef("S")).explicitColor);
  QCOMPARE(effectiveObjectColor(saved,territorialRef("S")),0xff0000u);
  QVERIFY(c.selectObject({{"domain","territorial"},{"type","region"},{"id","R"}}));
  c.setMemoDraft("kept");c.setValidFromDraft("0000");QVERIFY(!c.commitObjectField("validFrom"));
  QCOMPARE(c.validFromDraft(),QString());QCOMPARE(c.memoDraft(),QString("kept"));
  c.setNameDraft(" R renamed ");QVERIFY(c.commitObjectField("name"));QCOMPARE(c.selectedName(),QString("R renamed"));
  QCOMPARE(c.memoDraft(),QString("kept"));QVERIFY(c.commitObjectField("notes"));
  auto color=c.documentBytes();QVERIFY(c.beginColorEdit());c.selectCountry("A");QVERIFY(!c.confirmColorEdit("#00ff00"));QCOMPARE(c.documentBytes(),color);
 }
 void v3MigrationDoesNotReplayArchiveOverRecentValues() {
  Project p;p.replace(fixture());auto obj=QJsonDocument::fromJson(projectcodec::encode(p)).object();obj["version"]=3;
  auto units=obj["units"].toArray();
  for(int i=0;i<units.size();++i){auto u=units[i].toObject();u.remove("baseName");u.remove("nameExplicit");if(i==0)u["name"]="Recent native name";units[i]=u;}
  obj["units"]=units;auto presentation=obj["presentation"].toObject();auto domains=presentation["objectStyles"].toObject();auto styles=domains["territorial"].toObject();
  for(auto id:styles.keys()){auto v=styles[id].toObject();if(v["color"].isNull())v["color"]="#123456";styles[id]=v;}
  domains["territorial"]=styles;presentation["objectStyles"]=domains;obj["presentation"]=presentation;
  auto d=projectcodec::decode(QJsonDocument(obj).toJson());QCOMPARE(d.units[0].name,std::string("Recent native name"));
  QVERIFY(d.presentation.objectStyles.at(territorialRef("S")).explicitColor);
 }
 void cleanWebImportPreservesSemanticsAndCanEdit() {
  QFile f(QStringLiteral(M32_FIXTURES)+"/clean-v5.input.json");QVERIFY(f.open(QIODevice::ReadOnly));const auto input=f.readAll();
  auto candidate=webimport::prepare(input);Project p;p.replace(candidate.document);
  const auto child=territorialRef("00000000-0000-4000-8000-000000000003"),region=territorialRef("00000000-0000-4000-8000-000000000004");
  QVERIFY(!p.document().presentation.objectStyles.at(child).explicitColor);
  auto color=run(p,"territorial.color",TerritorialColorEdit{{territorialRef("A")},0xff0000});
  QVERIFY2(color.ok(),color.detail.c_str());QCOMPARE(p.propertyView(child)->effectiveColor,0xff0000u);
  QVERIFY(run(p,"territorial.field",TerritorialFieldEdit{region,TerritorialField::ValidFrom,"1900"}).ok());
  QVERIFY(run(p,"territorial.lock",TerritorialLockEdit{{child},true}).ok());
  Project reopened;reopened.replace(projectcodec::decode(projectcodec::encode(p)));
  QCOMPARE(reopened.propertyView(child)->effectiveColor,0xff0000u);QVERIFY(reopened.document().units[1].locked);
  auto opaque=QJsonDocument::fromJson(input).object();opaque["future"]=QJsonObject{{"colorOwner","A"}};
  p.replace(webimport::prepare(QJsonDocument(opaque).toJson()).document);
  QCOMPARE(run(p,"territorial.color",TerritorialColorEdit{{territorialRef("A")},0x00ff00}).error,CommandError::UnsupportedDependency);
 }
 void importedUnitsAppearWithoutNativeReopen() {
  EditorController c;QVERIFY(c.prepareWebImport(QUrl::fromLocalFile(QStringLiteral(M32_FIXTURES)+"/clean-v5.input.json")));
  QTRY_VERIFY_WITH_TIMEOUT(c.hasWebImportPreview(),5000);QVERIFY(c.confirmWebImport(c.webImportHash(),"discard"));
  QCOMPARE(c.paths().size(),qsizetype(3));QVERIFY(c.selectObject({{"domain","territorial"},{"type","subunit"},{"id","00000000-0000-4000-8000-000000000003"}}));
  c.setNameDraft("Imported child");QVERIFY(c.commitObjectField("name"));QCOMPARE(c.selectedName(),QString("Imported child"));
 }
 void exposesTerritorialPropertyBridge() {
  EditorController editor;
  QVERIFY2(editor.metaObject()->indexOfProperty("objectProperties")>=0,"M3.2 object property read model must be exposed");
 }
};
QTEST_GUILESS_MAIN(PropertyTests)
#include "property_tests.moc"
