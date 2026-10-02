#include "editorcontroller.h"
#include <QTemporaryDir>
#include <QFile>
#include <QtTest>
#include <algorithm>
#include <tuple>
class PresentationEditorTests:public QObject {
    Q_OBJECT
private slots:
    void countryFlagRemainsWhenNameChannelIsHidden() {
        QTemporaryDir dir;
        EditorController editor({false,dir.filePath("private.json")});
        editor.selectCountry("DEU");
        QVERIFY(editor.countryVisuals().value("DEU").toMap().value("flagAvailable").toBool());
        QVERIFY(editor.setPresentationVisibility("basemapLabels",false));
        const auto hasDeu=[&] {
            const auto rows=editor.placedLabels();
            return std::any_of(rows.begin(),rows.end(),[](const QVariant& row){
                return row.toMap().value("ref").toMap().value("id").toString()=="DEU";
            });
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasDeu(),1000);
        const auto placed=editor.placedLabels();
        const auto it=std::find_if(placed.begin(),placed.end(),[](const QVariant& row){
            return row.toMap().value("ref").toMap().value("id").toString()=="DEU";
        });
        QVERIFY(it!=placed.end());
        const auto flag=it->toMap();
        QVERIFY(flag.value("flagVisible").toBool());
        QVERIFY(!flag.value("flagSource").toString().isEmpty());
        QVERIFY(!flag.value("nameVisible").toBool());
    }
    void labelLayoutRespectsWebBottomSafeArea() {
        using namespace pandoeditor;
        for(const bool mobile:{false,true}) {
            QTemporaryDir dir;
            ProjectDocument document({{"A","Alpha",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x112233}},
                                     {{"countries","Countries"}});
            document.documentId=mobile?"safe-area-mobile":"safe-area-desktop";
            Project project;project.replace(document);
            const auto path=dir.filePath("safe-area.json");QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));QVERIFY(file.write(projectcodec::encode(project))>0);file.close();
            EditorController editor({mobile,dir.filePath("private.json")});
            QVERIFY(editor.openFile(QUrl::fromLocalFile(path)));
            QVERIFY(editor.resizeMapCamera(600,400));
            QVERIFY(editor.setProjectionMode("flat"));
            const auto paths=editor.paths();QVERIFY(!paths.isEmpty());
            const auto geometry=paths.front().toMap();
            const double mapY=geometry.value("top").toDouble()+geometry.value("height").toDouble()/2;
            auto placedAt=[&](double screenY) {
                if(!editor.fitMapCamera())return false;
                QTest::qWait(ViewportResourceScheduler::SettleDelayMs*2);
                const auto state=editor.mapViewState();
                const double current=state.value("originY").toDouble()+
                    mapY*state.value("mapScale").toDouble();
                editor.beginMapInteraction();editor.beginMapCameraPan();
                if(!editor.updateMapCameraPan(0,screenY-current)) {
                    editor.endMapCameraPan();editor.endMapInteraction();return false;
                }
                editor.endMapCameraPan();editor.endMapInteraction();
                QTest::qWait(ViewportResourceScheduler::SettleDelayMs*2);
                const auto rows=editor.placedLabels();
                return std::any_of(rows.begin(),rows.end(),[](const QVariant& row){
                    return row.toMap().value("ref").toMap().value("id").toString()=="A";
                });
            };
            QVERIFY(placedAt(285)); // Within the 96 px mobile inset and the 32 px desktop inset.
            QCOMPARE(placedAt(355),!mobile); // Only the desktop safe area includes this box.
            QVERIFY(!placedAt(390)); // The bottom UI occupies both layouts.
            editor.selectCountry("A");QVERIFY(placedAt(390));
            editor.clearSelection();
            QVERIFY(editor.setLabelPinned({{"domain","territorial"},{"id","A"}},true));
            QVERIFY(placedAt(390)); // Pinned and selected labels bypass web bounds.
        }
    }
    void visibleContentDomainsHoverAndHiddenHoverExpires() {
        using namespace pandoeditor;
        QTemporaryDir dir;
        ProjectDocument document({{"A","Alpha",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x112233}},
                                 {{"countries","Countries"}});
        document.documentId="content-hover-domains";
        Geometry point;point.type="Point";point.points={{30,5}};
        document.geometries.insert({"place-point",1},point);
        PlaceLabel place;place.id="L";place.name="Place";place.kind="custom";place.geometry={"place-point",1};document.labels.push_back(place);
        auto addPolygon=[&](const std::string& id,double x) {
            Geometry shape;shape.type="Polygon";
            shape.polygons={{{{x,0},{x+10,0},{x+10,10},{x,10},{x,0}}}};
            document.geometries.insert({id,1},std::move(shape));
        };
        addPolygon("distribution",40);addPolygon("generic",60);
        DistributionLayer layer;layer.id="D";layer.name="Distribution";layer.type="language";
        document.distributionLayers.push_back(layer);
        DistributionEntry entry;entry.id="E";entry.layerId="D";entry.geometry=GeometryRef{"distribution",1};
        document.distributionEntries.push_back(entry);
        GenericFeature generic;generic.id="G";generic.name="Generic";generic.geometry={"generic",1};
        document.genericFeatures.push_back(generic);
        Project project;project.replace(document);
        const auto path=dir.filePath("content.json");QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));QVERIFY(file.write(projectcodec::encode(project))>0);file.close();
        EditorController editor({false,dir.filePath("private.json")});
        QVERIFY(editor.openFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(!editor.placedLabels().isEmpty(),1000);
        auto picked=[&](const QString& key) {
            for(const auto& value:editor.paths()){
                const auto row=value.toMap();if(row.value("countryId").toString()!=key)continue;
                return editor.pickObject(row.value("left").toDouble()+row.value("width").toDouble()/2,
                                         row.value("top").toDouble()+row.value("height").toDouble()/2,100,2);
            }
            return QVariantMap{};
        };
        for(const auto& [key,domain,id,group]:std::vector<std::tuple<QString,QString,QString,QString>>{
            {"content/label/L","label","L","labels"},
            {"content/distributionEntry/E","distributionLayer","D","languages"},
            {"content/generic/G","generic","G","genericFeatures"}}){
            const auto hit=picked(key);
            QCOMPARE(hit.value("domain").toString(),domain);QCOMPARE(hit.value("id").toString(),id);
            QVERIFY(editor.setHoverObject(hit,"map"));
            QCOMPARE(editor.hoverObject().value("id").toString(),id);
            QVERIFY(editor.setPresentationVisibility(group,false));
            QVERIFY2(editor.hoverObject().isEmpty(),qPrintable(group+" hover must expire when hidden"));
            QVERIFY(picked(key).isEmpty());
            QVERIFY(editor.setPresentationVisibility(group,true));
        }
        QVERIFY(editor.setHoverObject({{"domain","label"},{"id","L"}},"map"));
        QVERIFY(editor.setHoverObject({{"domain","generic"},{"id","G"}},"search"));
        QVERIFY(!editor.setHoverObject({},"map","label:L"));
        QCOMPARE(editor.hoverObject().value("id").toString(),QStringLiteral("G"));
    }
    void labelLayoutAndDistributionMatchWebRules() {
        using namespace pandoeditor;
        LabelLayoutCandidate ordinary{{"label","a"},"label:a","place",10,10,20,10,70,0,10,false,false};
        auto selected=ordinary;selected.ref={"label","b"};selected.key="label:b";selected.selected=true;
        QCOMPARE(layoutLabels({ordinary,selected},1,3),std::vector<ObjectRef>({{"label","b"}}));
        auto pinned=ordinary;pinned.ref={"label","c"};pinned.key="label:c";pinned.pinned=true;
        QCOMPARE(layoutLabels({ordinary,pinned},1,3),std::vector<ObjectRef>({{"label","c"}}));
        ordinary.x=5;ordinary.width=20;
        QVERIFY(layoutLabels({ordinary},1,3,LabelLayoutBounds{0,0,100,100}).empty());
        ordinary.selected=true;
        QCOMPARE(layoutLabels({ordinary},1,3,LabelLayoutBounds{0,0,100,100}),
                 std::vector<ObjectRef>({{"label","a"}}));
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
        EditorController controller({false,dir.filePath("private.json")});const auto revision=controller.revision();QVERIFY(controller.configureHydroData(QUrl::fromLocalFile(manifest)));QCOMPARE(controller.revision(),revision+1);QVERIFY(controller.hydroDataStatus()["ready"].toBool());
        auto* runtime=qobject_cast<HydroRuntimeProvider*>(controller.hydroSource());
        QVERIFY(runtime);
        runtime->requestViewport({7.5,800,500,1500,20,1});
        QTRY_VERIFY_WITH_TIMEOUT(controller.hydroViewportLoaded(),5000);
        QCOMPARE(controller.revision(),revision+1);
        controller.undo();QVERIFY(!controller.hydroDataStatus()["ready"].toBool());
        QVERIFY(!controller.hydroViewportLoaded());
        controller.redo();QVERIFY(controller.hydroDataStatus()["ready"].toBool());
    }
    void builtinHydroSearchPickAndFocusUseRuntimeMetadata(){
        QTemporaryDir dir;EditorController controller({false,dir.filePath("private.json")});
        const auto manifest=QStringLiteral(WEB_HYDRO_FIXTURE)+"/v0.13.1/manifest.json";
        QVERIFY(controller.configureHydroData(QUrl::fromLocalFile(manifest)));
        auto* runtime=qobject_cast<HydroRuntimeProvider*>(controller.hydroSource());
        QVERIFY(runtime);
        runtime->requestViewport({7.5,800,500,1500,20,1});
        QTRY_VERIFY_WITH_TIMEOUT(controller.hydroViewportLoaded(),5000);
        controller.setSearchQuery("Hole");
        const auto results=controller.searchResults();
        QVERIFY(std::any_of(results.begin(),results.end(),[](const QVariant& row){
            return row.toMap().value("id").toString()=="fixture:4";
        }));
        const auto projection=controller.hydroProjection();
        const auto px=[&](double lon){return lon*projection.value("cosLatitude").toDouble()-projection.value("minX").toDouble();};
        const auto py=[&](double lat){return projection.value("maxLatitude").toDouble()-lat;};
        const auto hit=controller.pickObject(px(30.5),py(0.5),100,7.5);
        QCOMPARE(hit.value("id").toString(),QStringLiteral("fixture:4"));
        QVERIFY(controller.selectObject(hit));
        QCOMPARE(controller.primaryObject().value("domain").toString(),QStringLiteral("hydroBuiltin"));
        QSignalSpy focus(&controller,&EditorController::focusRequested);
        QVERIFY(controller.focusObject());QCOMPARE(focus.count(),1);
        const auto revision=controller.revision();
        QVERIFY(controller.copyBuiltinHydro());
        QTRY_COMPARE_WITH_TIMEOUT(controller.hiddenHydroIds().size(),1,5000);
        QCOMPARE(controller.hiddenHydroIds().front().toString(),QStringLiteral("fixture:4"));
        QCOMPARE(controller.revision(),revision+1);
        QCOMPARE(controller.primaryObject().value("domain").toString(),QStringLiteral("hydro"));
        controller.undo();QVERIFY(controller.hiddenHydroIds().isEmpty());
        QCOMPARE(controller.revision(),revision+2);
    }
    void fragmentedBuiltinCopiesAtomicallyAndSurvivesReopen(){
        QTemporaryDir dir;const auto manifest=QStringLiteral(WEB_HYDRO_FIXTURE)+"/v0.13.1/manifest.json";
        const auto path=dir.filePath("copied-hydro.json");
        EditorController controller({false,dir.filePath("private.json")});
        QVERIFY(controller.configureHydroData(QUrl::fromLocalFile(manifest)));
        QVERIFY(controller.selectObject({{"domain","hydroBuiltin"},{"id","fixture:5"}}));
        const auto revision=controller.revision();
        QVERIFY(controller.copyBuiltinHydro());
        QTRY_COMPARE_WITH_TIMEOUT(controller.hiddenHydroIds().size(),1,5000);
        QCOMPARE(controller.revision(),revision+1);
        QCOMPARE(controller.primaryObject().value("domain").toString(),QStringLiteral("hydro"));
        QCOMPARE(controller.hiddenHydroIds().front().toString(),QStringLiteral("fixture:5"));
        const auto copiedId=controller.primaryObject().value("id").toString();
        controller.setSearchQuery("Split");
        const auto results=controller.searchResults();
        const auto original=std::find_if(results.begin(),results.end(),[](const QVariant& row){
            return row.toMap().value("id").toString()=="fixture:5";
        });
        QVERIFY(original!=results.end());QVERIFY(!original->toMap().value("visible").toBool());
        QVERIFY(controller.saveFile(QUrl::fromLocalFile(path)));
        EditorController reopened({false,dir.filePath("second-private.json")});
        QVERIFY(reopened.openFile(QUrl::fromLocalFile(path)));
        QCOMPARE(reopened.hiddenHydroIds().front().toString(),QStringLiteral("fixture:5"));
        QVERIFY(reopened.selectObject({{"domain","hydro"},{"id",copiedId}}));
        QCOMPARE(reopened.primaryObject().value("id").toString(),copiedId);
        QVERIFY(reopened.beginContentEdit("hydro"));
        QVERIFY(reopened.beginContentGeometry());
        const auto firstVertex=[&]{return reopened.geometryDraftPaths().front().toMap()
            .value("vertices").toList().front().toMap();};
        const auto before=firstVertex();
        QVERIFY(reopened.geometrySetMoveMode(true));
        QVERIFY(reopened.geometryBeginObjectDrag());
        QVERIFY(reopened.geometryTranslateObject(1,2));
        QVERIFY(qAbs(firstVertex().value("x").toDouble()-before.value("x").toDouble()-1)<1e-7);
        QVERIFY(qAbs(firstVertex().value("y").toDouble()-before.value("y").toDouble()-2)<1e-7);
        reopened.geometryEndObjectDrag();
        QVERIFY(reopened.geometryUndoDraft());
        QVERIFY(qAbs(firstVertex().value("x").toDouble()-before.value("x").toDouble())<1e-7);
        QVERIFY(reopened.geometryRedoDraft());
        QVERIFY(qAbs(firstVertex().value("x").toDouble()-before.value("x").toDouble()-1)<1e-7);
        reopened.cancelGeometryEdit();reopened.cancelContentEdit();
        controller.undo();QVERIFY(controller.hiddenHydroIds().isEmpty());
        controller.redo();QCOMPARE(controller.hiddenHydroIds().size(),1);
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
