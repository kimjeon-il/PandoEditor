#include "editorcontroller.h"
#include <QTemporaryDir>
#include <QFile>
#include <QtTest>
class PresentationEditorTests:public QObject {
    Q_OBJECT
private slots:
    void visibilityKeepsSelectionDraftAndRedo() {
        QTemporaryDir dir;EditorController c({false,dir.filePath("private.json")});
        c.selectCountry("DEU");c.setColor("#112233");c.undo();QVERIFY(c.canRedo());
        c.setNameDraft("pending");const auto rev=c.revision(),selection=c.selectionRevision();
        QVERIFY(c.setPresentationVisibility("countries",false));
        QCOMPARE(c.revision(),rev);QCOMPARE(c.selectionRevision(),selection);QVERIFY(c.canRedo());QCOMPARE(c.nameDraft(),QString("pending"));
        QVERIFY(!c.countryVisuals()["DEU"].toMap()["visible"].toBool());
        c.setSearchQuery("DEU");QVERIFY(!c.searchResults().empty());QVERIFY(c.focusObject());
        QVERIFY(c.flushPresentationRecovery());QVERIFY(c.dirty());QVERIFY(!QFile::exists(dir.filePath("private.json")));
        c.discardPendingEdits();c.redo();QVERIFY(!c.countryVisuals()["DEU"].toMap()["visible"].toBool());
    }
    void recoveryReopensPresentationWithoutDraftCommit() {
        QTemporaryDir dir;const auto path=dir.filePath("private.json");
        {
            EditorController c({false,path});c.selectCountry("DEU");c.setNameDraft("not committed");
            QVERIFY(c.setPresentationOpacity("countries",.6));QVERIFY(c.flushPresentationRecovery());
        }
        EditorController c({false,path});QVERIFY(c.presentationRecoveryAvailable());QVERIFY(c.restorePresentationRecovery());
        QCOMPARE(c.presentationGroups()[0].toMap()["opacity"].toDouble(),.6);c.selectCountry("DEU");QVERIFY(c.nameDraft()!="not committed");QVERIFY(c.dirty());
        QVERIFY(c.discardPresentationRecovery());QVERIFY(!c.presentationRecoveryAvailable());
    }
    void pendingRecoveryDoesNotOverwriteAnotherProject() {
        QTemporaryDir dir;EditorController c({false,dir.filePath("private.json")});
        QVERIFY(c.setPresentationVisibility("countries",false));
        QVERIFY(c.flushPresentationRecovery());
        const auto path=dir.filePath("new.json");auto replacement=c.documentBytes();
        replacement.replace(c.documentId().toUtf8(),"separate-recovery-document");
        QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));f.write(replacement);f.close();
        QVERIFY(c.openFile(QUrl::fromLocalFile(path)));QVERIFY(!c.flushPresentationRecovery());
        // Replacing a project cancels the delayed write, but it must not erase
        // another document's recovery snapshot just because this session moved.
        QVERIFY(c.presentationRecoveryAvailable());QVERIFY(c.discardPresentationRecovery());
    }
};
QTEST_MAIN(PresentationEditorTests)
#include "presentation_editor_tests.moc"
