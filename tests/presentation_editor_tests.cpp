#include "editorcontroller.h"
#include <QTemporaryDir>
#include <QFile>
#include <QtTest>
class PresentationEditorTests:public QObject {
    Q_OBJECT
private slots:
    void labelLayoutAndDistributionMatchWebRules() {
        using namespace pandoeditor;
        LabelLayoutCandidate ordinary{{"label","a"},"label:a","place",10,10,20,10,70,0,10,false,false};
        auto selected=ordinary;selected.ref={"label","b"};selected.key="label:b";selected.selected=true;
        QCOMPARE(layoutLabels({ordinary,selected},1,3),std::vector<ObjectRef>({{"label","b"}}));
        auto pinned=ordinary;pinned.ref={"label","c"};pinned.key="label:c";pinned.pinned=true;
        QCOMPARE(layoutLabels({ordinary,pinned},1,3),std::vector<ObjectRef>({{"label","c"}}));
        const auto city=automaticLabelSettings("city",LabelSettings{12.,0.,99.,Point{1,2},false,"foreign"});
        QCOMPARE(*city.priority,70.);QCOMPARE(*city.minZoom,1.25);QCOMPARE(city.collisionGroup,std::string("place"));QVERIFY(city.pinned);
        QCOMPARE(distributionFillAlpha(50,.5),.205);
    }
    void distributionModeUsesSelectedVisibleLayer() {
        using namespace pandoeditor;ProjectDocument d;d.distributionLayers={{"a","A","language"},{"b","B","language"},{"c","C","religion"}};
        d.distributionEntries={{"a1","a",territorialRef("T"),{},50},{"b1","b",territorialRef("T"),{},50},{"c1","c",territorialRef("T"),{},30}};
        d.presentation.webPresentation.hiddenItems["languages"].insert("b");
        auto dominant=visibleDistributionEntries(d);QCOMPARE(dominant,std::vector<ObjectRef>({{"distributionEntry","a1"},{"distributionEntry","c1"}}));
        d.presentation.webPresentation.distributionSettings.renderMode=DistributionRenderMode::Intensity;
        QVERIFY(visibleDistributionEntries(d,"b").empty());QCOMPARE(visibleDistributionEntries(d,"a"),std::vector<ObjectRef>({{"distributionEntry","a1"}}));
    }
    void localHydroManifestConfiguresAtomically() {
        QTemporaryDir dir;
        const QString manifest=QStringLiteral(WEB_HYDRO_FIXTURE)+"/v0.13.1/manifest.json";
        EditorController controller({false,dir.filePath("private.json")});const auto revision=controller.revision();QVERIFY(controller.configureHydroData(QUrl::fromLocalFile(manifest)));QCOMPARE(controller.revision(),revision+1);QVERIFY(controller.hydroDataStatus()["ready"].toBool());controller.undo();QVERIFY(!controller.hydroDataStatus()["ready"].toBool());
    }
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
