#include "editorcontroller.h"
#include "referenceimagelibrary.h"
#include "ui_navigation.h"
#include "windowsframe.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickStyle>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVector2D>
#include <cmath>

using namespace pandoeditor;

namespace {
ProjectDocument fixture(double longitude=40.,double latitude=59.)
{
    return ProjectDocument({{"A","A",{{{{longitude,latitude},{longitude+4,latitude},
        {longitude+4,latitude+4},{longitude,latitude+4},{longitude,latitude}}}},0xabcdef}},
        {{"countries","Countries"}});
}

QVariantMap modelData(QObject* object)
{
    const auto value=object->property("modelData");
    return value.metaType()==QMetaType::fromType<QJSValue>()?
        value.value<QJSValue>().toVariant().toMap():value.toMap();
}

QObject* childWithProperty(QObject* object,const char* property)
{
    if(object->property(property).isValid())return object;
    for(auto* child:object->children())if(auto* found=childWithProperty(child,property))return found;
    return nullptr;
}

QObject* dragHandler(QQuickItem* item)
{
    for(auto* child:item->children())
        if(QByteArray(child->metaObject()->className()).contains("DragHandler"))return child;
    return nullptr;
}

// Independent old expressions are intentional. Calling the migrated adapters
// to calculate expectations would conceal stale camera/projection bindings.
QPointF screenPoint(double x,double y,const QVariantMap& camera)
{
    return {camera["originX"].toDouble()+x*camera["mapScale"].toDouble(),
            camera["originY"].toDouble()+y*camera["mapScale"].toDouble()};
}
QRectF screenRect(const QVariantMap& image,const QVariantMap& camera)
{
    return {screenPoint(image["x"].toDouble(),image["y"].toDouble(),camera),
            QSizeF(image["width"].toDouble()*camera["mapScale"].toDouble(),
                   image["height"].toDouble()*camera["mapScale"].toDouble())};
}
QRectF itemRect(QQuickItem* item,QQuickItem* map)
{
    return {item->mapToItem(map,QPointF()),QSizeF(item->width(),item->height())};
}

struct Ui {
    QTemporaryDir files;
    QStringList warnings;
    EditorController editor;
    QQmlApplicationEngine engine;
    QQuickWindow* window=nullptr;
    QQuickItem* map=nullptr;
    int width;
    explicit Ui(int w):editor([&]{EditorControllerConfig config;config.mobileMode=w==360;
        config.bootstrapWorld=false;config.autosaveEnabled=false;
        config.privateProjectPath=files.filePath("private.json");return config;}()),width(w)
    {
        QObject::connect(&engine,&QQmlEngine::warnings,&engine,[this](const auto& errors){
            for(const auto& error:errors)warnings.append(error.toString());
        });
    }
    bool open(const ProjectDocument& document,const QString& name="input.json")
    {
        Project project;project.replace(document);QFile file(files.filePath(name));
        if(!file.open(QIODevice::WriteOnly)||file.write(projectcodec::encode(project))<=0)return false;
        file.close();return editor.openFile(QUrl::fromLocalFile(file.fileName()));
    }
    bool start(bool referenceComponent=false)
    {
        if(!open(fixture())||!editor.setProjectionMode("flat"))return false;
        engine.rootContext()->setContextProperty("editor",&editor);
        const auto root=qEnvironmentVariable("M974_TEST_UI_ROOT");
        if(referenceComponent) {
            // Keep a narrow first-event adapter check using the exact production
            // delegates. The full-Main tests below also exercise the real menus.
            QFile source(root.isEmpty()?QString(":/common/MapView.qml"):root+"/common/MapView.qml");
            if(!source.open(QIODevice::ReadOnly))return false;
            const auto qml=source.readAll();
            const int first=qml.indexOf("    ReferenceImageLibrary {");
            const int last=qml.indexOf("    GpuMapItem {",first);
            if(first<0||last<=first)return false;
            const auto component=QByteArray(R"(
import QtQuick
import QtQuick.Window
import Pandoeditor.Windowing 1.0
Window {
    Item {
        id: view
        objectName: "mapView"
        anchors.fill: parent
        readonly property var cameraState: editor.mapViewState
        readonly property real mapScale: cameraState.mapScale
        readonly property real originX: cameraState.originX
        readonly property real originY: cameraState.originY
        readonly property bool globeMode: editor.projectionMode === "globe"
        readonly property bool geometryEditing: editor.geometryEditState.active === true
        function syncViewport() { if(width>0 && height>0) editor.resizeMapCamera(width,height) }
        onWidthChanged: syncViewport()
        onHeightChanged: syncViewport()
        Component.onCompleted: syncViewport()
)")+qml.mid(first,last-first)+"\n    }\n}\n";
            engine.loadData(component,QUrl("qrc:/common/EditReferenceComponentTest.qml"));
        } else engine.load(root.isEmpty()?QUrl("qrc:/common/Main.qml"):QUrl::fromLocalFile(root+"/common/Main.qml"));
        if(engine.rootObjects().isEmpty())return false;
        window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());if(!window)return false;
        window->resize(width,760);window->show();QTest::qWait(180);
        map=navigationItem(window->contentItem(),"mapView");if(!map)return false;
        QTest::mouseMove(window,QPoint(-100,-100));window->grabWindow();return true;
    }
    bool changeCamera()
    {
        editor.beginMapCameraPan();const bool panned=editor.updateMapCameraPan(19.,-11.);editor.endMapCameraPan();
        const bool zoomed=editor.zoomMapCameraAt(1.23,140.,170.);
        window->resize(width+29,789);QTest::qWait(40);
        window->resize(width,760);QTest::qWait(40);window->grabWindow();return panned&&zoomed;
    }
    QVariant evaluate(QObject* scope,const QString& expression)
    {
        QQmlExpression call(QQmlEngine::contextForObject(scope),scope,expression);
        const auto result=call.evaluate();if(call.hasError())warnings.append(call.error().toString());
        return result.metaType()==QMetaType::fromType<QJSValue>()?result.value<QJSValue>().toVariant():result;
    }
    ReferenceImageLibrary* library() const {return window->findChild<ReferenceImageLibrary*>();}
    QString addImage(bool edge=false)
    {
        auto* images=library();if(!images)return {};
        for(const auto& row:images->images()) {
            const auto id=row.toMap()["id"].toString();
            images->updateImage(id,{{"locked",false}});if(!images->removeImage(id))return {};
        }
        QImage image(edge?64:8,edge?64:6,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::red);
        if(edge)for(int y=0;y<64;y++)for(int x=0;x<64;x++)image.setPixelColor(x,y,x>=32&&y<54?Qt::white:Qt::black);
        const auto path=files.filePath("reference.png");
        if(!image.save(path)||!images->importImage(QUrl::fromLocalFile(path),"coordinate fixture"))return {};
        const auto id=images->images().front().toMap()["id"].toString();
        const auto before=editor.mapViewState();
        if(!editor.zoomMapCameraAt(15./before["mapScale"].toDouble(),140,170))return {};
        const auto camera=editor.mapViewState();
        if(!images->updateImage(id,{{"x",(95.-camera["originX"].toDouble())/camera["mapScale"].toDouble()},
            {"y",(145.-camera["originY"].toDouble())/camera["mapScale"].toDouble()},
            {"width",120./camera["mapScale"].toDouble()},
            {"height",90./camera["mapScale"].toDouble()}}))return {};
        QTest::qWait(60);window->grabWindow();return id;
    }
    QQuickItem* reference(bool globe=false) const
    {return navigationItem(map,globe?"referenceImageGeographicOverlay":"referenceImageOverlay");}
    void stop()
    {
        if(window) {
            QTest::mouseMove(window,QPoint(-100,-100));window->setProperty("allowClose",true);
            window->close();delete window;window=nullptr;map=nullptr;
        }
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
    ~Ui(){stop();}
};

void widths()
{
    QTest::addColumn<int>("width");QTest::newRow("desktop-1100")<<1100;QTest::newRow("mobile-360")<<360;
}
}

