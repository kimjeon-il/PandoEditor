#include "editorcontroller.h"
#include "territorial_fixture.h"
#include "windowsframe.h"
#include "ui_navigation.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QElapsedTimer>
#include <QTest>
#include <cmath>
using namespace pandoeditor;
namespace {
Geometry polygon(Ring ring){Geometry g;g.polygons={{std::move(ring)}};return g;}
Geometry box(double x,double y,double w,double h){return polygon({{x,y},{x+w,y},{x+w,y+h},{x,y+h},{x,y}});}
ProjectDocument fixture(bool child){
    ProjectDocument d({{"A","A",polygon({{0,0},{1,0},{1,.5},{1,1},{0,1},{0,0}}).polygons,0xabcdef},
        {"B","B",polygon({{1,0},{2,0},{2,1},{1,1},{1,.5},{1,0}}).polygons,0x123456},
        {"C","C",polygon({{0,1},{1,1},{2,1},{2,2},{0,2},{0,1}}).polygons,0x654321}},{{"countries","Countries"}});
    if(child){d.geometries.insert({"child",1},box(.7,.2,.3,.6));appendTerritory(d,{"child","Child","",UnitKind::General,false},{"child",1},"A");d.presentation.objectStyles[territorialRef("child")]={};}return d;
}
QQuickItem* boundaryButton(QQuickItem* root){
    if(root->isVisible()&&root->property("text").toString()==QStringLiteral("공유 국경")&&root->property("checkable").isValid())return root;
    for(auto* child:root->childItems())if(auto* result=boundaryButton(child))return result;return nullptr;
}
bool fullyVisible(QQuickWindow* window,QQuickItem* item){
    if(!item||!item->isVisible())return false;const QRectF full(item->mapToScene({}),QSizeF(item->width(),item->height()));QRectF visible=full;
    for(auto* parent=item->parentItem();parent;parent=parent->parentItem())if(parent->clip())visible=visible.intersected(QRectF(parent->mapToScene({}),QSizeF(parent->width(),parent->height())));
    return !full.isEmpty()&&visible.intersected(QRectF(QPointF(),window->size()))==full;
}
bool clickItem(QQuickWindow* window,QQuickItem* item){
    if(!item||!item->isVisible()||!item->isEnabled())return false;navigationEnsureVisible(item);QTest::qWait(100);window->grabWindow();
    QRectF visible(item->mapToScene({}),QSizeF(item->width(),item->height()));for(auto* parent=item->parentItem();parent;parent=parent->parentItem())if(parent->clip())visible=visible.intersected(QRectF(parent->mapToScene({}),QSizeF(parent->width(),parent->height())));
    visible=visible.intersected(QRectF(QPointF(),window->size()));if(visible.isEmpty())return false;QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,visible.center().toPoint());QTest::qWait(100);return true;
}
struct Ui {
    QTemporaryDir files;ProjectDocument document;QStringList warnings;EditorController editor;QQmlApplicationEngine engine;QQuickWindow* window=nullptr;QQuickItem* map=nullptr;MapProjection projection;QByteArray before;int width;
    Ui(int w,bool child=false):document(fixture(child)),editor([&]{EditorControllerConfig c;c.mobileMode=w==360;c.bootstrapWorld=false;c.autosaveEnabled=false;c.privateProjectPath=files.filePath("private.json");return c;}()),width(w){QObject::connect(&engine,&QQmlEngine::warnings,&engine,[this](const auto& errors){for(const auto& error:errors)warnings.append(error.toString());});}
    bool start(bool all=true){
        Project project;project.replace(document);QFile file(files.filePath("input.json"));if(!file.open(QIODevice::WriteOnly))return false;file.write(projectcodec::encode(project));file.close();if(!editor.openFile(QUrl::fromLocalFile(file.fileName()))||!editor.setProjectionMode("flat"))return false;
        QVariantList selected{QVariantMap{{"domain","territorial"},{"id","A"}},QVariantMap{{"domain","territorial"},{"id","B"}}};if(all)selected.append(QVariantMap{{"domain","territorial"},{"id","C"}});editor.setSelection(selected,selected.front().toMap(),"map");
        engine.rootContext()->setContextProperty("editor",&editor);const auto root=qEnvironmentVariable("M974_TEST_UI_ROOT");engine.load(root.isEmpty()?QUrl("qrc:/common/Main.qml"):QUrl::fromLocalFile(root+"/common/Main.qml"));if(engine.rootObjects().isEmpty())return false;window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());if(!window)return false;window->resize(width,760);window->show();QTest::qWait(180);window->grabWindow();map=navigationItem(window->contentItem(),"mapView");if(!map)return false;projection.rebuild(document);before=editor.documentBytes();return true;
    }
    bool enter(){
        enterExistingControlRoute(window,"editorAnnexAction");if(!clickItem(window,boundaryButton(window->contentItem())))return false;
        auto* close=navigationItem(window->contentItem(),"closeObjectEditor");if(close&&close->isVisible())clickItem(window,close);return true;
    }
    bool focus(Point geographic){
        const auto a=projection.project({geographic.x,geographic.y}),b=projection.project({geographic.x+1,geographic.y});const auto view=editor.mapViewState();
        if(!editor.zoomMapCameraAt(220./(std::abs(b.x-a.x)*view["mapScale"].toDouble()),map->width()/2,map->height()/2))return false;
        const auto camera=editor.mapViewState();editor.beginMapCameraPan();const bool ok=editor.updateMapCameraPan((width==360?180.:340.)-(camera["originX"].toDouble()+a.x*camera["mapScale"].toDouble()),160.-(camera["originY"].toDouble()+a.y*camera["mapScale"].toDouble()));editor.endMapCameraPan();QTest::qWait(120);window->grabWindow();return ok;
    }
    QPoint at(Point geographic){const auto p=projection.project(geographic);const auto camera=editor.mapViewState();return map->mapToScene({camera["originX"].toDouble()+p.x*camera["mapScale"].toDouble(),camera["originY"].toDouble()+p.y*camera["mapScale"].toDouble()}).toPoint();}
    void tap(Point point,bool touch){const auto p=at(point);if(touch){auto* device=touchDevice();QTest::touchEvent(window,device).press(0,p,window).commit();QTest::touchEvent(window,device).release(0,p,window).commit();}else QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,p);QTest::qWait(80);}
    static QPointingDevice* touchDevice(){static auto* device=QTest::createTouchDevice();return device;}
    void drag(Point start,Point end,bool touch){
        const auto first=at(start),last=at(end);if(touch)QTest::touchEvent(window,touchDevice()).press(0,first,window).commit();else QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,first);QTest::qWait(40);
        for(int i=1;i<=5;++i){const auto p=first+(last-first)*i/5;if(touch)QTest::touchEvent(window,touchDevice()).move(0,p,window).commit();else QTest::mouseMove(window,p,25);QTest::qWait(25);}
        if(touch)QTest::touchEvent(window,touchDevice()).release(0,last,window).commit();else QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,last);QTest::qWait(80);
    }
    bool click(const char* name){return clickItem(window,navigationItem(window->contentItem(),name));}
    void capture(const QString& stage){const auto folder=qEnvironmentVariable("M974_BOUNDARY_UI_CAPTURE_DIR");if(!folder.isEmpty()){QDir().mkpath(folder);window->grabWindow().save(folder+QString("/boundary-%1-%2.png").arg(width).arg(stage));}}
    ~Ui(){if(window){window->setProperty("allowClose",true);window->close();}}
};
}
class M974BoundaryUiTests:public QObject {
    Q_OBJECT
private slots:
    void pointerDragPreviewAndConfirm_data(){QTest::addColumn<int>("width");QTest::addColumn<bool>("touch");QTest::newRow("desktop-mouse")<<1100<<false;QTest::newRow("desktop-touch")<<1100<<true;QTest::newRow("small-mouse")<<360<<false;QTest::newRow("small-touch")<<360<<true;}
    void pointerDragPreviewAndConfirm(){
        QFETCH(int,width);QFETCH(bool,touch);Ui ui(width);QVERIFY2(ui.start(),qPrintable(ui.warnings.join('\n')));QVERIFY(ui.enter());QTRY_COMPARE_WITH_TIMEOUT(ui.editor.geometryEditState()["boundaryStatus"].toString(),QString("ready"),4000);QVERIFY(ui.focus({1,1}));ui.capture(touch?"touch-prepared":"mouse-prepared");
        ui.tap({1,1},touch);QVERIFY(ui.editor.geometryEditState()["selectedVertex"].toInt()>=0);ui.drag({1,1},{1,1.2},touch);QTRY_VERIFY_WITH_TIMEOUT(ui.editor.geometryEditState()["previewReady"].toBool(),5000);QCOMPARE(ui.editor.documentBytes(),ui.before);ui.capture(touch?"touch-preview":"mouse-preview");QVERIFY(ui.click("geometryConfirm"));QTRY_VERIFY(!ui.editor.geometryEditState()["active"].toBool());QVERIFY(ui.editor.documentBytes()!=ui.before);ui.editor.undo();QCOMPARE(ui.editor.documentBytes(),ui.before);QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }
    void fixedNodeDoesNotMove_data(){QTest::addColumn<int>("width");QTest::newRow("desktop")<<1100;QTest::newRow("small")<<360;}
    void fixedNodeDoesNotMove(){QFETCH(int,width);Ui ui(width);QVERIFY(ui.start(false));QVERIFY(ui.enter());QTRY_COMPARE_WITH_TIMEOUT(ui.editor.geometryEditState()["boundaryStatus"].toString(),QString("ready"),4000);QVERIFY(ui.focus({1,1}));const auto paths=ui.editor.geometryDraftPaths();ui.tap({1,1},width==360);QCOMPARE(ui.editor.geometryEditState()["selectedVertex"].toInt(),-1);ui.drag({1,1},{1,1.2},width==360);QCOMPARE(ui.editor.geometryDraftPaths(),paths);QVERIFY(!ui.editor.geometryEditState()["previewReady"].toBool());QCOMPARE(ui.editor.documentBytes(),ui.before);QVERIFY(!ui.editor.canUndo());}
    void actualMiddleButtonStillPansDuringBoundaryEditing(){
        Ui ui(1100);QVERIFY(ui.start());QVERIFY(ui.enter());QTRY_COMPARE_WITH_TIMEOUT(ui.editor.geometryEditState()["boundaryStatus"].toString(),QString("ready"),4000);QVERIFY(ui.focus({1,1}));const auto before=ui.editor.mapViewState();const auto paths=ui.editor.geometryDraftPaths();const auto point=ui.at({1,1});QTest::mousePress(ui.window,Qt::MiddleButton,Qt::NoModifier,point);QTest::qWait(30);for(int step=1;step<=6;++step){QTest::mouseMove(ui.window,point+QPoint(40,30)*step/6,25);QTest::qWait(20);}QTest::mouseRelease(ui.window,Qt::MiddleButton,Qt::NoModifier,point+QPoint(40,30));QTRY_VERIFY(ui.editor.mapViewState()["originX"].toDouble()!=before["originX"].toDouble());QCOMPARE(ui.editor.geometryDraftPaths(),paths);QCOMPARE(ui.editor.documentBytes(),ui.before);QVERIFY(!ui.editor.geometryEditState()["previewReady"].toBool());
    }
    void impactGateDeclineAndApprove_data(){QTest::addColumn<int>("width");QTest::newRow("desktop")<<1100;QTest::newRow("small")<<360;}
    void impactGateDeclineAndApprove(){
        QFETCH(int,width);Ui ui(width,true);QVERIFY(ui.start(false));QVERIFY(ui.enter());QTRY_COMPARE_WITH_TIMEOUT(ui.editor.geometryEditState()["boundaryStatus"].toString(),QString("ready"),4000);QVERIFY(ui.focus({1,.5}));ui.tap({1,.5},width==360);QVERIFY(ui.editor.geometryEditState()["selectedVertex"].toInt()>=0);ui.drag({1,.5},{.8,.5},width==360);QTRY_VERIFY_WITH_TIMEOUT(ui.editor.geometryEditState()["previewReady"].toBool(),5000);QVERIFY(ui.click("geometryConfirm"));QVERIFY(ui.editor.geometryEditState()["boundaryImpactConfirmation"].toBool());QCOMPARE(ui.editor.documentBytes(),ui.before);QVERIFY(fullyVisible(ui.window,navigationItem(ui.window->contentItem(),"geometryConfirmBoundaryImpacts")));QVERIFY(fullyVisible(ui.window,navigationItem(ui.window->contentItem(),"geometryCancelBoundaryImpacts")));ui.capture("impact-confirmation");QVERIFY(ui.click("geometryCancelBoundaryImpacts"));QVERIFY(!ui.editor.geometryEditState()["previewReady"].toBool());QCOMPARE(ui.editor.documentBytes(),ui.before);QVERIFY(!ui.editor.canUndo());
        ui.tap({1,.5},width==360);QVERIFY(ui.editor.geometryEditState()["selectedVertex"].toInt()>=0);ui.drag({1,.5},{.8,.5},width==360);QTRY_VERIFY_WITH_TIMEOUT(ui.editor.geometryEditState()["previewReady"].toBool(),5000);QVERIFY(ui.click("geometryConfirm"));QVERIFY(ui.editor.geometryEditState()["boundaryImpactConfirmation"].toBool());QVERIFY(ui.click("geometryConfirmBoundaryImpacts"));QTRY_VERIFY(!ui.editor.geometryEditState()["active"].toBool());QVERIFY(ui.editor.documentBytes()!=ui.before);ui.editor.undo();QCOMPARE(ui.editor.documentBytes(),ui.before);QVERIFY2(ui.warnings.isEmpty(),qPrintable(ui.warnings.join('\n')));
    }
};
int main(int argc,char** argv){QQuickStyle::setStyle("Basic");QGuiApplication app(argc,argv);registerWindowsFrameType();M974BoundaryUiTests tests;return QTest::qExec(&tests,argc,argv);}
#include "m974_boundary_ui_tests.moc"
