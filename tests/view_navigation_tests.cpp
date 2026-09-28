#include "editorcontroller.h"
#include "../renderer/maprenderitem.h"
#include "../renderer/geographicimagemesh.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class ViewNavigationTests final : public QObject {
    Q_OBJECT
private slots:
    void firstRunIsGlobeAndProjectionCamerasRemainIndependent() {
        EditorController editor;QCOMPARE(editor.projectionMode(),QStringLiteral("globe"));
        QVERIFY(editor.publishMapView({{"viewportWidth",800.},{"viewportHeight",600.},{"scale",240.},
            {"translateX",400.},{"translateY",300.},{"centerLongitude",35.},{"centerLatitude",12.}}));
        const auto globe=editor.mapViewState();QCOMPARE(globe.value("centerLongitude").toDouble(),35.);
        QVERIFY(editor.setProjectionMode("flat"));QCOMPARE(editor.projectionMode(),QStringLiteral("flat"));
        QVERIFY(editor.publishMapView({{"viewportWidth",900.},{"viewportHeight",500.},{"scale",180.},
            {"translateX",450.},{"translateY",250.},{"centerLongitude",-20.},{"centerLatitude",4.}}));
        const auto flat=editor.mapViewState();QCOMPARE(flat.value("centerLongitude").toDouble(),-20.);
        QVERIFY(editor.setProjectionMode("globe"));
        QCOMPARE(editor.mapViewState().value("centerLongitude").toDouble(),35.);
        QCOMPARE(editor.mapViewState().value("scale").toDouble(),240.);
        QVERIFY(editor.setProjectionMode("flat"));
        QCOMPARE(editor.mapViewState().value("centerLongitude").toDouble(),-20.);
        QCOMPARE(editor.mapViewState().value("viewportWidth").toDouble(),900.);
    }
    void screenPickingRejectsOutsideGlobeAndPublishesViewport() {
        EditorController editor;
        QVERIFY(editor.publishMapView({{"viewportWidth",800.},{"viewportHeight",600.},{"scale",240.},
            {"translateX",400.},{"translateY",300.},{"centerLongitude",0.},{"centerLatitude",0.}}));
        QCOMPARE(editor.mapViewState().value("viewportHeight").toDouble(),600.);
        QVERIFY(editor.pickObjectScreen(799,599,1).isEmpty());
        editor.beginMapSelectionScreen(799,599,false,1);
        QVERIFY(editor.selectionItems().isEmpty());
    }
    void cpuFallbackConsumesThePublishedSceneAndView() {
        EditorController editor;MapRenderItem renderer;
        renderer.setSceneBridge(editor.mapSceneBridge());
        QVERIFY(renderer.sceneRevision()>0);
        QCOMPARE(renderer.viewRevision(),editor.mapViewState().value("revision").toULongLong());
        QVERIFY(editor.setProjectionMode("flat"));
        QCOMPARE(renderer.viewRevision(),editor.mapViewState().value("revision").toULongLong());
    }
    void globeLabelsConsumeThePublishedView() {
        EditorController editor;
        QVERIFY(editor.publishMapView({{"viewportWidth",800.},{"viewportHeight",600.},{"scale",240.},
            {"translateX",400.},{"translateY",300.},{"centerLongitude",10.},{"centerLatitude",20.}}));
        const auto labels=editor.labelLayout(10000,-5000,-5000,1,800,600);
        QVERIFY(!labels.isEmpty());
        for(const auto& value:labels) {
            const auto row=value.toMap();
            QVERIFY(std::hypot(row.value("x").toDouble()-400,row.value("y").toDouble()-300)<=241);
        }
    }
    void geographicImageMeshUsesOneProjectionSnapshot() {
        MapViewState flat;flat.viewportWidth=800;flat.viewportHeight=600;flat.scale=200;
        flat.translateX=400;flat.translateY=300;
        const auto flatMesh=buildGeographicImageMesh({-180,-90,180,90},flat,8,4);
        QCOMPARE(flatMesh.indices.size(),std::size_t(8*4*6));
        auto globe=flat;globe.mode=ProjectionMode::Globe;
        const auto globeMesh=buildGeographicImageMesh({-180,-90,180,90},globe,32,16);
        QVERIFY(!globeMesh.indices.empty());
        QVERIFY(globeMesh.indices.size()<std::size_t(32*16*6));
    }
    void appliedAppearancePersistsButPreviewDoesNotLeak() {
        QTemporaryDir directory;QVERIFY(directory.isValid());
        EditorControllerConfig config;config.appearancePath=directory.filePath("appearance.json");
        {
            EditorController editor(config);editor.beginAppearancePreview();
            QVERIFY(editor.previewAppearance({{"theme","dark"},{"accentPreset","pink"},
                {"statusBarVisible",false},{"smoothLines",false}}));
            QVERIFY(editor.applyAppearancePreview());
        }
        EditorController restored(config);
        const auto applied=restored.appearancePreferences();
        QCOMPARE(applied.value("theme").toString(),QString("dark"));
        QCOMPARE(applied.value("accentPreset").toString(),QString("pink"));
        QCOMPARE(applied.value("statusBarVisible").toBool(),false);
        QCOMPARE(applied.value("smoothLines").toBool(),false);
        restored.beginAppearancePreview();QVERIFY(restored.previewAppearance({{"theme","light"}}));
        restored.cancelAppearancePreview();
        QCOMPARE(restored.appearancePreferences().value("theme").toString(),QString("dark"));
    }
};

QTEST_MAIN(ViewNavigationTests)
#include "view_navigation_tests.moc"
