#include "ui_navigation.h"
#include "editorcontroller.h"
#include "windowsframe.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QDir>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QLocale>
#include <algorithm>
#include <cmath>

using namespace pandoeditor;
namespace {
QByteArray readFile(const QString& path) {
    QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray{};
}
bool writeFile(const QString& path,const QByteArray& bytes) {
    QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();
}
bool clickControl(QQuickWindow* window,const QString& name,bool settle=true) {
    enterExistingControlRoute(window,name);
    auto* item=navigationItem(window->contentItem(),name);
    if(!item||!item->isVisible()||!item->isEnabled())return false;
    navigationEnsureVisible(item);window->grabWindow();
    QRectF visibleRect(item->mapToScene({}),QSizeF(item->width(),item->height()));
    for(auto* parent=item->parentItem();parent;parent=parent->parentItem())if(parent->clip())
        visibleRect=visibleRect.intersected(QRectF(parent->mapToScene({}),QSizeF(parent->width(),parent->height())));
    visibleRect=visibleRect.intersected(QRectF(QPointF(),window->size()));
    if(visibleRect.isEmpty())return false;
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,visibleRect.center().toPoint());
    if(settle)QTest::qWait(60);
    return true;
}
struct RiverUi {
    QTemporaryDir files;
    ProjectDocument document;
    QStringList warnings;
    EditorController editor;
    QQmlApplicationEngine engine;
    QQuickWindow* window=nullptr;
    QQuickItem* map=nullptr;
    MapProjection projection;
    QByteArray before;
    qulonglong revision=0;
    Point focus{40.35,.45};
    double focusWidth=.5;
    explicit RiverUi(int width):
        document({{"target","Target",{{{{50,0},{51,0},{51,1},{50,1},{50,0}}}},0x112233},
                  {"donor","Donor",{{{{40.1,.2},{40.6,.2},{40.6,.7},{40.1,.7},{40.1,.2}}},{{{41.1,1.2},{41.6,1.2},{41.6,1.7},{41.1,1.7},{41.1,1.2}}}},0x445566}},
                 {{"countries","Countries"}}),
        editor([&]{EditorControllerConfig c;c.mobileMode=width==360;c.bootstrapWorld=false;c.autosaveEnabled=false;
                    c.privateProjectPath=files.filePath("private.json");return c;}()) {
        document.documentId="river-ui";
        document.physicalData.source=std::string(WEB_HYDRO_FIXTURE)+"/v0.13.1/manifest.json";
        QObject::connect(&engine,&QQmlEngine::warnings,&engine,[this](const QList<QQmlError>& errors){
            for(const auto& error:errors)warnings<<error.toString();
        });
    }
    bool start(int width) {
        Project project;project.replace(document);
        const auto path=files.filePath("project.json");
        if(!writeFile(path,projectcodec::encode(project))||!editor.openFile(QUrl::fromLocalFile(path)))return false;
        editor.selectCountry("target");if(!editor.setProjectionMode("flat"))return false;
        engine.rootContext()->setContextProperty("editor",&editor);engine.load(QUrl("qrc:/common/Main.qml"));
        if(engine.rootObjects().isEmpty())return false;
        window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());if(!window)return false;
        window->resize(width,760);QTest::qWait(150);window->grabWindow();
        map=navigationItem(window->contentItem(),"mapView");if(!map)return false;
        projection.rebuild(document);
        const auto left=projection.project({focus.x-focusWidth/2,focus.y}),right=projection.project({focus.x+focusWidth/2,focus.y});
        if(!editor.zoomMapCameraAt((width==360?200.:300.)/(std::abs(right.x-left.x)*editor.mapViewState()["mapScale"].toDouble()),map->width()/2,map->height()/2))return false;
        const auto center=projection.project(focus);const auto camera=editor.mapViewState();
        editor.beginMapCameraPan();
        const bool moved=editor.updateMapCameraPan((width==360?180.:300.)-(camera["originX"].toDouble()+center.x*camera["mapScale"].toDouble()),
                                                   (width==360?170.:270.)-(camera["originY"].toDouble()+center.y*camera["mapScale"].toDouble()));
        editor.endMapCameraPan();before=editor.documentBytes();revision=editor.revision();return moved;
    }
    bool unchanged() const {return editor.documentBytes()==before&&editor.revision()==revision&&!editor.canUndo();}
    QVariantMap state() const {return editor.geometryEditState();}
    QQuickItem* item(const QString& name) const {return navigationItem(window->contentItem(),name);}
    bool click(const QString& name,bool settle=true){return clickControl(window,name,settle);}
    void tap(Point geographic) {
        const auto projected=projection.project(geographic);const auto camera=editor.mapViewState();
        const QPointF local(camera["originX"].toDouble()+projected.x*camera["mapScale"].toDouble(),camera["originY"].toDouble()+projected.y*camera["mapScale"].toDouble());
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,map->mapToScene(local).toPoint());QTest::qWait(60);
    }
    QString copyHydroFixture() {
        const auto root=files.filePath("hydro");
        for(const auto& version:{QString("v0.13.0"),QString("v0.13.1")}) {
            const auto source=QStringLiteral(WEB_HYDRO_FIXTURE)+"/"+version;
            QDirIterator it(source,QDir::Files,QDirIterator::Subdirectories);
            while(it.hasNext()) {
                const auto from=it.next(),to=root+"/"+version+"/"+QDir(source).relativeFilePath(from);
                if(!QDir().mkpath(QFileInfo(to).path())||!QFile::copy(from,to))return {};
            }
        }
        document.physicalData.source=(root+"/v0.13.1/manifest.json").toStdString();
        return root;
    }
    ~RiverUi(){if(window){window->setProperty("allowClose",true);window->close();}}
};
}
class RiverSelectionUiTests:public QObject {
    Q_OBJECT
private slots:
    void riverControlsFollowProductionMethod_data() {
        QTest::addColumn<int>("width");QTest::newRow("desktop")<<1100;QTest::newRow("mobile-360")<<360;
    }
    void riverControlsFollowProductionMethod() {
        QFETCH(int,width);RiverUi ui(width);QVERIFY2(ui.start(width),qPrintable(ui.warnings.join('\n')));
        QVERIFY(ui.click("editorAnnexAction"));ui.tap({40.35,.45});
        QTRY_COMPARE(ui.state()["providers"].toList().size(),1);
        QVERIFY(ui.click("geometryAdvance"));QVERIFY(ui.click("geometryMethod_components"));
        QTRY_VERIFY(!ui.state()["selectionPending"].toBool());
        auto* toggle=ui.item("geometryRiverBoundaries");
        QVERIFY2(toggle&&toggle->isVisible(),"Component selection must expose the production river-boundary toggle");
        QVERIFY(toggle->isEnabled());QVERIFY(!toggle->property("checked").toBool());
        QVERIFY(ui.item("geometryRiverStatus")->isVisible());
        QVERIFY(ui.click("geometryMethod_polygon"));
        QVERIFY(!toggle->isVisible());
        QVERIFY(ui.click("geometryMethod_components"));QTRY_VERIFY(!ui.state()["selectionPending"].toBool());
        QVERIFY(toggle->isVisible());QVERIFY(toggle->isEnabled());
        QVERIFY(ui.unchanged());QVERIFY(ui.click("geometryCancel"));
        QVERIFY(!ui.state()["active"].toBool());QVERIFY(ui.unchanged());
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }
    void realRiverSelectionLifecycle_data() {
        QTest::addColumn<int>("width");QTest::newRow("desktop")<<1100;QTest::newRow("mobile-360")<<360;
    }
    void realRiverSelectionLifecycle() {
        QFETCH(int,width);RiverUi ui(width);QVERIFY2(ui.start(width),qPrintable(ui.warnings.join('\n')));
        QVERIFY(ui.click("editorAnnexAction"));ui.tap({40.35,.45});
        QTRY_COMPARE(ui.state()["providers"].toList().size(),1);
        QVERIFY(ui.click("geometryAdvance"));QVERIFY(ui.click("geometryMethod_components"));
        QTRY_VERIFY(!ui.state()["selectionPending"].toBool());
        bool sawLoading=false,loadingVisible=false,escapeEnabled=false;
        connect(&ui.editor,&EditorController::geometryEditChanged,this,[&]{
            if(ui.state()["riverStatus"]!="pending")return;
            sawLoading=true;
            const auto* loading=ui.item("geometryRiverLoading");
            loadingVisible=loadingVisible||(loading&&loading->isVisible()&&loading->property("running").toBool());
            const auto* toggle=ui.item("geometryRiverBoundaries");
            escapeEnabled=escapeEnabled||(toggle&&toggle->isEnabled()&&ui.item("geometryBack")->isEnabled()&&ui.item("geometryCancel")->isEnabled());
        });
        QVERIFY(ui.click("geometryRiverBoundaries"));
        QTRY_COMPARE_WITH_TIMEOUT(ui.state()["riverStatus"].toString(),QString("ready"),15000);
        QVERIFY(sawLoading);QVERIFY(loadingVisible);QVERIFY(escapeEnabled);QVERIFY(ui.unchanged());
        QVERIFY(ui.window->grabWindow().save(QDir::tempPath()+"/m972-river-ui-"+QString::number(width)+"-ready.png"));
        const auto cells=ui.state()["components"].toList();QCOMPARE(cells.size(),4);
        QSet<QString> labels;
        for(const auto& cell:cells) {
            const auto row=cell.toMap();QCOMPARE(row["partitionKind"].toString(),QString("river"));
            QVERIFY(QJsonDocument::fromJson(row["provenance"].toString().toUtf8()).object()["sourceRiverIds"].toArray().contains("fixture:5"));
            auto* overlay=ui.item("geometrySelectionOverlay_component_"+row["key"].toString());
            QVERIFY(overlay&&overlay->isVisible());
            labels.insert(ui.item("geometryComponent_"+row["key"].toString())->property("text").toString());
        }
        QCOMPARE(labels.size(),cells.size());
        // Select one actual cell on the map, then its neighbor in the list.
        ui.tap({40.15,.6});QCOMPARE(ui.state()["selectedComponentKeys"].toList().size(),1);
        const auto selected=ui.state()["selectedComponentKeys"].toList().front().toString();
        const auto other=cells[0].toMap()["key"].toString()==selected?cells[1].toMap()["key"].toString():cells[0].toMap()["key"].toString();
        QVERIFY(ui.click("geometryComponent_"+other));QCOMPARE(ui.state()["selectedComponentKeys"].toList().size(),2);
        QCOMPARE(ui.state()["selectedVertex"].toInt(),-1);
        for(const auto& path:ui.editor.geometryDraftPaths())QVERIFY(path.toMap()["vertices"].toList().isEmpty());
        QVERIFY(!ui.state()["previewReady"].toBool());
        QVERIFY(!ui.item("geometryTransferMetrics")->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(ui.state()["canAddPart"].toBool(),15000);
        QVERIFY(std::abs(ui.state()["transferArea"].toDouble()-.25)<1e-10);
        QVERIFY(writeFile(QDir::tempPath()+"/m972-river-ui-mini-receipt.json",QJsonDocument::fromVariant(ui.editor.riverSelectionObservation()["transferredGeometry"]).toJson(QJsonDocument::Compact)));
        QVERIFY2(ui.state().contains("transferAreaKm2"),"A ready receipt must expose the exact pinned-web square-kilometer area");
        const double areaKm2=ui.state()["transferAreaKm2"].toDouble();
        QVERIFY(std::isfinite(areaKm2)&&areaKm2>0);
        QVERIFY(writeFile(QDir::tempPath()+"/m972-river-ui-mini-area.json",QJsonDocument::fromVariant(QVariantMap{{"geometry",ui.editor.riverSelectionObservation()["transferredGeometry"]},{"transferAreaKm2",areaKm2}}).toJson(QJsonDocument::Compact)));
        QVERIFY(ui.item("geometryTransferMetrics")&&ui.item("geometryTransferMetrics")->isVisible());
        QVERIFY(ui.item("geometryTransferMetrics")->property("text").toString().contains(QStringLiteral("3,091")));
        QVERIFY(ui.item("geometryTransferMetrics")->property("text").toString().endsWith(QStringLiteral(" km²")));
        QVERIFY(!ui.item("geometryTransferMetrics")->property("text").toString().contains(QStringLiteral("좌표")));
        QVERIFY(ui.unchanged());QVERIFY(ui.click("geometryArchivePart"));
        QTRY_COMPARE(ui.state()["parts"].toList().size(),2);
        QVERIFY(!ui.state()["useRiverBoundaries"].toBool());
        const auto firstPart=ui.state()["parts"].toList().front().toMap()["id"].toString();
        const auto secondPart=ui.state()["parts"].toList()[1].toMap()["id"].toString();
        QVERIFY(ui.click("geometryMethod_polygon"));ui.tap({40.2,.3});ui.tap({40.3,.3});
        QVERIFY(ui.click("geometryMethod_line"));
        QCOMPARE(ui.state()["confirmationKind"].toString(),QString("method"));
        QVERIFY(ui.click("geometryCancelTerritoryChange"));
        QCOMPARE(ui.state()["activeMethod"].toString(),QString("polygon"));
        QVERIFY(ui.click("geometryMethod_line"));QVERIFY(ui.click("geometryConfirmTerritoryChange"));
        QCOMPARE(ui.state()["activeMethod"].toString(),QString("line"));
        QCOMPARE(ui.state()["parts"].toList().front().toMap()["id"].toString(),firstPart);QVERIFY(ui.unchanged());
        QVERIFY(ui.click("geometryMethod_components"));QTRY_VERIFY(!ui.state()["selectionPending"].toBool());
        QVERIFY(ui.click("geometryRiverBoundaries"));
        QTRY_COMPARE_WITH_TIMEOUT(ui.state()["riverStatus"].toString(),QString("ready"),15000);
        QTRY_COMPARE(ui.state()["components"].toList().size(),2);
        for(const auto& cell:ui.state()["components"].toList())
            QCOMPARE(cell.toMap()["sourcePolygonIndex"].toInt(),1);
        QTRY_VERIFY_WITH_TIMEOUT(!ui.state()["calculating"].toBool(),15000);
        QVERIFY(ui.state()["selectedComponentKeys"].toList().isEmpty());
        QVERIFY2(!ui.state()["previewReady"].toBool(),"Archived geometry must not make an empty live component selection ready");
        QVERIFY(!ui.state()["canAdvance"].toBool());
        QVERIFY(!ui.item("geometryTransferMetrics")->isVisible());
        const auto residualCell=ui.state()["components"].toList().front().toMap()["key"].toString();
        QVERIFY(ui.click("geometryComponent_"+residualCell));
        QTRY_VERIFY_WITH_TIMEOUT(ui.state()["previewReady"].toBool()&&ui.state()["canAdvance"].toBool(),15000);
        QVERIFY(ui.item("geometryTransferMetrics")->isVisible());QVERIFY(ui.unchanged());
        QVERIFY(ui.click("geometryComponent_"+residualCell));
        QTRY_VERIFY_WITH_TIMEOUT(!ui.state()["calculating"].toBool(),15000);
        QVERIFY(!ui.state()["previewReady"].toBool());QVERIFY(!ui.state()["canAdvance"].toBool());
        QVERIFY(!ui.item("geometryTransferMetrics")->isVisible());QVERIFY(ui.unchanged());
        QVERIFY(ui.click("geometryRemovePart_"+firstPart));
        QTRY_COMPARE_WITH_TIMEOUT(ui.state()["riverStatus"].toString(),QString("ready"),15000);
        QCOMPARE(ui.state()["parts"].toList().size(),1);
        QCOMPARE(ui.state()["parts"].toList().front().toMap()["id"].toString(),secondPart);
        QVERIFY(ui.click("geometryRemovePart_"+secondPart));
        QTRY_COMPARE_WITH_TIMEOUT(ui.state()["riverStatus"].toString(),QString("ready"),15000);
        QTRY_COMPARE(ui.state()["components"].toList().size(),4);
        QVERIFY(ui.state()["selectedComponentKeys"].toList().isEmpty());QVERIFY(ui.unchanged());
        const auto restored=ui.state()["components"].toList();
        QVERIFY(ui.click("geometryComponent_"+restored[0].toMap()["key"].toString()));
        QVERIFY(ui.click("geometryComponent_"+restored[1].toMap()["key"].toString()));
        QTRY_VERIFY_WITH_TIMEOUT(ui.state()["canAddPart"].toBool(),15000);
        QVERIFY(ui.click("geometryArchivePart"));
        QTRY_VERIFY_WITH_TIMEOUT(ui.state()["canAdvance"].toBool(),15000);
        QVERIFY(ui.click("geometryReview"));QCOMPARE(ui.state()["stage"].toString(),QString("review"));
        QVERIFY(ui.window->grabWindow().save(QDir::tempPath()+"/m972-river-ui-"+QString::number(width)+"-review.png"));
        QVERIFY(ui.click("geometryBack"));QCOMPARE(ui.state()["stage"].toString(),QString("selection"));
        QVERIFY(ui.state()["previewReady"].toBool());
        QVERIFY(ui.click("geometryBack"));QCOMPARE(ui.state()["stage"].toString(),QString("setup"));
        QVERIFY(ui.click("geometryRemoveProvider_donor"));
        QCOMPARE(ui.state()["confirmationKind"].toString(),QString("settings"));
        QVERIFY(ui.click("geometryCancelTerritoryChange"));
        QCOMPARE(ui.state()["parts"].toList().size(),2);QVERIFY(ui.unchanged());
        QVERIFY(ui.click("geometryAdvance"));QVERIFY(ui.click("geometryReview"));
        QVERIFY(ui.click("geometryConfirm"));
        QTRY_VERIFY_WITH_TIMEOUT(!ui.state()["active"].toBool(),15000);
        QCOMPARE(ui.editor.revision(),ui.revision+1);QVERIFY(ui.editor.canUndo());
        const auto after=ui.editor.documentBytes();QVERIFY(after!=ui.before);
        QVERIFY(ui.click("undoButton"));QCOMPARE(ui.editor.documentBytes(),ui.before);
        QVERIFY(ui.click("redoButton"));QCOMPARE(ui.editor.documentBytes(),after);
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }
    void sourceErrorCanRetryRealAssets_data() {
        QTest::addColumn<int>("width");QTest::newRow("desktop")<<1100;QTest::newRow("mobile-360")<<360;
    }
    void sourceErrorCanRetryRealAssets() {
        QFETCH(int,width);RiverUi ui(width);const auto root=ui.copyHydroFixture();QVERIFY(!root.isEmpty());
        const auto detail=root+"/v0.13.0/metadata-detail.json.gz";const auto original=readFile(detail);
        QVERIFY(!original.isEmpty());QVERIFY(writeFile(detail,"damaged source"));
        QVERIFY2(ui.start(width),qPrintable(ui.warnings.join('\n')));
        QVERIFY(ui.click("editorAnnexAction"));ui.tap({40.35,.45});
        QTRY_COMPARE(ui.state()["providers"].toList().size(),1);
        QVERIFY(ui.click("geometryAdvance"));QVERIFY(ui.click("geometryMethod_components"));
        QTRY_VERIFY(!ui.state()["selectionPending"].toBool());
        QVERIFY(ui.click("geometryRiverBoundaries"));
        QTRY_COMPARE_WITH_TIMEOUT(ui.state()["riverStatus"].toString(),QString("sourceError"),15000);
        QVERIFY(!ui.state()["riverError"].toString().isEmpty());
        QVERIFY(ui.state()["components"].toList().isEmpty());
        QVERIFY(ui.item("geometryRiverStatus")->property("text").toString().contains(ui.state()["riverError"].toString()));
        QVERIFY(ui.item("geometryRetryRiverPartitions")->isVisible());QVERIFY(ui.item("geometryRetryRiverPartitions")->isEnabled());
        QVERIFY(ui.item("geometryRiverBoundaries")->isEnabled());QVERIFY(ui.item("geometryBack")->isEnabled());QVERIFY(ui.item("geometryCancel")->isEnabled());
        QVERIFY(!ui.state()["canAddPart"].toBool());QVERIFY(ui.unchanged());
        QVERIFY(writeFile(detail,original));QVERIFY(ui.click("geometryRetryRiverPartitions"));
        QTRY_COMPARE_WITH_TIMEOUT(ui.state()["riverStatus"].toString(),QString("ready"),15000);
        QCOMPARE(ui.state()["components"].toList().size(),4);
        QVERIFY(!ui.item("geometryRetryRiverPartitions")->isVisible());QVERIFY(ui.state()["riverError"].toString().isEmpty());
        QVERIFY(ui.unchanged());QVERIFY(ui.click("geometryCancel"));QVERIFY(ui.unchanged());
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

    void pendingRiverControlsCanEscape_data() {
        QTest::addColumn<int>("width");QTest::addColumn<QString>("exitControl");
        for(const auto width:{1100,360})for(const auto& control:{QString("geometryRiverBoundaries"),QString("geometryBack"),QString("geometryCancel"),QString("geometryMethod_polygon")})
            QTest::newRow(qPrintable(QString::number(width)+"-"+control))<<width<<control;
    }
    void pendingRiverControlsCanEscape() {
        QFETCH(int,width);QFETCH(QString,exitControl);RiverUi ui(width);
        QVERIFY2(ui.start(width),qPrintable(ui.warnings.join('\n')));
        QVERIFY(ui.click("editorAnnexAction"));ui.tap({40.35,.45});
        QTRY_COMPARE(ui.state()["providers"].toList().size(),1);
        QVERIFY(ui.click("geometryAdvance"));QVERIFY(ui.click("geometryMethod_components"));
        QTRY_VERIFY(!ui.state()["selectionPending"].toBool());
        QVERIFY(ui.click("geometryRiverBoundaries",false));
        QCOMPARE(ui.state()["riverStatus"].toString(),QString("pending"));
        QVERIFY(ui.state()["components"].toList().isEmpty());
        QVERIFY(ui.item("geometryRiverLoading")->isVisible());
        QVERIFY(!ui.state()["canAddPart"].toBool());QVERIFY(!ui.state()["canApply"].toBool());
        QVERIFY(ui.click(exitControl,false));
        if(exitControl=="geometryCancel")QVERIFY(!ui.state()["active"].toBool());
        else if(exitControl=="geometryBack")QCOMPARE(ui.state()["stage"].toString(),QString("setup"));
        else if(exitControl=="geometryMethod_polygon")QCOMPARE(ui.state()["activeMethod"].toString(),QString("polygon"));
        else QVERIFY(!ui.state()["useRiverBoundaries"].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!ui.state()["calculating"].toBool(),15000);
        QVERIFY(ui.state()["riverStatus"].toString()!="pending");
        if(exitControl=="geometryBack") {
            QVERIFY(!ui.item("geometryRiverLoading")->isVisible());
            QVERIFY(ui.click("geometryAdvance"));
            QTRY_COMPARE_WITH_TIMEOUT(ui.state()["riverStatus"].toString(),QString("ready"),15000);
            QCOMPARE(ui.state()["components"].toList().size(),4);
        } else {
            for(const auto& component:ui.state()["components"].toList())QVERIFY(component.toMap()["partitionKind"]!="river");
        }
        QVERIFY(ui.unchanged());
        // The next session/request must settle instead of reviving the cancelled cells.
        if(ui.state()["active"].toBool())QVERIFY(ui.click("geometryCancel"));
        QVERIFY(ui.click("editorAnnexAction"));ui.tap({40.35,.45});
        QTRY_COMPARE(ui.state()["providers"].toList().size(),1);
        QVERIFY(ui.click("geometryAdvance"));QVERIFY(ui.click("geometryMethod_components"));
        QTRY_VERIFY(!ui.state()["selectionPending"].toBool());
        QVERIFY(ui.click("geometryRiverBoundaries"));
        QTRY_COMPARE_WITH_TIMEOUT(ui.state()["riverStatus"].toString(),QString("ready"),15000);
        QCOMPARE(ui.state()["components"].toList().size(),4);QVERIFY(ui.unchanged());
        QVERIFY(ui.click("geometryCancel"));
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }
    void emptyRiverSourceShowsOriginalFallback() {
        RiverUi ui(360);const auto source=ui.document.physicalData;
        ui.document=ProjectDocument({{"target","Target",{{{{50,0},{51,0},{51,1},{50,1},{50,0}}}},0x112233},
                                     {"donor","Donor",{{{{20.1,.2},{20.6,.2},{20.6,.7},{20.1,.7},{20.1,.2}}}},0x445566}},{{"countries","Countries"}});
        ui.document.physicalData=source;ui.document.documentId="river-ui-empty";ui.focus={20.35,.45};ui.focusWidth=.5;
        QVERIFY2(ui.start(360),qPrintable(ui.warnings.join('\n')));
        QVERIFY(ui.click("editorAnnexAction"));ui.tap(ui.focus);
        QTRY_COMPARE(ui.state()["providers"].toList().size(),1);
        QVERIFY(ui.click("geometryAdvance"));QVERIFY(ui.click("geometryMethod_components"));
        QTRY_VERIFY(!ui.state()["selectionPending"].toBool());QVERIFY(ui.click("geometryRiverBoundaries"));
        QTRY_COMPARE_WITH_TIMEOUT(ui.state()["riverStatus"].toString(),QString("ready"),15000);
        QCOMPARE(ui.state()["components"].toList().size(),1);
        QCOMPARE(ui.state()["components"].toList().front().toMap()["partitionKind"].toString(),QString("original"));
        QVERIFY(ui.item("geometryRiverStatus")->property("text").toString().contains(QStringLiteral("원래 구성 영역")));
        QVERIFY(ui.unchanged());QVERIFY(ui.click("geometryCancel"));
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

};
int main(int argc,char** argv) {
    QQuickStyle::setStyle("Basic");QGuiApplication app(argc,argv);registerWindowsFrameType();
    RiverSelectionUiTests test;return QTest::qExec(&test,argc,argv);
}
#include "river_selection_ui_tests.moc"
