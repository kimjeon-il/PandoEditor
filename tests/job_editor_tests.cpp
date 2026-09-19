#include "editorcontroller.h"
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
namespace {
QString firstCountry(EditorController& c) { return c.countryRows().first().toMap()["id"].toString(); }
bool invoke(EditorController& c,const char* method,bool& ok) { return QMetaObject::invokeMethod(&c,method,Q_RETURN_ARG(bool,ok)); }
QByteArray bytes(const QString& path) { QFile f(path); if(!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
}
class JobEditorTests : public QObject {
    Q_OBJECT
private slots:
    void applyIsAsyncAndOneUndo() {
        EditorController c; c.selectCountry(firstCountry(c)); const auto original=c.selectedName();
        c.setNameDraft("async"); c.setMemoDraft("notes"); c.setColorDraft("#102030"); c.previewCountryOpacity(.35);
        bool ok=false; QVERIFY(invoke(c,"applyPendingEditsAsync",ok)); QVERIFY(ok);
        QCOMPARE(c.revision(),qulonglong(0)); QCOMPARE(c.selectedName(),original); QVERIFY(c.property("jobBusy").toBool());
        QTRY_COMPARE(c.revision(),qulonglong(1)); QVERIFY(!c.property("jobBusy").toBool()); QCOMPARE(c.selectedName(),QString("async"));
        c.undo(); QCOMPARE(c.selectedName(),original); QVERIFY(!c.canUndo() && c.canRedo() && !c.dirty());
        c.redo(); QCOMPARE(c.selectedName(),QString("async")); QCOMPARE(c.revision(),qulonglong(3));
    }
    void cancelKeepsDraftAndRedo() {
        EditorController c; c.selectCountry(firstCountry(c)); c.setColor("#102030"); c.undo(); const auto name=c.selectedName();
        c.setNameDraft("pending"); bool ok=false; QVERIFY(invoke(c,"applyPendingEditsAsync",ok)); QVERIFY(ok);
        QVERIFY(QMetaObject::invokeMethod(&c,"cancelBackgroundWork"));
        QTRY_VERIFY(!c.property("jobBusy").toBool()); QCoreApplication::processEvents();
        QCOMPARE(c.selectedName(),name); QCOMPARE(c.nameDraft(),QString("pending")); QCOMPARE(c.revision(),qulonglong(2)); QVERIFY(c.canRedo());
    }
    void newerDraftAndReopenDiscardOldResult() {
        QTemporaryDir dir; EditorController c; c.selectCountry(firstCountry(c)); const auto original=c.selectedName();
        const auto path=QUrl::fromLocalFile(dir.filePath("before.json")); QVERIFY(c.saveFile(path));
        c.setNameDraft("first"); bool ok=false; QVERIFY(invoke(c,"applyPendingEditsAsync",ok)); QVERIFY(ok);
        c.setNameDraft("second"); QCoreApplication::processEvents(); QCOMPARE(c.nameDraft(),QString("second")); QCOMPARE(c.selectedName(),original); QCOMPARE(c.revision(),qulonglong(0));
        QVERIFY(invoke(c,"applyPendingEditsAsync",ok)); QVERIFY(ok); QVERIFY(c.openFile(path));
        QCoreApplication::processEvents(); QCOMPARE(c.revision(),qulonglong(0)); QVERIFY(!c.canUndo() && !c.dirty());
        c.selectCountry(firstCountry(c)); QCOMPARE(c.selectedName(),original);
    }
    void asyncPreviewRequiresConfirm() {
        EditorController c; c.selectCountry(firstCountry(c)); auto original=c.selectedName(); c.setNameDraft("preview");
        bool ok=false; QVERIFY(invoke(c,"preparePendingEditsAsync",ok)); QVERIFY(ok);
        QTRY_VERIFY(c.hasPreparedPreview()); QCOMPARE(c.revision(),qulonglong(0)); QCOMPARE(c.selectedName(),original);
        QVERIFY(c.confirmPreview()); QCOMPARE(c.revision(),qulonglong(1)); QCOMPARE(c.selectedName(),QString("preview"));
        QVERIFY(!c.confirmPreview()); QCOMPARE(c.revision(),qulonglong(1));
    }
    void validationFailureAndNoOp() {
        EditorController c; c.selectCountry(firstCountry(c)); const auto name=c.selectedName();
        c.setColor("#102030"); c.undo(); QSignalSpy errors(&c,&EditorController::errorOccurred);
        bool ok=false; QVERIFY(invoke(c,"applyPendingEditsAsync",ok)); QVERIFY(ok); QVERIFY(!c.property("jobBusy").toBool()); QVERIFY(c.canRedo());
        c.setNameDraft("  "); QVERIFY(invoke(c,"applyPendingEditsAsync",ok)); QVERIFY(ok);
        QTRY_VERIFY(!c.property("jobBusy").toBool()); QVERIFY(!errors.isEmpty()); QCOMPARE(c.nameDraft(),QString("  ")); QCOMPARE(c.selectedName(),name);
        QCOMPARE(c.revision(),qulonglong(2)); QVERIFY(c.canRedo());
    }
    void pcMobileSameAsyncSavedResult() {
        QTemporaryDir dir; EditorController pc(EditorControllerConfig{false,dir.filePath("pc-private.json")});
        EditorController mobile(EditorControllerConfig{true,dir.filePath("mobile.json")});
        for(auto* c:{&pc,&mobile}) {
            c->selectCountry(firstCountry(*c)); c->setNameDraft("same"); c->setMemoDraft("same notes"); c->previewCountryOpacity(.4);
            bool ok=false; QVERIFY(invoke(*c,"applyPendingEditsAsync",ok)); QVERIFY(ok); QTRY_COMPARE(c->revision(),qulonglong(1));
            c->undo(); QCOMPARE(c->revision(),qulonglong(2)); QVERIFY(!c->canUndo()); c->redo(); QCOMPARE(c->revision(),qulonglong(3));
        }
        QVERIFY(pc.saveFile(QUrl::fromLocalFile(dir.filePath("pc.json")))); QVERIFY(mobile.savePrivate());
        QCOMPARE(bytes(dir.filePath("pc.json")),bytes(dir.filePath("mobile.json")));
    }
    void newerDraftDuringPreviewNotificationWins() {
        EditorController c; c.selectCountry(firstCountry(c)); const auto name=c.selectedName();
        c.setNameDraft("old result");
        connect(&c,&EditorController::previewChanged,&c,[&](){ if(c.hasPreparedPreview()) c.setNameDraft("new input"); });
        bool ok=false; QVERIFY(invoke(c,"applyPendingEditsAsync",ok)); QVERIFY(ok);
        QTRY_VERIFY(!c.property("jobBusy").toBool());
        QCOMPARE(c.nameDraft(),QString("new input")); QCOMPARE(c.selectedName(),name); QCOMPARE(c.revision(),qulonglong(0));
    }
    void webNameAndNotesCommitIndependently() {
        EditorController c; c.selectCountry(firstCountry(c)); const auto original=c.selectedName();
        c.setNameDraft("  web name  "); c.setMemoDraft("uncommitted notes"); bool ok=false;
        QVERIFY(QMetaObject::invokeMethod(&c,"commitCountryField",Q_RETURN_ARG(bool,ok),Q_ARG(QString,QString("name")))); QVERIFY(ok);
        QCOMPARE(c.selectedName(),QString("web name")); QCOMPARE(c.nameDraft(),QString("web name")); QCOMPARE(c.memoDraft(),QString("uncommitted notes")); QCOMPARE(c.revision(),qulonglong(1));
        QVERIFY(QMetaObject::invokeMethod(&c,"commitCountryField",Q_RETURN_ARG(bool,ok),Q_ARG(QString,QString("notes")))); QVERIFY(ok);
        QCOMPARE(c.revision(),qulonglong(2)); c.undo(); QCOMPARE(c.selectedName(),QString("web name")); QCOMPARE(c.memoDraft(),QString());
        c.undo(); QCOMPARE(c.selectedName(),original); QVERIFY(!c.canUndo());
    }
};
QTEST_GUILESS_MAIN(JobEditorTests)
#include "job_editor_tests.moc"
