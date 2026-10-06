#include "editorcontroller.h"
#include "windowsframe.h"
#include "ui_navigation.h"
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTest>
#include <QVector2D>
#include <algorithm>
#include <cmath>

using namespace pandoeditor;

namespace {
// Nonzero projection bounds and a high latitude make confusing geographic,
// overlay, screen, or delta coordinates observable in the resulting draft.
ProjectDocument fixture()
{
    ProjectDocument document({{"A","A",{{{{40,59},{44,59},{44,63},{40,63},{40,59}}}},0xabcdef}},
                             {{"countries","Countries"}});
    Geometry line;line.type="LineString";line.lines={{{41,60},{43,60}}};
    const GeometryRef geometry{"river-geometry",1};
    document.geometries.insert(geometry,line);
    HydroFeature river;river.id="river";river.name="River";river.geometry=geometry;
    document.hydro.push_back(river);
    return document;
}

struct Ui {
    QTemporaryDir files;
    ProjectDocument document=fixture();
    QStringList warnings;
    EditorController editor;
    QQmlApplicationEngine engine;
    QQuickWindow* window=nullptr;
    QQuickItem* map=nullptr;
    MapProjection projection;
    QByteArray before;
    int width;

    explicit Ui(int w):editor([&]{EditorControllerConfig c;c.mobileMode=w==360;
        c.bootstrapWorld=false;c.autosaveEnabled=false;
        c.privateProjectPath=files.filePath("private.json");return c;}()),width(w)
    {
        QObject::connect(&engine,&QQmlEngine::warnings,&engine,[this](const auto& errors){
            for(const auto& error:errors)warnings.append(error.toString());
        });
    }
    bool start()
    {
        Project project;project.replace(document);
        QFile file(files.filePath("input.json"));
        if(!file.open(QIODevice::WriteOnly)||file.write(projectcodec::encode(project))<=0)return false;
        file.close();
        if(!editor.openFile(QUrl::fromLocalFile(file.fileName()))||!editor.setProjectionMode("flat"))return false;
        engine.rootContext()->setContextProperty("editor",&editor);
        const auto root=qEnvironmentVariable("M974_TEST_UI_ROOT");
        engine.load(root.isEmpty()?QUrl("qrc:/common/Main.qml"):QUrl::fromLocalFile(root+"/common/Main.qml"));
        if(engine.rootObjects().isEmpty())return false;
        window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());if(!window)return false;
        window->resize(width,760);window->show();QTest::qWait(180);
        map=navigationItem(window->contentItem(),"mapView");if(!map)return false;
        projection.rebuild(document);before=editor.documentBytes();
        if(!editRiver())return false;
        const auto camera=editor.mapViewState();
        if(!editor.zoomMapCameraAt(80./(projection.cosLatitudeValue()*camera["mapScale"].toDouble()),
                                   map->width()/2,map->height()/2))return false;
        const auto first=projection.project({41,60});const auto view=editor.mapViewState();
        editor.beginMapCameraPan();
        const bool moved=editor.updateMapCameraPan(100.-(view["originX"].toDouble()+first.x*view["mapScale"].toDouble()),
                                                  160.-(view["originY"].toDouble()+first.y*view["mapScale"].toDouble()));
        editor.endMapCameraPan();QTest::qWait(80);window->grabWindow();return moved;
    }
    bool editRiver()
    {
        return editor.selectObject({{"domain","hydro"},{"id","river"}},"replace","test")&&
               editor.beginContentEdit("hydro",QString(),false)&&editor.beginContentGeometry();
    }
    QVariantList vertices() const {return editor.geometryDraftPaths().front().toMap()["vertices"].toList();}
    QPoint atVertex(int index) const
    {
        const auto point=vertices().at(index).toMap();const auto view=editor.mapViewState();
        return map->mapToScene({view["originX"].toDouble()+point["x"].toDouble()*view["mapScale"].toDouble(),
                               view["originY"].toDouble()+point["y"].toDouble()*view["mapScale"].toDouble()}).toPoint();
    }
    // This is deliberately the old explicit arithmetic, independent of the new
    // screen wrappers. A stale snapshot or double inversion must fail the test.
    Point overlayAt(const QPoint& scene) const
    {
        const auto local=map->mapFromScene(scene);const auto view=editor.mapViewState();
        return {(local.x()-view["originX"].toDouble())/view["mapScale"].toDouble(),
                (local.y()-view["originY"].toDouble())/view["mapScale"].toDouble()};
    }
    bool changeCamera()
    {
        editor.beginMapCameraPan();const bool panned=editor.updateMapCameraPan(13.,-9.);editor.endMapCameraPan();
        const bool zoomed=editor.zoomMapCameraAt(1.17,150,160);
        window->resize(width+23,783);QTest::qWait(50);
        window->resize(width,760);QTest::qWait(50);window->grabWindow();
        return panned&&zoomed;
    }
    static QPointingDevice* device(){static auto* result=QTest::createTouchDevice();return result;}
    void press(const QPoint& point,bool touch)
    {
        if(touch)QTest::touchEvent(window,device()).press(0,point,window).commit();
        else QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,point);
        QTest::qWait(25);
    }
    void move(const QPoint& point,bool touch)
    {
        if(touch)QTest::touchEvent(window,device()).move(0,point,window).commit();
        else QTest::mouseMove(window,point,20);
        QTest::qWait(25);
    }
    void release(const QPoint& point,bool touch)
    {
        if(touch)QTest::touchEvent(window,device()).release(0,point,window).commit();
        else QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,point);
        QTest::qWait(40);
    }
    void tap(const QPoint& point,bool touch){press(point,touch);release(point,touch);}
    QObject* activeDrag() const
    {
        for(auto* object:map->children())
            if(QByteArray(object->metaObject()->className()).contains("DragHandler")&&object->property("active").toBool())return object;
        return nullptr;
    }
    ~Ui(){if(window){QTest::mouseMove(window,QPoint(-100,-100));window->setProperty("allowClose",true);window->close();}}
};

