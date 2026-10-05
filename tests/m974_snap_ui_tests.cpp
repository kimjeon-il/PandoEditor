#include "editorcontroller.h"
#include "windowsframe.h"
#include "ui_navigation.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QFile>
#include <QTest>
#include <QJsonDocument>
#include <cmath>
using namespace pandoeditor;
class M974SnapUiTests : public QObject {
    Q_OBJECT
private slots:
    void actualPointerTypeControlsSnapRadius_data(){
        QTest::addColumn<bool>("mobile");QTest::addColumn<bool>("touch");QTest::addColumn<double>("latitude");
        for(const auto latitude:{0.,59.})for(const bool mobile:{false,true})for(const bool touch:{false,true})
            QTest::newRow(qPrintable(QString("%1-%2-lat%3").arg(mobile?"small":"desktop",touch?"touch":"mouse").arg(latitude)))<<mobile<<touch<<latitude;
    }
    void actualPointerTypeControlsSnapRadius(){
        QFETCH(bool,mobile);QFETCH(bool,touch);QFETCH(double,latitude);QTemporaryDir files;EditorControllerConfig config;config.mobileMode=mobile;config.bootstrapWorld=false;config.autosaveEnabled=false;config.privateProjectPath=files.filePath("private.json");EditorController editor(config);
        Project project;project.replace(std::vector<Country>{{"A","A",{{{{0,latitude},{2,latitude},{2,latitude+2},{0,latitude+2},{0,latitude}}}},0xabcdef}});QFile file(files.filePath("project.json"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(projectcodec::encode(project));file.close();QVERIFY(editor.openFile(QUrl::fromLocalFile(file.fileName())));editor.selectCountry("A");QCOMPARE(editor.selectedId(),QString("A"));QVERIFY(editor.setProjectionMode("flat"));
        QQmlApplicationEngine engine;QStringList warnings;connect(&engine,&QQmlEngine::warnings,&engine,[&](const auto& errors){for(const auto& error:errors)warnings.append(error.toString());});engine.rootContext()->setContextProperty("editor",&editor);const auto root=qEnvironmentVariable("M974_TEST_UI_ROOT");engine.load(root.isEmpty()?QUrl("qrc:/common/Main.qml"):QUrl::fromLocalFile(root+"/common/Main.qml"));QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());QVERIFY(window);window->resize(mobile?360:1100,760);window->show();QTest::qWait(150);
        auto map=navigationItem(window->contentItem(),"mapView");QVERIFY(map);QVERIFY(editor.beginGeometryDraw());QTest::qWait(50);
        const auto first=editor.mapViewState();QVERIFY(editor.zoomMapCameraAt(100/first["mapScale"].toDouble(),map->width()/2,map->height()/2));
        MapProjection projection;projection.rebuild(project.document());const auto edge=projection.project({0,latitude+1});auto camera=editor.mapViewState();const double edgeX=mobile?150:450,edgeY=150;
        editor.beginMapCameraPan();QVERIFY(editor.updateMapCameraPan(edgeX-(camera["originX"].toDouble()+edge.x*camera["mapScale"].toDouble()),edgeY-(camera["originY"].toDouble()+edge.y*camera["mapScale"].toDouble())));editor.endMapCameraPan();QTest::qWait(60);window->grabWindow();
        const auto at=map->mapToScene(QPointF(edgeX-14,edgeY)).toPoint();QVERIFY(QRect(QPoint(),window->size()).contains(at));
        const auto tapAt=[&](bool useTouch,const QPoint& position){if(useTouch){static auto device=QTest::createTouchDevice();QTest::touchEvent(window,device).press(0,position,window).commit();QTest::touchEvent(window,device).release(0,position,window).commit();}else QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,position);QTest::qWait(40);};
        const auto tap=[&]{tapAt(touch,at);};
        // Establish the real mouse hover before either device's first input;
        // this also exercises switching from a mouse to a touch tap in one view.
        QTest::mouseMove(window,at);QTest::qWait(80);QCOMPARE(editor.geometrySnapState()["status"].toString(),QString("empty"));QCOMPARE(editor.geometrySnapState()["submitted"].toULongLong(),qulonglong(0));
        tap();QTRY_COMPARE_WITH_TIMEOUT(editor.geometrySnapState()["status"].toString(),QString("ready"),3000);tap();
        const auto indicator=editor.geometrySnapState()["indicator"].toMap();if(touch){QVERIFY2(indicator["kind"].toString()==QString("edge"),qPrintable(QString::fromUtf8(QJsonDocument::fromVariant(editor.geometrySnapState()).toJson(QJsonDocument::Compact))));QVERIFY(!indicator.isEmpty());const auto state=editor.geometryEditState();const auto display=editor.mapViewState();
            QCOMPARE(display["originX"].toDouble()+state["snapX"].toDouble()*display["mapScale"].toDouble(),edgeX);
            const auto localPointer=map->mapFromScene(at);
            QVERIFY(std::abs(display["originY"].toDouble()+state["snapY"].toDouble()*display["mapScale"].toDouble()-localPointer.y())<1e-9);
            auto marker=navigationItem(map,"geometrySnapIndicator");QVERIFY(marker);QVERIFY(marker->isVisible());
            const auto center=marker->mapToItem(map,QPointF(marker->width()/2,marker->height()/2));
            QVERIFY(std::abs(center.x()-edgeX)<1e-9);QVERIFY(std::abs(center.y()-localPointer.y())<1e-9);}else QVERIFY2(indicator.isEmpty(),qPrintable(QString("mouse snapped at 14 px on mobile=%1: %2").arg(mobile).arg(indicator["kind"].toString())));
        const auto vertices=editor.geometryDraftPaths().front().toMap()["vertices"].toList();
        QCOMPARE(vertices.size(),2);
        const auto committed=vertices.back().toMap();
        const auto committedCamera=editor.mapViewState();
        QCOMPARE(committedCamera["originX"].toDouble()+committed["x"].toDouble()*committedCamera["mapScale"].toDouble(),touch?edgeX:double(map->mapFromScene(at).x()));
        {
            // A subsequent real mouse move must still use mouse semantics.
            QTest::mouseMove(window,map->mapToScene(QPointF(edgeX-25,edgeY)).toPoint());
            QTRY_VERIFY(editor.geometrySnapState()["indicator"].toMap().isEmpty());
            QTRY_COMPARE(editor.geometrySnapState()["status"].toString(),QString("ready"));
            QTest::mouseMove(window,map->mapToScene(QPointF(edgeX-8,edgeY)).toPoint());
            QTRY_COMPARE(editor.geometrySnapState()["status"].toString(),QString("ready"));
            QTest::mouseMove(window,map->mapToScene(QPointF(edgeX-7,edgeY)).toPoint());
            QTRY_COMPARE(editor.geometrySnapState()["indicator"].toMap()["kind"].toString(),QString("edge"));
        }
        // A real mouse exit must clear the marker after mouse takeover.
        auto marker=navigationItem(map,"geometrySnapIndicator");QVERIFY(marker);
        QTest::mouseMove(window,QPoint(-100,-100));
        QTRY_VERIFY(editor.geometrySnapState()["indicator"].toMap().isEmpty());
        QTRY_VERIFY(!marker->isVisible());
        const auto reentry=map->mapToScene(QPointF(edgeX-7,edgeY)).toPoint();
        QTest::mouseMove(window,reentry);
        QTRY_COMPARE(editor.geometrySnapState()["indicator"].toMap()["kind"].toString(),QString("edge"));
        tapAt(true,at);
        QCOMPARE(editor.geometrySnapState()["indicator"].toMap()["kind"].toString(),QString("edge"));
        editor.cancelGeometryEdit();
        QTRY_VERIFY(!editor.geometryEditState()["active"].toBool());
        QCOMPARE(editor.geometrySnapState()["status"].toString(),QString("empty"));
        QTRY_VERIFY(!marker->isVisible());
        QVERIFY(editor.beginGeometryDraw());
        // Seed only the new draft. A real mouse move must warm a fresh
        // session's provider after canceling a touch-owned marker.
        QVERIFY(editor.geometryAddPoint(edge.x,edge.y,0,"mouse"));
        QTest::mouseMove(window,map->mapToScene(QPointF(edgeX-8,edgeY)).toPoint());
        QTRY_COMPARE_WITH_TIMEOUT(editor.geometrySnapState()["status"].toString(),QString("ready"),3000);
        QTest::mouseMove(window,reentry);
        QTRY_COMPARE(editor.geometrySnapState()["indicator"].toMap()["kind"].toString(),QString("edge"));
        // Isolate the physical cursor from the next row's new window.
        QTest::mouseMove(window,QPoint(-100,-100));
        QTRY_VERIFY(editor.geometrySnapState()["indicator"].toMap().isEmpty());
        editor.cancelGeometryEdit();QCOMPARE(editor.geometrySnapState()["status"].toString(),QString("empty"));window->setProperty("allowClose",true);window->close();
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
    }
};
int main(int argc,char** argv){QQuickStyle::setStyle("Basic");QGuiApplication app(argc,argv);registerWindowsFrameType();M974SnapUiTests tests;return QTest::qExec(&tests,argc,argv);}
#include "m974_snap_ui_tests.moc"
