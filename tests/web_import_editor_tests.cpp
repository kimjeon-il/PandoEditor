#include "editorcontroller.h"
#include "webimport.h"
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
namespace {
QByteArray bytes(const QString& path) {QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
void write(const QString& path,const QByteArray& data) {QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size())throw std::runtime_error("fixture write");}
QByteArray source() {return bytes(QStringLiteral(WEB_IMPORT_FIXTURES)+"/../timeline-exchange/static.json");}
QString first(EditorController& c) {return c.countryRows().first().toMap()["id"].toString();}
}
class WebImportEditorTests:public QObject {
 Q_OBJECT
private slots:
 void prepareCancelKeepsDraftHistoryAndSelection() {
    QTemporaryDir d;auto path=d.filePath("source.json");write(path,source());
    EditorController c;c.selectCountry(first(c));c.setColor("#102030");c.undo();c.setNameDraft("pending");
    auto rev=c.revision();auto selected=c.selectedId();auto name=c.selectedName();
    QVERIFY(c.prepareWebImport(QUrl::fromLocalFile(path)));QVERIFY(c.webImportBusy());
    QCOMPARE(c.revision(),rev);QCOMPARE(c.nameDraft(),QString("pending"));
    c.cancelWebImport();QVERIFY(!c.webImportBusy());QVERIFY(!c.hasWebImportPreview());
    QTest::qWait(80);QCOMPARE(c.revision(),rev);QCOMPARE(c.selectedId(),selected);QCOMPARE(c.selectedName(),name);QVERIFY(c.canRedo());
    QCOMPARE(c.nameDraft(),QString("pending"));QCOMPARE(bytes(path),source());
 }
 void reviewAndConfirmOneShot() {
    QTemporaryDir d;auto path=d.filePath("source.json");write(path,source());EditorController c;
    auto session=c.projectInstanceId();QVERIFY(c.prepareWebImport(QUrl::fromLocalFile(path)));
    QTRY_VERIFY(c.hasWebImportPreview());QVERIFY(!c.webImportReport().isEmpty());
    auto hash=c.webImportHash();QVERIFY(!c.confirmWebImport("wrong","discard"));QVERIFY(c.hasWebImportPreview());
    QVERIFY(c.confirmWebImport(hash,"discard"));QVERIFY(!c.hasWebImportPreview());
    QCOMPARE(c.countryRows().size(),qsizetype(2));QCOMPARE(c.revision(),qulonglong(0));
    QVERIFY(c.projectInstanceId()!=session);QVERIFY(!c.canUndo()&&!c.canRedo());QVERIFY(c.dirty());
    QVERIFY(!c.confirmWebImport(hash,"discard"));QCOMPARE(bytes(path),source());
 }
 void staleAfterDraftEditUndoAndSameFileReopen() {
    QTemporaryDir d;auto path=d.filePath("source.json");write(path,source());EditorController c;c.selectCountry(first(c));
    auto saved=QUrl::fromLocalFile(d.filePath("current.json"));QVERIFY(c.saveFile(saved));
    for(int scenario=0;scenario<3;++scenario) {
       QVERIFY(c.prepareWebImport(QUrl::fromLocalFile(path)));QTRY_VERIFY(c.hasWebImportPreview());auto hash=c.webImportHash();
       if(scenario==0)c.setNameDraft("edited after report");
       if(scenario==1){c.setColor("#203040");c.undo();}
       if(scenario==2)QVERIFY(c.openFile(saved));
       QVERIFY(!c.confirmWebImport(hash,"discard"));QCOMPARE(c.countryRows().size(),qsizetype(5));c.discardPendingEdits();
    }
 }
 void saveFailurePreservesAllAndSaveSuccessStagesDraft() {
    QTemporaryDir d;auto path=d.filePath("source.json");write(path,source());EditorController c;c.selectCountry(first(c));
    auto oldId=c.selectedId();c.setColor("#103050");c.undo();c.setNameDraft("save pending draft");auto rev=c.revision();auto session=c.projectInstanceId();
    QVERIFY(c.prepareWebImport(QUrl::fromLocalFile(path)));QTRY_VERIFY(c.hasWebImportPreview());auto hash=c.webImportHash();
    QVERIFY(!c.confirmWebImport(hash,"save",QUrl::fromLocalFile(d.path())));
    QCOMPARE(c.nameDraft(),QString("save pending draft"));QCOMPARE(c.revision(),rev);QCOMPARE(c.projectInstanceId(),session);QVERIFY(c.canRedo());QVERIFY(c.hasWebImportPreview());
    auto save=d.filePath("old-saved.json");QVERIFY(c.confirmWebImport(hash,"save",QUrl::fromLocalFile(save)));
    pandoeditor::Project old;old.replace(projectcodec::decode(bytes(save)));QCOMPARE(old.country(oldId.toStdString())->name,std::string("save pending draft"));
    QCOMPARE(c.countryRows().size(),qsizetype(2));QCOMPARE(bytes(path),source());
 }
 void malformedDeltaAndSourceOverwriteAreRejected() {
    QTemporaryDir d;auto path=d.filePath("source.json");write(path,source());EditorController c;c.selectCountry(first(c));c.setNameDraft("keep");
    auto delta=d.filePath("delta.json");write(delta,R"({"format":"pandolab-autosave-delta","schemaVersion":9})");
    QVERIFY(c.prepareWebImport(QUrl::fromLocalFile(delta)));QTRY_VERIFY(!c.webImportBusy());QVERIFY(!c.hasWebImportPreview());QVERIFY(c.webImportError().contains("BASE_DATA_REQUIRED"));
    QCOMPARE(c.nameDraft(),QString("keep"));QVERIFY(c.prepareWebImport(QUrl::fromLocalFile(path)));QTRY_VERIFY(c.hasWebImportPreview());
    QVERIFY(!c.confirmWebImport(c.webImportHash(),"save",QUrl::fromLocalFile(path)));QCOMPARE(bytes(path),source());
    QVERIFY(c.confirmWebImport(c.webImportHash(),"discard"));c.selectCountry("A");c.setNameDraft("safe name");
    QVERIFY(!c.saveFile(QUrl::fromLocalFile(path)));QCOMPARE(c.nameDraft(),QString("safe name"));QCOMPARE(c.revision(),qulonglong(0));QCOMPARE(bytes(path),source());
 }
 void failedImportKeepsExistingCommandPreviewAndLatestRequestWins() {
    QTemporaryDir d;auto path=d.filePath("source.json");write(path,source());EditorController c;c.selectCountry(first(c));
    c.setNameDraft("pending preview");QVERIFY(c.preparePendingEdits());QVERIFY(c.hasPreparedPreview());
    QVERIFY(c.prepareWebImport(QUrl::fromLocalFile(d.filePath("missing.json"))));QTRY_VERIFY(!c.webImportBusy());
    QVERIFY(c.hasPreparedPreview());QVERIFY(!c.hasWebImportPreview());QCOMPARE(c.nameDraft(),QString("pending preview"));
    QVERIFY(c.confirmPreview());QCOMPARE(c.selectedName(),QString("pending preview"));
    auto changed=source();changed.replace(QStringLiteral("A 영토").toUtf8(),"Latest");
    auto second=d.filePath("second.json");write(second,changed);
    QVERIFY(c.prepareWebImport(QUrl::fromLocalFile(path)));QVERIFY(c.prepareWebImport(QUrl::fromLocalFile(second)));
    QTRY_VERIFY(c.hasWebImportPreview());auto hash=c.webImportHash();
    QCOMPARE(hash,webimport::prepare(changed).candidateHash);
    c.cancelWebImport();QTest::qWait(80);QVERIFY(!c.hasWebImportPreview());QVERIFY(!c.webImportBusy());
 }
 void invalidOutgoingDraftAndMobileSaveFailureKeepCandidate() {
    QTemporaryDir d;auto path=d.filePath("source.json");write(path,source());
    EditorController c(EditorControllerConfig{true,d.path()});c.selectCountry(first(c));c.setNameDraft("");
    QVERIFY(c.prepareWebImport(QUrl::fromLocalFile(path)));QTRY_VERIFY(c.hasWebImportPreview());auto hash=c.webImportHash();
    auto before=c.projectInstanceId();QVERIFY(!c.confirmWebImport(hash,"save"));
    QCOMPARE(c.revision(),qulonglong(0));QCOMPARE(c.projectInstanceId(),before);QCOMPARE(c.nameDraft(),QString(""));QVERIFY(c.hasWebImportPreview());
    c.cancelWebImport();c.setNameDraft("pending");
    QVERIFY(c.prepareWebImport(QUrl::fromLocalFile(path)));QTRY_VERIFY(c.hasWebImportPreview());
    QVERIFY(!c.confirmWebImport(c.webImportHash(),"save"));QVERIFY(c.hasWebImportPreview());QCOMPARE(c.nameDraft(),QString("pending"));
    QVERIFY(c.webImportError().contains("SAVE_FAILED"));QCOMPARE(bytes(path),source());
 }
 void identicalPcMobileAndRetainedRoundtrip() {
    QTemporaryDir d;auto path=d.filePath("source.json");write(path,source());
    EditorController pc(EditorControllerConfig{false,d.filePath("pc-private.json")}),mobile(EditorControllerConfig{true,d.filePath("mobile-private.json")});
    for(auto c:{&pc,&mobile}) {
       QVERIFY(c->prepareWebImport(QUrl::fromLocalFile(path)));QTRY_VERIFY(c->hasWebImportPreview());
       QVERIFY(c->confirmWebImport(c->webImportHash(),"discard"));c->selectCountry("A");c->setNameDraft("changed");QVERIFY(c->commitCountryField("name"));
       QCOMPARE(c->revision(),qulonglong(1));c->undo();QCOMPARE(c->revision(),qulonglong(2));c->redo();QCOMPARE(c->revision(),qulonglong(3));
    }
    QVERIFY(pc.saveFile(QUrl::fromLocalFile(d.filePath("pc.json"))));QVERIFY(mobile.savePrivate());
    QCOMPARE(bytes(d.filePath("pc.json")),bytes(d.filePath("mobile-private.json")));
    QVERIFY(pc.openFile(QUrl::fromLocalFile(d.filePath("pc.json"))));pc.selectCountry("A");QCOMPARE(pc.selectedName(),QString("changed"));
 }
};
QTEST_GUILESS_MAIN(WebImportEditorTests)
#include "web_import_editor_tests.moc"