void pointerRows()
{
    QTest::addColumn<int>("width");QTest::addColumn<bool>("touch");
    QTest::newRow("desktop-mouse")<<1100<<false;
    QTest::newRow("desktop-touch")<<1100<<true;
    QTest::newRow("mobile-360-mouse")<<360<<false;
    QTest::newRow("mobile-360-touch")<<360<<true;
}
}

class EditScreenUiTests:public QObject {
    Q_OBJECT
private slots:
    void screenEntryPointsAreInvokable()
    {
        // This compiles against the pre-migration header and produces an
        // intentional red test before the new QML-facing API exists.
        const auto& meta=EditorController::staticMetaObject;
        for(const char* signature:{"geometryAddPointScreen(double,double,double,QString)",
                                   "geometrySelectNearestScreen(double,double,double)",
                                   "geometryInsertNearestScreen(double,double,double)",
                                   "geometryMoveSelectedVertexScreen(double,double,double,QString)",
                                   "geometryHoverSnapScreen(double,double,QString)",
                                   "geometryPickTerritorySelectionScreen(double,double)",
                                   "geometryTranslateObjectScreen(double,double)"})
            QVERIFY2(meta.indexOfMethod(signature)>=0,signature);
    }

    void doubleTapUsesCurrentCamera_data(){pointerRows();}
    void doubleTapUsesCurrentCamera()
    {
        QFETCH(int,width);QFETCH(bool,touch);Ui ui(width);
        QVERIFY2(ui.start(),qPrintable(ui.warnings.join('\n')));
        const auto original=ui.editor.geometryDraftPaths();
        QVERIFY(ui.changeCamera());QCOMPARE(ui.editor.geometryDraftPaths(),original);
        const QPoint middle=(ui.atVertex(0)+ui.atVertex(1))/2;
        const auto input=ui.overlayAt(middle);const auto a=ui.projection.project({41,60});
        const auto b=ui.projection.project({43,60});
        const double dx=b.x-a.x,dy=b.y-a.y;
        const double t=std::clamp(((input.x-a.x)*dx+(input.y-a.y)*dy)/(dx*dx+dy*dy),0.,1.);
        const auto expected=ui.projection.project({41.+(43.-41.)*t,60.+(60.-60.)*t});
        // Separate default QTest mouse releases advance its fake clock by
        // 500 ms; mouseDClick supplies an actual double-click sequence.
        if(touch){ui.tap(middle,true);ui.tap(middle,true);}
        else {QTest::mouseDClick(ui.window,Qt::LeftButton,Qt::NoModifier,middle);QTest::qWait(40);}
        QTRY_COMPARE(ui.vertices().size(),3);
        const auto inserted=ui.vertices().at(1).toMap();
        QCOMPARE(inserted["x"].toDouble(),expected.x);
        QCOMPARE(inserted["y"].toDouble(),expected.y);
        QCOMPARE(ui.editor.geometryEditState()["selectedVertex"].toInt(),1);
        QCOMPARE(ui.editor.documentBytes(),ui.before);QVERIFY(!ui.editor.canUndo());
        const auto added=ui.editor.geometryDraftPaths();
        QVERIFY(ui.editor.geometryUndoDraft());QCOMPARE(ui.editor.geometryDraftPaths(),original);
        QVERIFY(ui.editor.geometryRedoDraft());QCOMPARE(ui.editor.geometryDraftPaths(),added);
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

    void vertexDragUsesEventTimeCamera_data(){pointerRows();}
    void vertexDragUsesEventTimeCamera()
    {
        QFETCH(int,width);QFETCH(bool,touch);Ui ui(width);
        QVERIFY2(ui.start(),qPrintable(ui.warnings.join('\n')));
        const auto original=ui.editor.geometryDraftPaths();const auto start=ui.atVertex(0);
        ui.tap(start,touch);QCOMPARE(ui.editor.geometryEditState()["selectedVertex"].toInt(),0);
        ui.press(start,touch);ui.move(start+QPoint(24,25),touch);
        // Touch delivery can wait for a frame. Activate on the first move so
        // the next sample tests movement rather than becoming the grab sample.
        QTRY_VERIFY(ui.activeDrag());ui.move(start+QPoint(34,35),touch);
        QVERIFY(ui.activeDrag());QTRY_VERIFY(ui.editor.geometryDraftPaths()!=original);
        const auto beforeCamera=ui.editor.geometryDraftPaths();
        QVERIFY(ui.changeCamera());QCOMPARE(ui.editor.geometryDraftPaths(),beforeCamera);
        const QPoint destination=start+QPoint(54,62);
        const auto input=ui.overlayAt(destination);
        const auto expected=ui.projection.project(ui.projection.unproject(input.x,input.y));
        ui.move(destination,touch);
        QTRY_COMPARE(ui.vertices().front().toMap()["x"].toDouble(),expected.x);
        QTRY_COMPARE(ui.vertices().front().toMap()["y"].toDouble(),expected.y);
        ui.release(destination,touch);
        const auto moved=ui.vertices().front().toMap();
        QCOMPARE(moved["x"].toDouble(),expected.x);QCOMPARE(moved["y"].toDouble(),expected.y);
        QCOMPARE(ui.vertices().back(),original.front().toMap()["vertices"].toList().back());
        QCOMPARE(ui.editor.documentBytes(),ui.before);QVERIFY(!ui.editor.canUndo());
        const auto changed=ui.editor.geometryDraftPaths();
        QVERIFY(ui.editor.geometryUndoDraft());QCOMPARE(ui.editor.geometryDraftPaths(),original);
        QVERIFY(!ui.editor.geometryEditState()["canUndo"].toBool());
        QVERIFY(ui.editor.geometryRedoDraft());QCOMPARE(ui.editor.geometryDraftPaths(),changed);
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

    void objectDragUsesCurrentScaleAndOriginDraft_data(){pointerRows();}
    void objectDragUsesCurrentScaleAndOriginDraft()
    {
        QFETCH(int,width);QFETCH(bool,touch);Ui ui(width);
        QVERIFY2(ui.start(),qPrintable(ui.warnings.join('\n')));
        QVERIFY(ui.editor.geometrySetMoveMode(true));
        const auto original=ui.editor.geometryDraftPaths();const auto vertices=ui.vertices();
        const QPoint start=(ui.atVertex(0)+ui.atVertex(1))/2;
        ui.press(start,touch);ui.move(start+QPoint(23,24),touch);
        QTRY_VERIFY(ui.activeDrag());ui.move(start+QPoint(33,34),touch);
        QVERIFY(ui.activeDrag());QTRY_VERIFY(ui.editor.geometryDraftPaths()!=original);
        QVERIFY(ui.changeCamera());
        QVERIFY(ui.activeDrag());
        const auto previousTranslation=ui.activeDrag()->property("activeTranslation").value<QVector2D>();
        const QPoint destination=start+QPoint(47,58);ui.move(destination,touch);
        QTRY_VERIFY(ui.activeDrag()&&ui.activeDrag()->property("activeTranslation").value<QVector2D>()!=previousTranslation);
        auto* drag=ui.activeDrag();QVERIFY(drag);
        const auto delta=drag->property("activeTranslation").value<QVector2D>();
        QVERIFY(!delta.isNull());
        const double scale=ui.editor.mapViewState()["mapScale"].toDouble();
        // Preserve the legacy inverse-origin difference, including its order.
        const auto origin=ui.projection.unproject(0,0);
        const auto translated=ui.projection.unproject(delta.x()/scale,delta.y()/scale);
        const double dx=translated.x-origin.x,dy=translated.y-origin.y;
        ui.release(destination,touch);
        for(int i=0;i<vertices.size();++i){
            const auto expected=ui.projection.project({i==0?41.+dx:43.+dx,60.+dy});
            const auto moved=ui.vertices().at(i).toMap();
            QCOMPARE(moved["x"].toDouble(),expected.x);QCOMPARE(moved["y"].toDouble(),expected.y);
        }
        QCOMPARE(ui.editor.documentBytes(),ui.before);QVERIFY(!ui.editor.canUndo());
        const auto changed=ui.editor.geometryDraftPaths();
        QVERIFY(ui.editor.geometryUndoDraft());QCOMPARE(ui.editor.geometryDraftPaths(),original);
        QVERIFY(!ui.editor.geometryEditState()["canUndo"].toBool());
        // Returning a real object drag to zero keeps its existing no-op Undo
        // policy and preserves the earlier Redo entry.
        const QPoint noOp=ui.map->mapToScene(QPointF(180,160)).toPoint();
        ui.press(noOp,touch);ui.move(noOp+QPoint(23,24),touch);
        QTRY_VERIFY(ui.activeDrag());ui.move(noOp+QPoint(35,38),touch);
        QTRY_VERIFY(ui.activeDrag()&&!ui.activeDrag()->property("activeTranslation").value<QVector2D>().isNull());
        QVERIFY(ui.activeDrag());ui.move(noOp,touch);QVERIFY(ui.activeDrag());
        QTRY_VERIFY(ui.activeDrag()&&ui.activeDrag()->property("activeTranslation").value<QVector2D>().isNull());
        QVERIFY(ui.activeDrag()->property("activeTranslation").value<QVector2D>().isNull());
        ui.release(noOp,touch);QCOMPARE(ui.editor.geometryDraftPaths(),original);
        QVERIFY(!ui.editor.geometryEditState()["canUndo"].toBool());
        QVERIFY(ui.editor.geometryRedoDraft());QCOMPARE(ui.editor.geometryDraftPaths(),changed);
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

    void noMotionAndActiveToolReplacement_data(){pointerRows();}
    void noMotionAndActiveToolReplacement()
    {
        QFETCH(int,width);QFETCH(bool,touch);Ui ui(width);
        QVERIFY2(ui.start(),qPrintable(ui.warnings.join('\n')));
        const auto original=ui.editor.geometryDraftPaths();const auto start=ui.atVertex(0);
        ui.tap(start,touch);QCOMPARE(ui.editor.geometryEditState()["selectedVertex"].toInt(),0);
        // A separated stationary press is a tap, not an insertion or drag.
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval()+30);
        ui.press(start,touch);ui.release(start,touch);
        QCOMPARE(ui.editor.geometryDraftPaths(),original);
        QVERIFY(!ui.editor.geometryEditState()["canUndo"].toBool());
        ui.press(start,touch);ui.move(start+QPoint(25,25),touch);ui.move(start+QPoint(45,45),touch);
        QVERIFY(ui.activeDrag());QVERIFY(ui.editor.geometryDraftPaths()!=original);
        // Cancel while Qt still owns the pointer, replace the tool, then deliver
        // the old release. It must not alter the replacement session.
        ui.editor.cancelContentEdit();QVERIFY(!ui.editor.geometryEditState()["active"].toBool());
        QVERIFY(ui.editRiver());QVERIFY(ui.editor.geometrySetMoveMode(true));
        QCOMPARE(ui.editor.geometryDraftPaths(),original);
        ui.release(start+QPoint(45,45),touch);
        QCOMPARE(ui.editor.geometryDraftPaths(),original);
        QCOMPARE(ui.editor.geometryEditState()["tool"].toString(),QString("move"));
        QVERIFY(!ui.editor.geometryEditState()["canUndo"].toBool());
        ui.tap((ui.atVertex(0)+ui.atVertex(1))/2,touch);
        QCOMPARE(ui.editor.geometryDraftPaths(),original);
        QVERIFY(!ui.editor.geometryEditState()["canUndo"].toBool());
        QCOMPARE(ui.editor.documentBytes(),ui.before);QVERIFY(!ui.editor.canUndo());
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

    void twoFingerTakeoverOnlyMovesCamera_data()
    {
        QTest::addColumn<int>("width");QTest::addColumn<bool>("activeFirst");
        QTest::newRow("desktop-before-drag")<<1100<<false;
        QTest::newRow("mobile-360-before-drag")<<360<<false;
        QTest::newRow("desktop-active-drag")<<1100<<true;
        QTest::newRow("mobile-360-active-drag")<<360<<true;
    }
    void twoFingerTakeoverOnlyMovesCamera()
    {
        QFETCH(int,width);QFETCH(bool,activeFirst);Ui ui(width);QVERIFY2(ui.start(),qPrintable(ui.warnings.join('\n')));
        QVERIFY(ui.editor.geometrySetMoveMode(true));
        const auto original=ui.editor.geometryDraftPaths();const auto camera=ui.editor.mapViewState();
        QPoint first=ui.atVertex(0);const QPoint second=ui.atVertex(1);
        QTest::touchEvent(ui.window,Ui::device()).press(0,first,ui.window).commit();
        if(activeFirst){
            ui.move(first+QPoint(24,25),true);QTRY_VERIFY(ui.activeDrag());
            ui.move(first+QPoint(44,45),true);
            QVERIFY(ui.activeDrag());QTRY_VERIFY(ui.editor.geometryDraftPaths()!=original);
            first+=QPoint(44,45);
        }
        const auto beforeTakeover=ui.editor.geometryDraftPaths();
        QTest::touchEvent(ui.window,Ui::device()).stationary(0).press(1,second,ui.window).commit();
        for(int step=1;step<=4;++step){
            const QPoint delta(8*step,6*step);
            QTest::touchEvent(ui.window,Ui::device()).move(0,first+delta,ui.window).move(1,second+delta,ui.window).commit();
            QTest::qWait(25);
        }
        QTest::touchEvent(ui.window,Ui::device()).release(0,first+QPoint(32,24),ui.window).release(1,second+QPoint(32,24),ui.window).commit();
        QTest::qWait(80);
        QTRY_VERIFY(ui.editor.mapViewState()["originX"].toDouble()!=camera["originX"].toDouble());
        // Characterized against the original QML too: an already-active
        // one-finger handler deactivates/commits before the two-finger cancel
        // call. Preserve that draft and its one Undo; do not silently change
        // this pre-existing behavior as part of coordinate extraction.
        QCOMPARE(ui.editor.geometryDraftPaths(),beforeTakeover);
        QCOMPARE(ui.editor.geometryEditState()["canUndo"].toBool(),activeFirst);
        if(activeFirst){QVERIFY(ui.editor.geometryUndoDraft());QCOMPARE(ui.editor.geometryDraftPaths(),original);}
        QCOMPARE(ui.editor.documentBytes(),ui.before);QVERIFY(!ui.editor.canUndo());
        // Single-finger editing must still be usable after the takeover ends.
        // The existing pan can move the line off a narrow viewport. Object
        // dragging accepts any map point, so start the retry visibly on-map.
        const QPoint retry=ui.map->mapToScene(QPointF(180,160)).toPoint();
        QVERIFY(QRect(QPoint(),ui.window->size()).contains(retry+QPoint(39,42)));
        ui.press(retry,true);ui.move(retry+QPoint(22,25),true);QTRY_VERIFY(ui.activeDrag());
        ui.move(retry+QPoint(39,42),true);QTRY_VERIFY(ui.editor.geometryDraftPaths()!=original);
        QVERIFY2(ui.activeDrag(),qPrintable(QString("single-touch retry at %1,%2; map %3x%4")
            .arg(retry.x()).arg(retry.y()).arg(ui.map->width()).arg(ui.map->height())));
        ui.release(retry+QPoint(39,42),true);
        QVERIFY(ui.editor.geometryDraftPaths()!=original);
        QVERIFY(ui.editor.geometryUndoDraft());QCOMPARE(ui.editor.geometryDraftPaths(),original);
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }
};

int main(int argc,char** argv)
{
    QQuickStyle::setStyle("Basic");QGuiApplication app(argc,argv);registerWindowsFrameType();
    EditScreenUiTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "edit_screen_ui_tests.moc"
