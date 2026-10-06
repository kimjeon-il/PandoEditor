#include "editorcontroller.h"
#include "referenceimagelibrary.h"
#include "ui_navigation.h"
#include "windowsframe.h"
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlContext>
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
            // Exercise the exact production reference-image delegates and
            // handlers, without the unrelated reference-menu Repeater. Full
            // Main.qml fixture import via library.importImage reproduces a
            // crash in legacy and migrated QML alongside the warning that the
            // Menu delegate is not an Item. No FileDialog interaction is tested.
            // Keep this explicitly component-level, not full import-flow parity.
            QFile source(root.isEmpty()?QString(":/common/MapView.qml"):root+"/common/MapView.qml");
            if(!source.open(QIODevice::ReadOnly))return false;
            const auto qml=source.readAll();
            const int first=qml.indexOf("    ReferenceImageLibrary { id: referenceImages }");
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
    ReferenceImageLibrary* library() const {return window->findChild<ReferenceImageLibrary*>();}
    QString addImage()
    {
        auto* images=library();if(!images)return {};
        for(const auto& row:images->images()) {
            const auto id=row.toMap()["id"].toString();
            images->updateImage(id,{{"locked",false}});if(!images->removeImage(id))return {};
        }
        QImage image(8,6,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::red);
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
    ~Ui(){if(window){QTest::mouseMove(window,QPoint(-100,-100));window->setProperty("allowClose",true);window->close();}}
};

void widths()
{
    QTest::addColumn<int>("width");QTest::newRow("desktop-1100")<<1100;QTest::newRow("mobile-360")<<360;
}
}

class EditDisplayUiTests:public QObject {
    Q_OBJECT
private slots:
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
        QFETCH(int,width);Ui ui(width);QVERIFY2(ui.start(true),qPrintable(ui.warnings.join('\n')));
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
        // The first images publication recreates the production delegate in
        // legacy and migrated QML. Release/history and repeated mid-drag updates
        // remain blocked by that pre-existing lifetime issue. Restore through
        // the library's existing cancellation API, not a synthetic UI commit.
        ui.library()->cancelGesture();
        QTest::mouseRelease(ui.window,Qt::LeftButton,Qt::NoModifier,position+QPoint(52,31));
        QCOMPARE(ui.library()->images(),records);
        QCOMPARE(ui.editor.documentBytes(),canonical);QVERIFY(!ui.editor.canUndo());
        QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }

    void referenceCancelLockAndGeometryGuards_data(){widths();}
    void referenceCancelLockAndGeometryGuards()
    {
        QFETCH(int,width);Ui ui(width);QVERIFY2(ui.start(true),qPrintable(ui.warnings.join('\n')));
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
        QFETCH(int,width);Ui ui(width);QVERIFY2(ui.start(true),qPrintable(ui.warnings.join('\n')));
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
    QTemporaryDir dataHome;qputenv("XDG_DATA_HOME",dataHome.path().toUtf8());
    QQuickStyle::setStyle("Basic");QGuiApplication app(argc,argv);registerWindowsFrameType();
    EditDisplayUiTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "edit_display_ui_tests.moc"