class EditDisplayUiTests:public QObject {
    Q_OBJECT
private slots:
    void init(){QTest::failOnWarning();}
    void liveWireActualWindowToDraftFlow()
    {
        Ui ui(1100);QVERIFY2(ui.start(),qPrintable(ui.warnings.join('\n')));const auto id=ui.addImage(true);QVERIFY(!id.isEmpty());
        ui.editor.selectCountry("A");QVERIFY(ui.editor.beginGeometryDraw());const auto before=ui.editor.referenceDraftCoordinates();const auto document=ui.editor.documentBytes();
        auto* menu=ui.window->findChild<QObject*>("referenceImageMenu");auto* imageMenu=qvariant_cast<QObject*>(ui.evaluate(menu,"menuAt(4)"));QVERIFY(imageMenu);ui.evaluate(imageMenu,"itemAt(8).triggered()");QTest::qWait(50);
        auto* library=ui.library();QVERIFY(navigationClick(ui.window,"referenceFreeTransformButton"));QVERIFY(navigationClick(ui.window,"referenceTraceStart"));QVERIFY(library->traceSession()["active"].toBool());
        const auto click=[&](QPointF p){QTest::mouseClick(ui.window,Qt::LeftButton,Qt::NoModifier,ui.map->mapToScene(p).toPoint());};
        click({155,160});QTRY_VERIFY(!library->traceSession()["busy"].toBool());QCOMPARE(library->traceSession()["anchors"].toList().size(),1);
        click({155,221});QTRY_VERIFY(!library->traceSession()["busy"].toBool());QCOMPARE(library->traceSession()["anchors"].toList().size(),2);
        QVERIFY(navigationClick(ui.window,"referenceTraceUndo"));QTRY_VERIFY(!library->traceSession()["busy"].toBool());QCOMPARE(library->traceSession()["anchors"].toList().size(),1);
        click({155,221});QTRY_VERIFY(!library->traceSession()["busy"].toBool());click({195,221});QTRY_VERIFY(!library->traceSession()["busy"].toBool());
        QVERIFY2(library->traceSession()["error"].toString().isEmpty(),qPrintable(library->traceSession()["error"].toString()));
        QVERIFY(navigationClick(ui.window,"referenceTraceFinish"));QCOMPARE(library->traceSession()["phase"].toString(),QString("preview"));QVERIFY(!ui.window->grabWindow().isNull());
        const auto coordinates=library->traceSession()["coordinates"].toList();QVERIFY(coordinates.size()>=3);
        QVERIFY(navigationClick(ui.window,"referenceTraceApply"));QVERIFY(library->traceSession().isEmpty());QVERIFY(ui.editor.referenceDraftCoordinates()!=before);QCOMPARE(ui.editor.documentBytes(),document);
        QVERIFY(ui.editor.geometryUndoDraft());QCOMPARE(ui.editor.referenceDraftCoordinates(),before);QVERIFY(ui.editor.geometryRedoDraft());
        const auto old=ui.editor.referenceDraftContext();QVERIFY(ui.editor.geometryUndoDraft());QVERIFY(!ui.editor.replaceReferenceDraft(coordinates,old));QCOMPARE(ui.editor.referenceDraftCoordinates(),before);
        QVERIFY(navigationClick(ui.window,"referenceTraceStart"));click({155,160});library->cancelTrace();QTest::qWait(80);QVERIFY(library->traceSession().isEmpty());QCOMPARE(ui.editor.documentBytes(),document);
        QVERIFY(ui.editor.geometryRedoDraft());auto* panel=ui.window->findChild<QObject*>("referenceCalibrationPanel");ui.evaluate(panel,"close()");QTest::qWait(50);
        QVERIFY(navigationClick(ui.window,"geometryPreview"));QTRY_VERIFY(ui.editor.geometryEditState()["previewReady"].toBool());QVERIFY(navigationClick(ui.window,"geometryConfirm"));QTRY_VERIFY(!ui.editor.geometryEditState()["active"].toBool());QVERIFY(ui.editor.documentBytes()!=document);QVERIFY(ui.editor.canUndo());ui.editor.undo();QCOMPARE(ui.editor.documentBytes(),document);
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

    void lineRefinementActualWindowPreservesDraftUntilApply()
    {
        Ui ui(1100);QVERIFY2(ui.start(),qPrintable(ui.warnings.join('\n')));
        auto document=fixture();Geometry line;line.type="LineString";line.lines={{{41,60},{43,60}}};const GeometryRef geometry{"river-geometry",1};document.geometries.insert(geometry,line);HydroFeature river;river.id="river";river.name="River";river.geometry=geometry;document.hydro.push_back(river);QVERIFY(ui.open(document));
        const auto id=ui.addImage(true);QVERIFY(!id.isEmpty());QVERIFY(ui.editor.selectObject({{"domain","hydro"},{"id","river"}},"replace","test"));QVERIFY(ui.editor.beginContentEdit("hydro",QString(),false));QVERIFY(ui.editor.beginContentGeometry());
        QVariantList rough{ui.editor.referenceCoordinateAtScreen(149,160),ui.editor.referenceCoordinateAtScreen(149,215)};QVERIFY(ui.editor.replaceReferenceDraft(rough,ui.editor.referenceDraftContext()));const auto before=ui.editor.referenceDraftCoordinates();const auto bytes=ui.editor.documentBytes();
        auto* menu=ui.window->findChild<QObject*>("referenceImageMenu");auto* imageMenu=qvariant_cast<QObject*>(ui.evaluate(menu,"menuAt(4)"));QVERIFY(imageMenu);ui.evaluate(imageMenu,"itemAt(8).triggered()");QTest::qWait(50);QVERIFY(navigationClick(ui.window,"referenceFreeTransformButton"));
        auto* library=ui.library();QVERIFY(navigationClick(ui.window,"referenceRefineStart"));QTRY_VERIFY(!library->traceSession()["busy"].toBool());QCOMPARE(library->traceSession()["phase"].toString(),QString("preview"));QCOMPARE(ui.editor.referenceDraftCoordinates(),before);QVERIFY(!ui.window->grabWindow().isNull());
        QVERIFY(navigationClick(ui.window,"referenceTraceCancel"));QCOMPARE(ui.editor.referenceDraftCoordinates(),before);
        QVERIFY(navigationClick(ui.window,"referenceRefineStart"));QTRY_VERIFY(!library->traceSession()["busy"].toBool());const auto preview=library->traceSession()["coordinates"].toList();QVERIFY(preview.size()>=2);QVERIFY(navigationClick(ui.window,"referenceTraceApply"));QVERIFY(ui.editor.referenceDraftCoordinates()!=before);QCOMPARE(ui.editor.documentBytes(),bytes);
        QVERIFY(ui.editor.geometryUndoDraft());QCOMPARE(ui.editor.referenceDraftCoordinates(),before);QVERIFY(ui.editor.geometryRedoDraft());
        QVERIFY(navigationClick(ui.window,"referenceRefineStart"));QVERIFY(!ui.open(fixture(),"replacement.json"));QCOMPARE(ui.editor.documentBytes(),bytes);ui.editor.cancelContentEdit();QVERIFY(ui.open(fixture(),"replacement.json"));QTest::qWait(100);QVERIFY(library->traceSession().isEmpty());
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

    void geographicCalibrationActualWindowFlow()
    {
        Ui ui(1100);QVERIFY2(ui.start(),qPrintable(ui.warnings.join('\n')));
        const auto id=ui.addImage();QVERIFY(!id.isEmpty());
        auto* menu=ui.window->findChild<QObject*>("referenceImageMenu");QVERIFY(menu);
        auto* imageMenu=qvariant_cast<QObject*>(ui.evaluate(menu,"menuAt(4)"));QVERIFY(imageMenu);
        ui.evaluate(imageMenu,"itemAt(8).triggered()");QTest::qWait(50);
        auto* library=ui.library();QVERIFY2(library->calibrationSession()["active"].toBool(),qPrintable(ui.evaluate(imageMenu,"itemAt(8).text").toString()));
        auto* panel=ui.window->findChild<QObject*>("referenceCalibrationPanel");QVERIFY(panel);QVERIFY(panel->property("visible").toBool());
        auto* source=ui.window->findChild<QQuickItem*>("referenceCalibrationSource");QVERIFY(source);
        const auto sourcePoint=source->mapToScene(QPointF(source->width()/2,source->height()/2));
        QTest::mouseClick(ui.window,Qt::LeftButton,Qt::NoModifier,sourcePoint.toPoint());
        QVERIFY(library->calibrationSession()["pendingMap"].toBool());
        const QPointF click(300,300);const auto expected=ui.editor.referenceCoordinateAtScreen(click.x(),click.y());QCOMPARE(expected.size(),2);
        QTest::mouseClick(ui.window,Qt::LeftButton,Qt::NoModifier,ui.map->mapToScene(click).toPoint());
        const auto points=library->images().front().toMap()["geographicPoints"].toList();QCOMPARE(points.size(),1);
        const auto uv=points[0].toMap()["image"].toList();QVERIFY(std::abs(uv[0].toDouble()-.5)<.01);QVERIFY(std::abs(uv[1].toDouble()-.5)<.01);
        const auto geographic=points[0].toMap()["coordinate"].toList();QVERIFY(std::abs(geographic[0].toDouble()-expected[0].toDouble())<1e-8);
        QVERIFY(library->undo());QVERIFY(library->images().front().toMap()["geographicPoints"].toList().empty());
        QVERIFY(library->redo());QCOMPARE(library->images().front().toMap()["geographicPoints"].toList(),points);
        QVERIFY(library->clearCalibrationPoints());
        const auto place=[&](double u,double v,QPointF mapPoint) {
            const double w=source->property("paintedWidth").toDouble(),h=source->property("paintedHeight").toDouble();
            const QPointF local((source->width()-w)/2+u*w,(source->height()-h)/2+v*h);
            QTest::mouseClick(ui.window,Qt::LeftButton,Qt::NoModifier,source->mapToScene(local).toPoint());
            QTest::mouseClick(ui.window,Qt::LeftButton,Qt::NoModifier,ui.map->mapToScene(mapPoint).toPoint());
        };
        place(.1,.1,{280,280});place(.9,.1,{400,280});place(.1,.9,{280,400});
        QCOMPARE(library->images().front().toMap()["geographicPoints"].toList().size(),3);
        QVERIFY(library->calibrationSession()["result"].toMap()["ok"].toBool());
        QTest::qWait(100);const auto capture=ui.window->grabWindow();QVERIFY2(!capture.isNull(),"Actual window must present");
        const auto center=ui.map->mapToScene(QPointF(340,340));const double dpr=double(capture.width())/ui.window->width();
        const auto pixel=capture.pixelColor(qRound(center.x()*dpr),qRound(center.y()*dpr));
        QVERIFY2(pixel.red()>200&&pixel.green()<60&&pixel.blue()<60,qPrintable(pixel.name()));
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

    void cornerPinAndAnchorActualWindowFlow()
    {
        Ui ui(1100);QVERIFY2(ui.start(),qPrintable(ui.warnings.join('\n')));const auto id=ui.addImage();QVERIFY(!id.isEmpty());
        auto* menu=ui.window->findChild<QObject*>("referenceImageMenu");QVERIFY(menu);auto* imageMenu=qvariant_cast<QObject*>(ui.evaluate(menu,"menuAt(4)"));QVERIFY(imageMenu);
        ui.evaluate(imageMenu,"itemAt(8).triggered()");QTest::qWait(50);
        QVERIFY(navigationClick(ui.window,"referenceFreeTransformButton"));
        auto* library=ui.library();QVERIFY(library->calibrationSession()["freeTransformEditing"].toBool());
        auto* handle=navigationItem(ui.window->contentItem(),"referenceCornerHandle0");QVERIFY(handle);
        const auto before=library->images().front().toMap()["mapQuad"];
        const auto start=handle->mapToScene(handle->boundingRect().center()).toPoint();
        QTest::mousePress(ui.window,Qt::LeftButton,Qt::NoModifier,start);QTest::mouseMove(ui.window,start+QPoint(10,8),30);QTest::mouseRelease(ui.window,Qt::LeftButton,Qt::NoModifier,start+QPoint(10,8));
        const auto changed=library->images().front().toMap()["mapQuad"];QVERIFY(changed!=before);
        QVERIFY(library->undo());QCOMPARE(library->images().front().toMap()["mapQuad"],before);QVERIFY(library->redo());QCOMPARE(library->images().front().toMap()["mapQuad"],changed);
        QVERIFY(navigationClick(ui.window,"referenceAnchorButton"));
        auto* source=ui.window->findChild<QQuickItem*>("referenceCalibrationSource");QVERIFY(source);
        QTest::mouseClick(ui.window,Qt::LeftButton,Qt::NoModifier,source->mapToScene({source->width()/2,source->height()/2}).toPoint());
        QTest::mouseClick(ui.window,Qt::LeftButton,Qt::NoModifier,ui.map->mapToScene({160,190}).toPoint());
        QVERIFY(!library->images().front().toMap()["anchor"].toMap().isEmpty());
        const auto diagnostics=library->calibrationSession()["result"].toMap()["diagnostics"].toMap();QVERIFY(diagnostics["hardMaxMeters"].toDouble()<=.01);
        QVERIFY(ui.editor.setProjectionMode("globe"));QTest::qWait(80);QVERIFY(!ui.window->grabWindow().isNull());
        QVERIFY(!ui.reference(true)->property("calibrationMesh").toMap().isEmpty());
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

    void displayAdaptersAreInvokable()
    {
        const auto& meta=EditorController::staticMetaObject;
        for(const char* signature:{"editMapPointToScreen(double,double,QVariantMap)",
            "editPixelLengthToMap(double,QVariantMap)",
            "editLabelDragToMap(double,double,double,double,QVariantMap)",
            "editMapRectToScreen(QRectF,QVariantMap)",
            "editMapDragPosition(double,double,double,double,QVariantMap)",
            "editMapRectGeographicBounds(QRectF,QVariantMap)"})
            QVERIFY2(meta.indexOfMethod(signature)>=0,signature);
    }

    void cameraOnlyChangesMoveVerticesAndSnapMarker_data(){widths();}
    void cameraOnlyChangesMoveVerticesAndSnapMarker()
    {
        QFETCH(int,width);Ui ui(width);QVERIFY2(ui.start(),qPrintable(ui.warnings.join('\n')));
        ui.editor.selectCountry("A");QVERIFY(ui.editor.beginGeometryDraw());
        MapProjection projection;projection.rebuild(fixture());const auto edge=projection.project({40.,60.});
        const auto initial=ui.editor.mapViewState();
        QVERIFY(ui.editor.zoomMapCameraAt(80./initial["mapScale"].toDouble(),140,170));
        const auto camera=ui.editor.mapViewState();const auto edgeScreen=screenPoint(edge.x,edge.y,camera);
        ui.editor.beginMapCameraPan();QVERIFY(ui.editor.updateMapCameraPan(140-edgeScreen.x(),170-edgeScreen.y()));ui.editor.endMapCameraPan();
        QVERIFY(ui.editor.geometryAddPoint(edge.x,edge.y,10./80.,"mouse"));
        QTRY_COMPARE_WITH_TIMEOUT(ui.editor.geometrySnapState()["status"].toString(),QString("ready"),3000);
        QVERIFY(ui.editor.geometryAddPoint(edge.x,edge.y,10./80.,"mouse"));
        QTRY_VERIFY(!ui.editor.geometrySnapState()["indicator"].toMap().isEmpty());
        QTRY_VERIFY(navigationItem(ui.map,"geometryEditVertex"));
        auto* vertex=navigationItem(ui.map,"geometryEditVertex");
        auto* marker=navigationItem(ui.map,"geometrySnapIndicator");QVERIFY(marker);QVERIFY(marker->isVisible());
        auto* overlay=navigationItem(ui.map,"geometryDraftOverlay");QVERIFY(overlay);
        auto* stroke=childWithProperty(overlay,"strokeWidth");QVERIFY(stroke);
        auto* svg=childWithProperty(overlay,"path");QVERIFY(svg);
        const auto paths=ui.editor.geometryDraftPaths();const auto data=modelData(vertex);
        const auto svgPath=svg->property("path");QVERIFY(!svgPath.toString().isEmpty());
        const auto state=ui.editor.geometryEditState();
        const auto canonical=ui.editor.documentBytes();
        const auto preparations=ui.editor.renderQuality()["scenePreparationCount"];QVERIFY(preparations.isValid());
        const auto firstCenter=vertex->mapToItem(ui.map,{vertex->width()/2,vertex->height()/2});
        const auto firstMarker=marker->mapToItem(ui.map,{marker->width()/2,marker->height()/2});
        const auto beforeCamera=ui.editor.mapViewState();
        QCOMPARE(firstCenter,screenPoint(data["x"].toDouble(),data["y"].toDouble(),beforeCamera));
        QCOMPARE(firstMarker,screenPoint(state["snapX"].toDouble(),state["snapY"].toDouble(),beforeCamera));
        QSignalSpy geometry(&ui.editor,&EditorController::geometryChanged);
        QSignalSpy edits(&ui.editor,&EditorController::geometryEditChanged);
        QSignalSpy views(&ui.editor,&EditorController::viewStateChanged);
        QVERIFY(ui.changeCamera());QVERIFY(views.count()>0);
        const auto changed=ui.editor.mapViewState();
        const auto expectedVertex=screenPoint(data["x"].toDouble(),data["y"].toDouble(),changed);
        const auto expectedMarker=screenPoint(state["snapX"].toDouble(),state["snapY"].toDouble(),changed);
        QVERIFY(expectedVertex!=firstCenter);QVERIFY(expectedMarker!=firstMarker);
        QTRY_COMPARE(vertex->mapToItem(ui.map,QPointF(vertex->width()/2,vertex->height()/2)),expectedVertex);
        QTRY_COMPARE(marker->mapToItem(ui.map,QPointF(marker->width()/2,marker->height()/2)),expectedMarker);
        QCOMPARE(vertex->width(),10.);QCOMPARE(marker->width(),14.);QVERIFY(marker->isVisible());
        QCOMPARE(stroke->property("strokeWidth").toDouble(),2./changed["mapScale"].toDouble());
        QCOMPARE(svg->property("path"),svgPath);QCOMPARE(modelData(vertex),data);
        QCOMPARE(ui.editor.geometryDraftPaths(),paths);QCOMPARE(ui.editor.geometryEditState(),state);
        QCOMPARE(ui.editor.documentBytes(),canonical);QCOMPARE(ui.editor.renderQuality()["scenePreparationCount"],preparations);
        QCOMPARE(geometry.count(),0);QCOMPARE(edits.count(),0);
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

    void referenceRectTracksCameraWithoutChangingRecords_data(){widths();}
    void referenceRectTracksCameraWithoutChangingRecords()
    {
        QFETCH(int,width);Ui ui(width);QVERIFY2(ui.start(),qPrintable(ui.warnings.join('\n')));
        QVERIFY(!ui.addImage().isEmpty());auto* image=ui.reference();QVERIFY(image);QVERIFY(image->isVisible());
        const auto records=ui.library()->images();const auto data=records.front().toMap();
        QCOMPARE(itemRect(image,ui.map),screenRect(data,ui.editor.mapViewState()));
        const auto oldRect=itemRect(image,ui.map);const auto canonical=ui.editor.documentBytes();
        const auto preparations=ui.editor.renderQuality()["scenePreparationCount"];
        QSignalSpy geometry(&ui.editor,&EditorController::geometryChanged);
        QSignalSpy edits(&ui.editor,&EditorController::geometryEditChanged);
        QSignalSpy images(ui.library(),&ReferenceImageLibrary::imagesChanged);
        QVERIFY(ui.changeCamera());
        const auto expected=screenRect(data,ui.editor.mapViewState());QVERIFY(expected!=oldRect);
        QTRY_COMPARE(itemRect(image,ui.map),expected);
        QCOMPARE(ui.library()->images(),records);QCOMPARE(images.count(),0);
        QCOMPARE(ui.editor.documentBytes(),canonical);QCOMPARE(ui.editor.renderQuality()["scenePreparationCount"],preparations);
        QCOMPARE(geometry.count(),0);QCOMPARE(edits.count(),0);
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

    void referenceFirstPointerUpdateUsesCurrentCamera_data(){widths();}
    void referenceFirstPointerUpdateUsesCurrentCamera()
    {
        QFETCH(int,width);Ui ui(width);QVERIFY2(ui.start(true),qPrintable(ui.warnings.join('\n')));
        QVERIFY(!ui.addImage().isEmpty());QVERIFY(ui.changeCamera());
        auto* image=ui.reference();QVERIFY(image);QPointer<QObject> drag=dragHandler(image);QVERIFY(drag);
        const auto records=ui.library()->images();const auto data=records.front().toMap();
        const auto canonical=ui.editor.documentBytes();const auto startingCamera=ui.editor.mapViewState();
        const auto position=image->mapToScene(QPointF(image->width()*.35,image->height()*.35)).toPoint();
        QVERIFY(QRect(QPoint(),ui.window->size()).contains(position+QPoint(52,31)));
        QVariantMap eventCamera,eventRecord;QVector2D eventDelta;bool observedLiveHandler=false;int updates=0;
        const auto connection=QObject::connect(ui.library(),&ReferenceImageLibrary::imagesChanged,ui.library(),[&]{
            ++updates;eventCamera=ui.editor.mapViewState();eventRecord=ui.library()->images().front().toMap();
            observedLiveHandler=drag&&!drag->property("activeTranslation").isNull();
            if(drag)eventDelta=drag->property("activeTranslation").value<QVector2D>();
        });
        QTest::mousePress(ui.window,Qt::LeftButton,Qt::NoModifier,position);
        QTest::mouseMove(ui.window,position+QPoint(22,15),20);
        QVERIFY(drag&&drag->property("active").toBool());QCOMPARE(updates,0);
        QVERIFY(ui.editor.zoomMapCameraAt(1.31,140.,170.));
        QTest::mouseMove(ui.window,position+QPoint(52,31),20);
        QCOMPARE(updates,1);QVERIFY(observedLiveHandler);QVERIFY(eventDelta.lengthSquared()>4);
        QVERIFY(eventCamera["mapScale"]!=startingCamera["mapScale"]);
        QCOMPARE(eventRecord["x"].toDouble(),data["x"].toDouble()+eventDelta.x()/eventCamera["mapScale"].toDouble());
        QCOMPARE(eventRecord["y"].toDouble(),data["y"].toDouble()+eventDelta.y()/eventCamera["mapScale"].toDouble());
        QObject::disconnect(connection);
        // Keep cancellation coverage separate from the full-Main real-release
        // lifecycle test below. No direct commit substitutes for pointer release.
        ui.library()->cancelGesture();
        QTest::mouseRelease(ui.window,Qt::LeftButton,Qt::NoModifier,position+QPoint(52,31));
        QCOMPARE(ui.library()->images(),records);
        QCOMPARE(ui.editor.documentBytes(),canonical);QVERIFY(!ui.editor.canUndo());
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

    void referenceDragSurvivesUpdatesAndRelease_data(){widths();}
    void referenceDragSurvivesUpdatesAndRelease()
    {
        QFETCH(int,width);Ui ui(width);QVERIFY2(ui.start(),qPrintable(ui.warnings.join('\n')));
        const auto id=ui.addImage();QVERIFY(!id.isEmpty());auto* library=ui.library();
        QPointer<QQuickItem> image=ui.reference(),geographic=ui.reference(true);QVERIFY(image);QVERIFY(geographic);
        QPointer<QObject> drag=dragHandler(image);QVERIFY(drag);
        QSignalSpy destroyed(drag,&QObject::destroyed);
        QSignalSpy imageDestroyed(image,&QObject::destroyed);
        QSignalSpy history(library,&ReferenceImageLibrary::historyChanged);
        QSignalSpy images(library,&ReferenceImageLibrary::imagesChanged);
        QSignalSpy geometry(&ui.editor,&EditorController::geometryChanged);
        QSignalSpy edits(&ui.editor,&EditorController::geometryEditChanged);
        const auto canonical=ui.editor.documentBytes();
        const auto preparations=ui.editor.renderQuality()["scenePreparationCount"];
        for(int gesture=0;gesture<2;++gesture) {
            const auto before=library->images();const auto data=before.front().toMap();
            const auto startCamera=ui.editor.mapViewState();
            const int historyBefore=history.count(),imagesBefore=images.count();
            const auto position=image->mapToScene(QPointF(image->width()*.25,image->height()*.25)).toPoint();
            const QPoint offsets[]={{42,25},{57,35},{69,43}};
            QVERIFY(QRect(QPoint(),ui.window->size()).contains(position+offsets[2]));
            QTest::mousePress(ui.window,Qt::LeftButton,Qt::NoModifier,position);
            QTest::mouseMove(ui.window,position+QPoint(20,12),20);
            QVERIFY(drag&&drag->property("active").toBool());
            QCOMPARE(ui.editor.mapViewState(),startCamera);
            for(int event=0;event<3;++event) {
                if(event==1) {
                    ui.editor.beginMapCameraPan();QVERIFY(ui.editor.updateMapCameraPan(-8.,7.));ui.editor.endMapCameraPan();
                    QVERIFY(ui.editor.zoomMapCameraAt(1.13,140.,170.));
                }
                const auto expectedCamera=ui.editor.mapViewState();
                QTest::mouseMove(ui.window,position+offsets[event],20);
                QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QTest::qWait(25);
                QVERIFY2(drag&&image,"Reference-image delegate/DragHandler was destroyed during a preview update");
                QCOMPARE(ui.reference(),image.data());QCOMPARE(ui.reference(true),geographic.data());QCOMPARE(dragHandler(image),drag.data());
                QVERIFY(drag->property("active").toBool());QCOMPARE(destroyed.count(),0);QCOMPARE(imageDestroyed.count(),0);
                QCOMPARE(images.count(),imagesBefore+event+1);QCOMPARE(history.count(),historyBefore);
                const auto camera=ui.editor.mapViewState();QCOMPARE(camera,expectedCamera);
                const auto delta=drag->property("activeTranslation").value<QVector2D>();QVERIFY(delta.lengthSquared()>4);
                const auto moved=library->images().front().toMap();
                QCOMPARE(moved["x"].toDouble(),data["x"].toDouble()+delta.x()/camera["mapScale"].toDouble());
                QCOMPARE(moved["y"].toDouble(),data["y"].toDouble()+delta.y()/camera["mapScale"].toDouble());
                QCOMPARE(itemRect(image,ui.map),screenRect(moved,camera));
            }
            QVERIFY(ui.editor.mapViewState()["mapScale"]!=startCamera["mapScale"]);
            const auto finalRecords=library->images();QVERIFY(finalRecords!=before);
            QTest::mouseRelease(ui.window,Qt::LeftButton,Qt::NoModifier,position+offsets[2]);
            QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QTest::qWait(25);
            QVERIFY(drag&&!drag->property("active").toBool());QCOMPARE(history.count(),historyBefore+1);
            QCOMPARE(library->images(),finalRecords);
            QVERIFY(library->undo());QCOMPARE(library->images(),before);
            QVERIFY(library->redo());QCOMPARE(library->images(),finalRecords);
            QCOMPARE(history.count(),historyBefore+3);QCOMPARE(destroyed.count(),0);QCOMPARE(imageDestroyed.count(),0);
            QCOMPARE(ui.reference(),image.data());QCOMPARE(ui.reference(true),geographic.data());QCOMPARE(dragHandler(image),drag.data());
        }
        QCOMPARE(ui.editor.documentBytes(),canonical);QCOMPARE(ui.editor.renderQuality()["scenePreparationCount"],preparations);
        QCOMPARE(geometry.count(),0);QCOMPARE(edits.count(),0);QVERIFY(!ui.editor.canUndo());
        ui.stop();QVERIFY(!drag);QVERIFY(!image);QVERIFY(!geographic);
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

    void referenceMenusKeepActionsOrderAndSafeOwnership_data(){widths();}
    void referenceMenusKeepActionsOrderAndSafeOwnership()
    {
        QFETCH(int,width);Ui ui(width);QVERIFY2(ui.start(),qPrintable(ui.warnings.join('\n')));
        const auto firstId=ui.addImage();QVERIFY(!firstId.isEmpty());auto* library=ui.library();
        auto* menu=ui.window->findChild<QObject*>("referenceImageMenu");QVERIFY(menu);
        QCOMPARE(menu->property("count").toInt(),5);
        QPointer<QObject> firstMenu=qvariant_cast<QObject*>(ui.evaluate(menu,"menuAt(4)"));QVERIFY(firstMenu);
        QCOMPARE(modelData(firstMenu)["id"].toString(),firstId);QCOMPARE(firstMenu->property("count").toInt(),9);
        QCOMPARE(ui.evaluate(firstMenu,"itemAt(8).text").toString(),QString::fromUtf8("기준점 보정…"));
        QCOMPARE(ui.evaluate(menu,"itemAt(0).text").toString(),QString::fromUtf8("이미지 추가…"));
        QCOMPARE(ui.evaluate(menu,"itemAt(1).text").toString(),QString::fromUtf8("배치 되돌리기"));
        QCOMPARE(ui.evaluate(menu,"itemAt(2).text").toString(),QString::fromUtf8("배치 다시 실행"));
        const auto trigger=[&](QObject* target,int index) {ui.evaluate(target,QString("itemAt(%1).triggered()").arg(index));QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QTest::qWait(15);};
        trigger(firstMenu,0);QVERIFY(!library->images().front().toMap()["visible"].toBool());
        trigger(menu,1);QVERIFY(library->images().front().toMap()["visible"].toBool());
        trigger(menu,2);QVERIFY(!library->images().front().toMap()["visible"].toBool());trigger(firstMenu,0);
        trigger(firstMenu,1);QVERIFY(library->images().front().toMap()["locked"].toBool());
        for(int index=3;index<8;++index)QVERIFY(!ui.evaluate(firstMenu,QString("itemAt(%1).enabled").arg(index)).toBool());
        trigger(firstMenu,1);QVERIFY(!library->images().front().toMap()["locked"].toBool());
        QPointer<QObject> blend=qvariant_cast<QObject*>(ui.evaluate(firstMenu,"menuAt(2)"));QVERIFY(blend);
        QCOMPARE(blend->property("count").toInt(),4);
        const QStringList modes={"normal","multiply","screen","difference"};
        for(int i=0;i<modes.size();++i) {trigger(blend,i);QCOMPARE(library->images().front().toMap()["blend"].toString(),modes[i]);}
        trigger(firstMenu,3);QCOMPARE(library->images().front().toMap()["opacity"].toDouble(),.9);
        trigger(firstMenu,4);QCOMPARE(library->images().front().toMap()["opacity"].toDouble(),1.);
        QVERIFY(library->importImage(QUrl::fromLocalFile(ui.files.filePath("reference.png")),"second"));
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QTest::qWait(20);
        const auto secondId=library->images().back().toMap()["id"].toString();
        QCOMPARE(menu->property("count").toInt(),6);
        const auto menuId=[&](int index) {return ui.evaluate(menu,QString("menuAt(%1).modelData.id").arg(index)).toString();};
        QCOMPARE(menuId(4),firstId);QCOMPARE(menuId(5),secondId);
        trigger(firstMenu,5);QCOMPARE(library->images().back().toMap()["id"].toString(),firstId);
        QCOMPARE(menuId(4),secondId);QCOMPARE(menuId(5),firstId);QVERIFY(firstMenu);QVERIFY(blend);
        trigger(firstMenu,6);QCOMPARE(menuId(4),firstId);QCOMPARE(menuId(5),secondId);
        trigger(firstMenu,7);QVERIFY(!firstMenu);QVERIFY(!blend);QCOMPARE(menu->property("count").toInt(),5);
        QCOMPARE(menuId(4),secondId);QCOMPARE(library->images().size(),1);
        QVERIFY(library->removeImage(secondId));
        QVERIFY(library->importImage(QUrl::fromLocalFile(ui.files.filePath("reference.png")),"reimported"));
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QTest::qWait(20);
        const auto reimported=library->images().front().toMap()["id"].toString();QVERIFY(reimported!=firstId);
        QVERIFY(ui.reference());QVERIFY(ui.reference(true));
        QCOMPARE(menu->property("count").toInt(),5);QCOMPARE(menuId(4),reimported);
        QPointer<QObject> finalMenu=qvariant_cast<QObject*>(ui.evaluate(menu,"menuAt(4)"));QVERIFY(finalMenu);
        ui.evaluate(menu,"open()");QTest::qWait(20);ui.evaluate(menu,"close()");
        ui.stop();QVERIFY(!finalMenu);
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

    void referenceDragLeavesOutsideAndLockedMapPanning_data(){widths();}
    void referenceDragLeavesOutsideAndLockedMapPanning()
    {
        QFETCH(int,width);Ui ui(width);QVERIFY2(ui.start(),qPrintable(ui.warnings.join('\n')));
        const auto id=ui.addImage();QVERIFY(!id.isEmpty());auto* library=ui.library();
        QSignalSpy history(library,&ReferenceImageLibrary::historyChanged);
        for(const bool locked:{false,true}) {
            if(locked)QVERIFY(library->updateImage(id,{{"locked",true}}));
            const auto records=library->images();const auto before=ui.editor.mapViewState();const int historyBefore=history.count();
            const auto position=locked?ui.reference()->mapToScene({20,20}).toPoint():ui.map->mapToScene({35,330}).toPoint();
            QTest::mousePress(ui.window,Qt::LeftButton,Qt::NoModifier,position);
            QTest::mouseMove(ui.window,position+QPoint(22,15),20);
            QTest::mouseMove(ui.window,position+QPoint(48,29),20);
            QTest::mouseRelease(ui.window,Qt::LeftButton,Qt::NoModifier,position+QPoint(48,29));
            QVERIFY(ui.editor.mapViewState()["panX"]!=before["panX"]);
            QCOMPARE(library->images(),records);QCOMPARE(history.count(),historyBefore);
        }
        ui.stop();QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

    void referenceActiveDragCancellationRestoresWithoutHistory_data(){widths();}
    void referenceActiveDragCancellationRestoresWithoutHistory()
    {
        QFETCH(int,width);Ui ui(width);QVERIFY2(ui.start(),qPrintable(ui.warnings.join('\n')));
        QVERIFY(!ui.addImage().isEmpty());auto* library=ui.library();
        const auto records=library->images();QPointer<QObject> drag=dragHandler(ui.reference());QVERIFY(drag);
        QSignalSpy history(library,&ReferenceImageLibrary::historyChanged);
        const auto position=ui.reference()->mapToScene({30,30}).toPoint();
        for(const bool reenableBeforeRelease:{false,true}) {
            history.clear();
            QTest::mousePress(ui.window,Qt::LeftButton,Qt::NoModifier,position);
            QTest::mouseMove(ui.window,position+QPoint(22,15),20);
            QTest::mouseMove(ui.window,position+QPoint(48,29),20);
            QVERIFY(drag&&drag->property("active").toBool());QVERIFY(library->images()!=records);
            ui.editor.selectCountry("A");QVERIFY(ui.editor.beginGeometryDraw());
            QTRY_VERIFY(!drag->property("enabled").toBool());
            if(reenableBeforeRelease) {
                ui.editor.cancelGeometryEdit();QVERIFY(drag->property("enabled").toBool());
            }
            QTest::mouseRelease(ui.window,Qt::LeftButton,Qt::NoModifier,position+QPoint(48,29));
            QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QTest::qWait(25);
            QCOMPARE(library->images(),records);QCOMPARE(history.count(),0);
            if(!reenableBeforeRelease)ui.editor.cancelGeometryEdit();
            QTest::qWait(25);QVERIFY(drag->property("enabled").toBool());
            QTest::mousePress(ui.window,Qt::LeftButton,Qt::NoModifier,position);
            QTest::mouseMove(ui.window,position+QPoint(22,15),20);
            QTest::mouseMove(ui.window,position+QPoint(48,29),20);
            QVERIFY(drag->property("active").toBool());
            QTest::mouseRelease(ui.window,Qt::LeftButton,Qt::NoModifier,position+QPoint(48,29));
            QCOMPARE(history.count(),1);QVERIFY(library->images()!=records);
            QVERIFY(library->undo());QCOMPARE(library->images(),records);
        }
        ui.stop();QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

    void referenceCancelLockAndGeometryGuards_data(){widths();}
    void referenceCancelLockAndGeometryGuards()
    {
        QFETCH(int,width);Ui ui(width);QVERIFY2(ui.start(),qPrintable(ui.warnings.join('\n')));
        const auto id=ui.addImage();QVERIFY(!id.isEmpty());auto* library=ui.library();
        const auto canonical=ui.editor.documentBytes();
        const auto original=library->images();QSignalSpy history(library,&ReferenceImageLibrary::historyChanged);
        QVERIFY(library->beginGesture(id));QVERIFY(library->updateGesture({{"x",12.5},{"y",8.25}}));
        QVERIFY(library->images()!=original);library->cancelGesture();QCOMPARE(library->images(),original);
        QCOMPARE(history.count(),0);QVERIFY(!library->commitGesture());
        QVERIFY(library->updateImage(id,{{"locked",true}}));QVERIFY(!library->beginGesture(id));
        for(const bool geometryEditing:{false,true}) {
            if(geometryEditing) {
                QVERIFY(library->updateImage(id,{{"locked",false}}));ui.editor.selectCountry("A");
                QVERIFY(ui.editor.beginGeometryDraw());
            }
            QTRY_VERIFY(ui.reference());auto* image=ui.reference();auto* drag=dragHandler(image);QVERIFY(drag);
            QTRY_VERIFY(!drag->property("enabled").toBool());
            const auto before=library->images();const int historyBefore=history.count();
            const auto position=image->mapToScene(QPointF(image->width()*.35,image->height()*.35)).toPoint();
            QVERIFY(QRect(QPoint(),ui.window->size()).contains(position+QPoint(45,30)));
            QTest::mousePress(ui.window,Qt::LeftButton,Qt::NoModifier,position);
            QTest::mouseMove(ui.window,position+QPoint(25,15),20);
            QTest::mouseMove(ui.window,position+QPoint(45,30),20);
            QTest::mouseRelease(ui.window,Qt::LeftButton,Qt::NoModifier,position+QPoint(45,30));
            QCOMPARE(library->images(),before);QCOMPARE(history.count(),historyBefore);
        }
        QCOMPARE(ui.editor.documentBytes(),canonical);QVERIFY(!ui.editor.canUndo());
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

    void referenceGlobeBoundsTrackGeographicSnapshot_data(){widths();}
    void referenceGlobeBoundsTrackGeographicSnapshot()
    {
        QFETCH(int,width);Ui ui(width);QVERIFY2(ui.start(),qPrintable(ui.warnings.join('\n')));
        QVERIFY(!ui.addImage().isEmpty());const auto records=ui.library()->images();const auto data=records.front().toMap();
        QVERIFY(ui.editor.setProjectionMode("globe"));QTRY_VERIFY(ui.reference(true));
        auto* image=ui.reference(true);QVERIFY(image->isVisible());QVERIFY(!ui.reference()->isVisible());
        const auto before=ui.editor.hydroProjection();
        for(int stage=0;stage<2;++stage) {
            if(stage) {
                QVERIFY(ui.open(fixture(-25.,12.),"replacement.json"));
                QVERIFY(ui.editor.hydroProjection()!=before);QVERIFY(ui.editor.setProjectionMode("globe"));
            }
            const auto projection=ui.editor.hydroProjection();
            const auto x=data["x"].toDouble(),y=data["y"].toDouble();
            const auto width=data["width"].toDouble(),height=data["height"].toDouble();
            QTRY_COMPARE(image->property("west").toDouble(),(x+projection["minX"].toDouble())/projection["cosLatitude"].toDouble());
            QCOMPARE(image->property("east").toDouble(),(x+width+projection["minX"].toDouble())/projection["cosLatitude"].toDouble());
            QCOMPARE(image->property("north").toDouble(),projection["maxLatitude"].toDouble()-y);
            QCOMPARE(image->property("south").toDouble(),projection["maxLatitude"].toDouble()-y-height);
            QCOMPARE(ui.library()->images(),records);
        }
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }
};

int main(int argc,char** argv)
{
#ifdef Q_OS_WIN
    // The offscreen font database still populates its font directory even when
    // application fonts are registered. Point it at installed fonts before Qt
    // initializes the platform, retaining the strict per-test warning policy.
    if(qEnvironmentVariable("QT_QPA_PLATFORM")=="offscreen"&&qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR")) {
        const QDir fonts(qEnvironmentVariable("WINDIR")+"/Fonts");
        if(fonts.exists())qputenv("QT_QPA_FONTDIR",QFile::encodeName(fonts.absolutePath()));
    }
#endif
    QTemporaryDir dataHome;qputenv("XDG_DATA_HOME",dataHome.path().toUtf8());
    QQuickStyle::setStyle("Basic");QGuiApplication app(argc,argv);registerWindowsFrameType();
    EditDisplayUiTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "edit_display_ui_tests.moc"
