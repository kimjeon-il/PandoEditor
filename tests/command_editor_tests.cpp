#include "editorcontroller.h"
#include <QCoreApplication>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class CommandEditorTests : public QObject {
    Q_OBJECT
    static QString countryId(EditorController& c) { return c.countryRows().front().toMap()["id"].toString(); }
    static QString layerId(EditorController& c) { return c.layers().front().toMap()["id"].toString(); }
    static quint64 revision(EditorController& c) { return c.property("revision").toULongLong(); }
    static QByteArray bytes(const QString& path) { QFile f(path); if(!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
private slots:
    void compoundPropertiesHaveOneUndo() {
        EditorController c; c.selectCountry(countryId(c));
        const auto original=c.selectedName(), originalColor=c.colorDraft();
        const auto originalLayer=c.layerNameDraft();
        const auto originalOpacity=c.countryOpacity(), originalLayerOpacity=c.layerOpacity();
        c.setNameDraft("새 이름"); c.setMemoDraft("새 메모"); c.setColorDraft("#102030");
        c.previewCountryOpacity(0.4); c.setLayerNameDraft("새 레이어명"); c.previewLayerOpacity(0.7);
        QVERIFY(c.commitPendingEdits()); QCOMPARE(c.selectedName(),QString("새 이름"));
        c.undo(); QCOMPARE(c.selectedName(),original); QCOMPARE(c.colorDraft(),originalColor);
        QCOMPARE(c.layerNameDraft(),originalLayer); QCOMPARE(c.countryOpacity(),originalOpacity);
        QCOMPARE(c.layerOpacity(),originalLayerOpacity); QVERIFY(!c.canUndo()); QVERIFY(!c.dirty());
        QVERIFY(c.property("revision").isValid()); QCOMPARE(revision(c),quint64(2));
        c.redo(); QCOMPARE(c.selectedName(),QString("새 이름")); QCOMPARE(revision(c),quint64(3));
    }
    void invalidDraftStaysAndImmediateFailureDoesNotCommit() {
        EditorController c; c.selectCountry(countryId(c)); const auto original=c.selectedName();
        QSignalSpy errors(&c,&EditorController::errorOccurred);
        c.setNameDraft(" "); c.setColorDraft("invalid"); c.setMemoDraft("keep me");
        QVERIFY(!c.commitPendingEdits()); QCOMPARE(c.nameDraft(),QString(" "));
        QCOMPARE(c.colorDraft(),QString("invalid")); QCOMPARE(c.memoDraft(),QString("keep me"));
        QCOMPARE(c.selectedName(),original); QVERIFY(!c.canUndo()); QVERIFY(!errors.empty());
        c.setNameDraft("pending"); c.setColorDraft("#102030");
        c.moveCountry("missing-layer");
        QCOMPARE(c.selectedName(),original); QCOMPARE(c.nameDraft(),QString("pending"));
        QVERIFY(!c.canUndo()); QCOMPARE(revision(c),quint64(0));
    }
    void immediateColorAndDraftAreOneUndo() {
        EditorController c; c.selectCountry(countryId(c)); const auto original=c.selectedName();
        const auto color=c.colorDraft();
        c.setNameDraft("pending name"); c.setMemoDraft("pending memo"); c.setColor("#102030");
        QCOMPARE(c.selectedName(),QString("pending name")); c.undo();
        QCOMPARE(c.selectedName(),original); QCOMPARE(c.colorDraft(),color); QVERIFY(!c.canUndo());
        QCOMPARE(revision(c),quint64(2));
    }
    void undoRedoNeverAutoCommitDrafts() {
        EditorController c; c.selectCountry(countryId(c)); const auto original=c.selectedName();
        c.setColor("#102030"); c.undo(); QVERIFY(c.canRedo());
        c.setNameDraft("pending"); QSignalSpy errors(&c,&EditorController::errorOccurred);
        c.redo(); QVERIFY(c.canRedo()); QCOMPARE(c.nameDraft(),QString("pending"));
        QCOMPARE(c.selectedName(),original); QCOMPARE(revision(c),quint64(2));
        QVERIFY(!errors.empty()); QVERIFY(errors.last().front().toString().contains("PENDING_EDITS"));
        QVERIFY(QMetaObject::invokeMethod(&c,"discardPendingEdits"));
        c.redo(); QCOMPARE(c.colorDraft(),QString("#102030")); QCOMPARE(revision(c),quint64(3));
    }
    void previewIsPureAndCancelPreservesDrafts() {
        EditorController c; c.selectCountry(countryId(c)); const auto original=c.selectedName();
        c.setNameDraft("pending"); bool prepared=false;
        QVERIFY(QMetaObject::invokeMethod(&c,"preparePendingEdits",Q_RETURN_ARG(bool,prepared)));
        QVERIFY(prepared); QCOMPARE(c.selectedName(),original); QVERIFY(!c.canUndo());
        QCOMPARE(revision(c),quint64(0)); QVERIFY(c.dirty());
        QVERIFY(QMetaObject::invokeMethod(&c,"cancelPreview"));
        QCOMPARE(c.nameDraft(),QString("pending")); bool confirmed=true;
        QVERIFY(QMetaObject::invokeMethod(&c,"confirmPreview",Q_RETURN_ARG(bool,confirmed)));
        QVERIFY(!confirmed); QCOMPARE(c.selectedName(),original); QVERIFY(!c.canUndo());
        QVERIFY(c.commitPendingEdits()); QCOMPARE(revision(c),quint64(1));
    }
    void changedDraftInvalidatesPreparedPreview() {
        EditorController c; c.selectCountry(countryId(c)); const auto original=c.selectedName();
        c.setNameDraft("first"); bool ok=false;
        QVERIFY(QMetaObject::invokeMethod(&c,"preparePendingEdits",Q_RETURN_ARG(bool,ok))); QVERIFY(ok);
        c.setNameDraft("second");
        QVERIFY(QMetaObject::invokeMethod(&c,"confirmPreview",Q_RETURN_ARG(bool,ok))); QVERIFY(!ok);
        QCOMPARE(c.nameDraft(),QString("second")); QCOMPARE(c.selectedName(),original); QVERIFY(!c.canUndo());
    }
    void pcMobileSameDocumentRevisionAndHistory() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        EditorController pc(EditorControllerConfig{false,dir.filePath("pc-private.json")});
        EditorController mobile(EditorControllerConfig{true,dir.filePath("mobile-private.json")});
        for(auto* c:{&pc,&mobile}) {
            c->selectCountry(countryId(*c)); c->selectLayer(layerId(*c));
            c->setNameDraft("common"); c->setMemoDraft("same notes"); c->setColorDraft("#102030");
            c->previewCountryOpacity(0.35); c->setLayerNameDraft("common layer");
            c->previewLayerOpacity(0.65); QVERIFY(c->commitPendingEdits());
            QCOMPARE(revision(*c),quint64(1)); c->undo(); QCOMPARE(revision(*c),quint64(2));
            QVERIFY(!c->canUndo()); QVERIFY(!c->dirty()); c->redo(); QCOMPARE(revision(*c),quint64(3));
        }
        QCOMPARE(pc.countryRows(),mobile.countryRows()); QCOMPARE(pc.colors(),mobile.colors());
        QCOMPARE(pc.countryVisuals(),mobile.countryVisuals()); QCOMPARE(pc.layerVisuals(),mobile.layerVisuals());
        QVERIFY(pc.saveFile(QUrl::fromLocalFile(dir.filePath("pc.json")))); QVERIFY(mobile.savePrivate());
        auto a=projectcodec::decode(bytes(dir.filePath("pc.json")));
        auto b=projectcodec::decode(bytes(dir.filePath("mobile-private.json")));
        // Legacy sample imports may receive fresh document IDs; identities are not domain edits.
        b.documentId=a.documentId;
        pandoeditor::Project ap,bp; ap.replace(a); bp.replace(b);
        QCOMPARE(projectcodec::encode(ap),projectcodec::encode(bp));
        QCOMPARE(revision(pc),quint64(3)); QCOMPARE(revision(mobile),quint64(3));
        QVERIFY(!pc.dirty() && !mobile.dirty());
    }
};
QTEST_GUILESS_MAIN(CommandEditorTests)
#include "command_editor_tests.moc"
