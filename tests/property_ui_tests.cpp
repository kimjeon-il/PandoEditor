#include "territorial_fixture.h"
#include "editorcontroller.h"
#include "ui_navigation.h"
#include "windowsframe.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QInputMethodEvent>
#include <memory>
#include <QJSValue>
#include <QVector2D>
#include <QTouchEvent>
using namespace pandoeditor;

namespace {
QVariantMap ref(QString id,QString type="general") {return {{"domain","territorial"},{"type",type},{"id",id}};}
QStringList ids(const QVariantList& rows) {QStringList out;for(auto row:rows)out<<row.toMap()["id"].toString();return out;}
QQuickItem* item(QQuickItem* root,const QString& name) {
    if(root->objectName()==name)return root;
    for(auto child:root->childItems())if(auto found=item(child,name))return found;
    return nullptr;
}
void modes() {QTest::addColumn<bool>("mobile");QTest::newRow("desktop-1100")<<false;QTest::newRow("mobile-360")<<true;}
ProjectDocument fixture() {
    ProjectDocument d({
        {"A","Alpha",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x123456},
        {"B","Beta",{{{{20,0},{30,0},{30,10},{20,10},{20,0}}}},0xabcdef}
    },{{"countries","Countries"},{"other","Units"},{"hidden","Hidden",false,false,1}});
    d.documentId="selection-ui-v1";
    auto add=[&](std::string id,UnitKind kind,std::string layer,double left,double right,bool locked) {
        Geometry g;g.type="Polygon";g.polygons={{{{left,2},{right,2},{right,4},{left,4},{left,2}}}};
        GeometryRef geometry{"ui-"+id,1};d.geometries.insert(geometry,std::move(g));
        appendTerritory(d,{id,id,"",kind,locked},geometry);
        d.presentation.membership[territorialRef(id)]=layer;d.presentation.objectStyles[territorialRef(id)]={0xabcdef,.8};
    };
    add("S",UnitKind::General,"other",1,4,true);
    add("R",UnitKind::Regional,"other",6,8,false);
    add("H",UnitKind::Regional,"hidden",40,42,false);
    setFixtureParent(d,territorialRef("S"),territorialRef("A"));
    validateDocument(d);return d;
}
struct Harness {
    QTemporaryDir dir;
    EditorController editor;
    QQmlApplicationEngine engine;
    QQuickWindow* window=nullptr;
    QStringList warnings;
    QUrl path;
    Harness(bool mobile):editor(EditorControllerConfig{mobile,dir.filePath("private.json")}) {
        Project p;p.replace(fixture());path=QUrl::fromLocalFile(dir.filePath("fixture.json"));
        QFile f(path.toLocalFile());if(!f.open(QIODevice::WriteOnly))throw std::runtime_error("fixture write");
        f.write(projectcodec::encode(p));f.close();
        if(!editor.openFile(path))throw std::runtime_error("fixture open");
        QObject::connect(&engine,&QQmlEngine::warnings,&engine,[this](const QList<QQmlError>& errors){
            for(const auto& e:errors)warnings<<e.toString();
        });
        engine.rootContext()->setContextProperty("editor",&editor);
        const auto root=qEnvironmentVariable("M974_TEST_UI_ROOT");
        engine.load(root.isEmpty()?QUrl("qrc:/common/Main.qml"):QUrl::fromLocalFile(root+"/common/Main.qml"));
        if(!engine.rootObjects().isEmpty())window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        if(window){window->resize(mobile?360:1100,mobile?640:760);window->show();QTest::qWait(150);window->grabWindow();}
    }
    ~Harness(){if(window){window->setProperty("allowClose",true);window->close();}engine.clearComponentCache();}
    QQuickItem* control(const QString& name)const{return window?item(window->contentItem(),name):nullptr;}
    bool click(const QString& name,Qt::KeyboardModifiers mods=Qt::NoModifier,bool touch=false) {
        enterExistingControlRoute(window,name);
        auto c=control(name);if(!c||!c->isVisible()||!c->isEnabled())return false;
        window->grabWindow();QTest::qWait(50); // settle visibility-driven form layout before scrolling
        // Bring a delegate into view without replacing an actual input gesture.
        for(auto p=c->parentItem();p;p=p->parentItem()){
            if(p->property("contentY").isValid()){
                auto content=qvariant_cast<QQuickItem*>(p->property("contentItem"));
                if(content){
                    double y=c->mapToItem(content,QPointF()).y();
                    const double max=std::max(0.,p->property("contentHeight").toDouble()-p->height());
                    p->setProperty("contentY",std::clamp(y-8.,0.,max));
                }            }
        }
        QTest::qWait(50);
        QRectF visibleRect(c->mapToScene(QPointF()),QSizeF(c->width(),c->height()));
        for(auto p=c->parentItem();p;p=p->parentItem())if(p->clip())
            visibleRect=visibleRect.intersected(QRectF(p->mapToScene(QPointF()),QSizeF(p->width(),p->height())));
        visibleRect=visibleRect.intersected(QRectF(QPointF(),window->size()));
        if(visibleRect.isEmpty())return false;
        const auto pos=visibleRect.center().toPoint();
        if(!QRect(QPoint(),window->size()).contains(pos))return false;
        if(touch){
            QTest::keyRelease(window,Qt::Key_Control,Qt::NoModifier);
            QTest::keyRelease(window,Qt::Key_Meta,Qt::NoModifier);
            static auto device=QTest::createTouchDevice();
            QTest::touchEvent(window,device).press(0,pos,window).commit();
            QTest::touchEvent(window,device).release(0,pos,window).commit();
        }else QTest::mouseClick(window,Qt::LeftButton,mods,pos);
        QTest::qWait(70);return true;
    }
    bool search(const QString& value) {
        if(!control("objectSearchField")->isVisible()&&!click("searchTab"))return false;
        if(!click("objectSearchField"))return false;
        auto c=control("objectSearchField");
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
        // IME commit also covers non-ASCII search input.
        if(value.isEmpty())QTest::keyClick(window,Qt::Key_Backspace);
        else {QInputMethodEvent event;event.setCommitString(value);QGuiApplication::sendEvent(c,&event);}
        if(!QTest::qWaitFor([&]{return editor.searchQuery()==value;},3000))return false;
        window->grabWindow();return true;
    }
    bool enter(const QString& name,const QString& value,bool finish=true) {
        if(!click(name)){qDebug()<<"unreachable input"<<name<<"selected"<<editor.selectedId()<<"memo popup"<<(window->findChild<QObject*>("objectNotesPopover")?window->findChild<QObject*>("objectNotesPopover")->property("visible"):QVariant());return false;}
        auto c=control(name);QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
        if(value.isEmpty())QTest::keyClick(window,Qt::Key_Backspace);
        else {QInputMethodEvent event;event.setCommitString(value);QGuiApplication::sendEvent(c,&event);}
        if(finish)QTest::keyClick(window,Qt::Key_Return);
        QTest::qWait(80);return true;
    }
    QPointF point(const QString& id) const {
        for(auto value:editor.paths()){
            const auto p=value.toMap();if(p["countryId"]!=id)continue;
            return {p["left"].toDouble()+p["width"].toDouble()/2,p["top"].toDouble()+p["height"].toDouble()/2};
        }
        return {};
    }
    QPoint mapPoint(const QString& id)const{
        const auto p=point(id);auto map=control("mapView");
        return map->mapToScene({map->property("originX").toDouble()+p.x()*map->property("mapScale").toDouble(),
                               map->property("originY").toDouble()+p.y()*map->property("mapScale").toDouble()}).toPoint();
    }
    void mapClick(const QString& id,Qt::KeyboardModifiers mods=Qt::NoModifier,bool touch=false){
        auto pos=mapPoint(id);
        if(id=="A"){
            // At 360px the web's 12px subunit tolerance reaches the rectangle
            // center. Use an interior point away from S/R for a single-country hit.
            for(auto value:editor.paths()){
                const auto p=value.toMap();if(p["countryId"]!=id)continue;
                auto map=control("mapView");
                const QPointF projected(p["left"].toDouble()+p["width"].toDouble()*.85,
                                        p["top"].toDouble()+p["height"].toDouble()*.15);
                pos=map->mapToScene({map->property("originX").toDouble()+projected.x()*map->property("mapScale").toDouble(),
                                     map->property("originY").toDouble()+projected.y()*map->property("mapScale").toDouble()}).toPoint();
            }
        }
        if(touch){
            QTest::keyRelease(window,Qt::Key_Control,Qt::NoModifier);
            QTest::keyRelease(window,Qt::Key_Meta,Qt::NoModifier);
            static auto device=QTest::createTouchDevice();
            QTest::touchEvent(window,device).press(0,pos,window).commit();
            QTest::touchEvent(window,device).release(0,pos,window).commit();
        }else QTest::mouseClick(window,Qt::LeftButton,mods,pos);
        QTest::qWait(100);
    }
};
}
namespace {
QVariantMap labelData(QQuickItem* label) {
    const auto data=label->property("modelData");
    return data.metaType()==QMetaType::fromType<QJSValue>()?data.value<QJSValue>().toVariant().toMap():data.toMap();
}
QQuickItem* placedLabel(QQuickItem* root,const QString& id) {
    if(root->objectName()=="mapPlacedLabel"&&labelData(root).value("ref").toMap().value("id")==id)return root;
    for(auto child:root->childItems())if(auto found=placedLabel(child,id))return found;
    return nullptr;
}
QObject* labelDragHandler(QQuickItem* label) {
    for(auto* child:label->children())
        if(QString::fromLatin1(child->metaObject()->className()).startsWith("QQuickDragHandler"))return child;
    return nullptr;
}
LabelSettings settings(const EditorController& editor,const ObjectRef& owner=territorialRef("A")) {
    const auto all=projectcodec::decode(editor.documentBytes()).presentation.webPresentation.labelSettings;
    const auto it=all.find(owner);return it==all.end()?LabelSettings{}:it->second;
}
}
class PropertyUiTests:public QObject {
 Q_OBJECT
private slots:
    void labelDragPinsPositionAndResets_data(){modes();}
    void labelDragPinsPositionAndResets(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);
        QVERIFY(h.editor.setProjectionMode("flat"));h.editor.clearSelection();
        QTRY_VERIFY(placedLabel(h.window->contentItem(),"A"));
        auto* label=placedLabel(h.window->contentItem(),"A");
        QPointer<QObject> drag=labelDragHandler(label);QVERIFY(drag);
        auto* map=h.control("mapView");QVERIFY(map);
        const auto camera=h.editor.mapViewState();
        const auto originalDocument=projectcodec::decode(h.editor.documentBytes());
        const auto contentRevision=h.editor.revision();
        QVERIFY(!settings(h.editor).pinned);QVERIFY(!settings(h.editor).manualPosition);
        static auto* device=QTest::createTouchDevice();
        QSignalSpy presentation(&h.editor,&EditorController::presentationChanged);
        for(const QPoint delta:{QPoint(60,40),QPoint(-35,-25)}) {
            QTRY_VERIFY(placedLabel(h.window->contentItem(),"A"));
            label=placedLabel(h.window->contentItem(),"A");drag=labelDragHandler(label);QVERIFY(drag);
            const double startX=label->property("labelX").toDouble(),startY=label->property("labelY").toDouble();
            const auto pos=label->mapToScene(QPointF(label->width()/2,label->height()/2)).toPoint();
            const int signalsBefore=presentation.count();
            if(mobile) {
                QTest::touchEvent(h.window,device).press(0,pos,h.window).commit();
                QTest::touchEvent(h.window,device).move(0,pos+delta/2,h.window).commit();
                QTRY_VERIFY(drag->property("active").toBool());
                QTest::touchEvent(h.window,device).move(0,pos+delta,h.window).commit();QTest::qWait(20);
            } else {
                QTest::mousePress(h.window,Qt::LeftButton,Qt::NoModifier,pos);
                QTest::mouseMove(h.window,pos+delta/2,20);
                QTest::mouseMove(h.window,pos+delta,20);
            }
            QTRY_VERIFY(drag->property("active").toBool());
            QTRY_VERIFY(drag->property("activeTranslation").value<QVector2D>().lengthSquared()>4);
            const auto translation=drag->property("activeTranslation").value<QVector2D>();
            QVERIFY(translation.lengthSquared()>4);
            QCOMPARE(presentation.count(),signalsBefore); // One command on release, never during the gesture.
            const auto projection=h.editor.hydroProjection();
            const double x=(startX+translation.x()-map->property("originX").toDouble())/map->property("mapScale").toDouble();
            const double y=(startY+translation.y()-map->property("originY").toDouble())/map->property("mapScale").toDouble();
            const Point expected{(x+projection.value("minX").toDouble())/projection.value("cosLatitude").toDouble(),
                projection.value("maxLatitude").toDouble()-y};
            if(mobile)QTest::touchEvent(h.window,device).release(0,pos+delta,h.window).commit();
            else QTest::mouseRelease(h.window,Qt::LeftButton,Qt::NoModifier,pos+delta,20);
            QTRY_VERIFY(!drag || !drag->property("active").toBool());
            QTRY_VERIFY_WITH_TIMEOUT(settings(h.editor).pinned,1000);
            const auto pinned=settings(h.editor);QVERIFY(pinned.manualPosition);
            QVERIFY(std::abs(pinned.manualPosition->x-expected.x)<1e-6);
            QVERIFY(std::abs(pinned.manualPosition->y-expected.y)<1e-6);
            QCOMPARE(presentation.count(),signalsBefore+1);
            QCOMPARE(h.editor.mapViewState(),camera);
            // Presentation publication intentionally rebuilds label delegates.
            QTRY_VERIFY(placedLabel(h.window->contentItem(),"A"));
            label=placedLabel(h.window->contentItem(),"A");
            QTRY_VERIFY(labelData(label).value("pinned").toBool());
            // A tap after a completed drag must not reuse the previous delta.
            QTest::qWait(ViewportResourceScheduler::SettleDelayMs*2);
            const auto afterDrag=h.editor.documentBytes();
            const auto tap=label->mapToScene(QPointF(label->width()/2,label->height()/2)).toPoint();
            if(mobile){QTest::touchEvent(h.window,device).press(0,tap,h.window).commit();QTest::touchEvent(h.window,device).release(0,tap,h.window).commit();}
            else QTest::mouseClick(h.window,Qt::LeftButton,Qt::NoModifier,tap);
            QCOMPARE(h.editor.documentBytes(),afterDrag);QCOMPARE(presentation.count(),signalsBefore+1);
            h.editor.clearSelection();
        }
        const auto pinned=settings(h.editor);QVERIFY(pinned.manualPosition);
        // Presentation-only label edits intentionally do not enter content undo
        // history. Keep that contract; adding presentation history is separate.
        QVERIFY(!h.editor.canUndo());QVERIFY(!h.editor.canRedo());
        QVERIFY(h.editor.resetLabelPosition(ref("A")));
        QVERIFY(!settings(h.editor).pinned);QVERIFY(!settings(h.editor).manualPosition);
        QVERIFY(!h.editor.canUndo());QVERIFY(!h.editor.canRedo());
        const auto finalDocument=projectcodec::decode(h.editor.documentBytes());
        QCOMPARE(finalDocument.units.size(),originalDocument.units.size());
        QCOMPARE(h.editor.revision(),contentRevision);
        QCOMPARE(finalDocument.presentation.webPresentation.labelSettings.size(),std::size_t(1));
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join("\n")));
    }
    void independentPendingLabelReleases_data(){
        QTest::addColumn<bool>("replaceBetweenCommits");
        QTest::newRow("both-commands")<<false;QTest::newRow("replace-between-commands")<<true;
    }
    void independentPendingLabelReleases(){
        QFETCH(bool,replaceBetweenCommits);Harness h(false);QVERIFY(h.window);
        QVERIFY(h.editor.setProjectionMode("flat"));
        QTRY_VERIFY(placedLabel(h.window->contentItem(),"A"));
        QTRY_VERIFY(placedLabel(h.window->contentItem(),"B"));
        QSignalSpy presentation(&h.editor,&EditorController::presentationChanged);
        bool replaced=false,reopened=false,firstWasA=false;QByteArray replacement;
        if(replaceBetweenCommits)QObject::connect(&h.editor,&EditorController::presentationChanged,&h.editor,[&] {
            if(replaced)return;
            replaced=true;firstWasA=settings(h.editor).pinned;
            reopened=h.editor.openFile(h.path);replacement=h.editor.documentBytes();
        });
        std::map<std::string,Point> expected;
        ulong timestamp=1000;
        // Deliver two real mouse gesture sequences synchronously, before the
        // event loop flushes either successful release's deferred command.
        const auto mouse=[&](QEvent::Type type,QPoint pos,Qt::MouseButton button,Qt::MouseButtons buttons) {
            QMouseEvent event(type,QPointF(pos),QPointF(h.window->mapToGlobal(pos)),button,buttons,Qt::NoModifier);
            event.setTimestamp(timestamp+=20);QCoreApplication::sendEvent(h.window,&event);
        };
        for(const QString id:{QString("A"),QString("B")}) {
            auto* label=placedLabel(h.window->contentItem(),id);QVERIFY(label);
            auto* drag=labelDragHandler(label);QVERIFY(drag);
            const auto pos=label->mapToScene(QPointF(label->width()/2,label->height()/2)).toPoint();
            mouse(QEvent::MouseButtonPress,pos,Qt::LeftButton,Qt::LeftButton);
            mouse(QEvent::MouseMove,pos+QPoint(25,20),Qt::NoButton,Qt::LeftButton);
            mouse(QEvent::MouseMove,pos+QPoint(60,40),Qt::NoButton,Qt::LeftButton);
            QVERIFY(drag->property("active").toBool());
            const auto delta=drag->property("activeTranslation").value<QVector2D>();QVERIFY(delta.lengthSquared()>4);
            const auto camera=h.editor.mapViewState(),projection=h.editor.hydroProjection();
            const double x=(label->property("labelX").toDouble()+delta.x()-camera["originX"].toDouble())/camera["mapScale"].toDouble();
            const double y=(label->property("labelY").toDouble()+delta.y()-camera["originY"].toDouble())/camera["mapScale"].toDouble();
            expected[id.toStdString()]={(x+projection["minX"].toDouble())/projection["cosLatitude"].toDouble(),projection["maxLatitude"].toDouble()-y};
            mouse(QEvent::MouseButtonRelease,pos+QPoint(60,40),Qt::LeftButton,Qt::NoButton);
            QVERIFY(!drag->property("active").toBool());QCOMPARE(presentation.count(),0);
        }
        QCoreApplication::processEvents();QTest::qWait(50);
        if(replaceBetweenCommits) {
            QVERIFY(replaced);QVERIFY(reopened);QVERIFY(firstWasA);
            QCOMPARE(h.editor.documentBytes(),replacement);
            QVERIFY(!settings(h.editor).pinned);QVERIFY(!settings(h.editor,territorialRef("B")).pinned);
        } else {
            QCOMPARE(presentation.count(),2);
            for(const auto& [id,point]:expected) {
                const auto pinned=settings(h.editor,territorialRef(id));QVERIFY(pinned.pinned);QVERIFY(pinned.manualPosition);
                QVERIFY(std::abs(pinned.manualPosition->x-point.x)<1e-6);
                QVERIFY(std::abs(pinned.manualPosition->y-point.y)<1e-6);
            }
        }
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join("\n")));
    }
    void labelReleaseCapturesCameraBeforeQueuedFlush_data(){modes();}
    void labelReleaseCapturesCameraBeforeQueuedFlush(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);QVERIFY(h.editor.setProjectionMode("flat"));
        QTRY_VERIFY(placedLabel(h.window->contentItem(),"A"));
        auto* label=placedLabel(h.window->contentItem(),"A");auto* drag=labelDragHandler(label);QVERIFY(drag);
        const auto pos=label->mapToScene(QPointF(label->width()/2,label->height()/2)).toPoint();
        const double labelX=label->property("labelX").toDouble(),labelY=label->property("labelY").toDouble();
        QSignalSpy presentation(&h.editor,&EditorController::presentationChanged);
        const auto revision=h.editor.revision();
        // Synchronous real pointer delivery leaves Qt.callLater pending, so a
        // later camera genuinely intervenes before the presentation command.
        ulong timestamp=1000;
        const auto mouse=[&](QEvent::Type type,QPoint point,Qt::MouseButton button,Qt::MouseButtons buttons){
            QMouseEvent event(type,QPointF(point),QPointF(h.window->mapToGlobal(point)),button,buttons,Qt::NoModifier);
            event.setTimestamp(timestamp+=20);QCoreApplication::sendEvent(h.window,&event);
        };
        mouse(QEvent::MouseButtonPress,pos,Qt::LeftButton,Qt::LeftButton);
        mouse(QEvent::MouseMove,pos+QPoint(25,20),Qt::NoButton,Qt::LeftButton);
        mouse(QEvent::MouseMove,pos+QPoint(60,40),Qt::NoButton,Qt::LeftButton);
        QVERIFY(drag->property("active").toBool());
        const auto delta=drag->property("activeTranslation").value<QVector2D>();QVERIFY(delta.lengthSquared()>4);
        const auto releaseCamera=h.editor.mapViewState(),projection=h.editor.hydroProjection();
        const double x=(labelX+delta.x()-releaseCamera["originX"].toDouble())/releaseCamera["mapScale"].toDouble();
        const double y=(labelY+delta.y()-releaseCamera["originY"].toDouble())/releaseCamera["mapScale"].toDouble();
        const Point expected{(x+projection["minX"].toDouble())/projection["cosLatitude"].toDouble(),
            projection["maxLatitude"].toDouble()-y};
        mouse(QEvent::MouseButtonRelease,pos+QPoint(60,40),Qt::LeftButton,Qt::NoButton);
        QVERIFY(!drag->property("active").toBool());QCOMPARE(presentation.count(),0);QVERIFY(!settings(h.editor).pinned);
        h.editor.beginMapCameraPan();QVERIFY(h.editor.updateMapCameraPan(39.,-23.));h.editor.endMapCameraPan();
        QVERIFY(h.editor.zoomMapCameraAt(1.4,120.,160.));
        const auto laterCamera=h.editor.mapViewState();QVERIFY(laterCamera!=releaseCamera);
        const double lateX=(labelX+delta.x()-laterCamera["originX"].toDouble())/laterCamera["mapScale"].toDouble();
        const double lateY=(labelY+delta.y()-laterCamera["originY"].toDouble())/laterCamera["mapScale"].toDouble();
        QVERIFY(std::abs(lateX-x)+std::abs(lateY-y)>1e-6);QCOMPARE(presentation.count(),0);
        QTRY_VERIFY_WITH_TIMEOUT(settings(h.editor).pinned,1000);
        const auto pinned=settings(h.editor);QVERIFY(pinned.manualPosition);
        QVERIFY(std::abs(pinned.manualPosition->x-expected.x)<1e-6);
        QVERIFY(std::abs(pinned.manualPosition->y-expected.y)<1e-6);
        QCOMPARE(presentation.count(),1);QCOMPARE(h.editor.mapViewState(),laterCamera);
        QCOMPARE(h.editor.revision(),revision);QVERIFY(!h.editor.canUndo());QVERIFY(!h.editor.canRedo());
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join("\n")));
    }
    void labelReleasedBeforeProjectReplacementCannotPinReusedId(){
        Harness h(false);QVERIFY(h.window);QVERIFY(h.editor.setProjectionMode("flat"));
        QTRY_VERIFY(placedLabel(h.window->contentItem(),"A"));
        auto* label=placedLabel(h.window->contentItem(),"A");auto* drag=labelDragHandler(label);QVERIFY(drag);
        const auto pos=label->mapToScene(QPointF(label->width()/2,label->height()/2)).toPoint();
        QTest::mousePress(h.window,Qt::LeftButton,Qt::NoModifier,pos);
        QTest::mouseMove(h.window,pos+QPoint(25,20),20);
        QTest::mouseMove(h.window,pos+QPoint(60,40),20);
        QVERIFY(drag->property("active").toBool());
        QVERIFY(drag->property("activeTranslation").value<QVector2D>().lengthSquared()>4);
        // Send release synchronously so the queued presentation command has not
        // run yet. QTest::mouseRelease itself would process that queue for us.
        const auto releasePos=pos+QPoint(60,40);
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(releasePos),
            QPointF(h.window->mapToGlobal(releasePos)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QCoreApplication::sendEvent(h.window,&release);
        QVERIFY(!drag->property("active").toBool());
        QVERIFY(!settings(h.editor).pinned);
        const auto previousInstance=h.editor.projectInstanceId();
        QVERIFY(h.editor.openFile(h.path));
        QVERIFY(h.editor.projectInstanceId()!=previousInstance);
        const auto replacement=h.editor.documentBytes();
        // Also clears QTest's mouse-button state before the next test case.
        QTest::mouseRelease(h.window,Qt::LeftButton,Qt::NoModifier,releasePos);
        QTest::qWait(50);
        QCOMPARE(h.editor.documentBytes(),replacement);
        QVERIFY(!settings(h.editor).pinned);QVERIFY(!settings(h.editor).manualPosition);
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join("\n")));
    }
    void labelCanceledAndDisabledDragsDoNotCommit_data(){
        QTest::addColumn<int>("ending");QTest::newRow("touch-cancel")<<0;QTest::newRow("disable-during-drag")<<1;QTest::newRow("return-to-origin")<<2;
    }
    void labelCanceledAndDisabledDragsDoNotCommit(){
        QFETCH(int,ending);Harness h(true);QVERIFY(h.window);QVERIFY(h.editor.setProjectionMode("flat"));
        QTRY_VERIFY(placedLabel(h.window->contentItem(),"A"));
        auto* label=placedLabel(h.window->contentItem(),"A");auto* drag=labelDragHandler(label);QVERIFY(drag);
        static auto* device=QTest::createTouchDevice();
        const auto pos=label->mapToScene(QPointF(label->width()/2,label->height()/2)).toPoint();
        const auto before=h.editor.documentBytes();QSignalSpy presentation(&h.editor,&EditorController::presentationChanged);
        QTest::touchEvent(h.window,device).press(0,pos,h.window).commit();
        QTest::touchEvent(h.window,device).move(0,pos+QPoint(25,20),h.window).commit();QTest::qWait(20);
        // Qt may defer touch delivery until the next frame. Observe activation
        // before the second movement so it cannot become the activation sample.
        QTRY_VERIFY(drag->property("active").toBool());
        QTest::touchEvent(h.window,device).move(0,pos+QPoint(60,40),h.window).commit();QTest::qWait(20);
        QTRY_VERIFY(drag->property("activeTranslation").value<QVector2D>().lengthSquared()>4);
        if(ending==1){
            QVERIFY(drag->setProperty("enabled",false));
            QTest::touchEvent(h.window,device).release(0,pos+QPoint(60,40),h.window).commit();
            QVERIFY(drag->setProperty("enabled",true));
        } else if(ending==2) {
            QTest::touchEvent(h.window,device).move(0,pos,h.window).commit();QTest::qWait(20);
            QVERIFY(drag->property("activeTranslation").value<QVector2D>().isNull());
            QTest::touchEvent(h.window,device).release(0,pos,h.window).commit();
        } else {
            QTouchEvent cancel(QEvent::TouchCancel,device);
            QGuiApplication::sendEvent(h.window,&cancel);
            QTest::touchEvent(h.window,device).release(0,pos+QPoint(60,40),h.window).commit();
        }
        QTRY_VERIFY(!drag->property("active").toBool());
        QCOMPARE(h.editor.documentBytes(),before);QCOMPARE(presentation.count(),0);
        QTest::touchEvent(h.window,device).press(0,pos,h.window).commit();
        QTest::touchEvent(h.window,device).release(0,pos,h.window).commit();
        QCOMPARE(h.editor.documentBytes(),before);QCOMPARE(presentation.count(),0);
        h.editor.clearSelection();
        // A fresh gesture after each interrupted/no-op path must still work.
        QTRY_VERIFY(placedLabel(h.window->contentItem(),"A"));
        label=placedLabel(h.window->contentItem(),"A");
        auto* nextDrag=labelDragHandler(label);QVERIFY(nextDrag);
        const auto nextPos=label->mapToScene(QPointF(label->width()/2,label->height()/2)).toPoint();
        QTest::touchEvent(h.window,device).press(0,nextPos,h.window).commit();
        QTest::touchEvent(h.window,device).move(0,nextPos+QPoint(20,-15),h.window).commit();QTest::qWait(20);
        QTRY_VERIFY(nextDrag->property("active").toBool());
        QTest::touchEvent(h.window,device).move(0,nextPos+QPoint(35,-25),h.window).commit();QTest::qWait(20);
        QTRY_VERIFY(nextDrag->property("activeTranslation").value<QVector2D>().lengthSquared()>4);
        QTest::touchEvent(h.window,device).release(0,nextPos+QPoint(35,-25),h.window).commit();
        QTRY_VERIFY_WITH_TIMEOUT(settings(h.editor).pinned,1000);
        QVERIFY(settings(h.editor).manualPosition);QCOMPARE(presentation.count(),1);
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join("\n")));
    }
    void latestSelectionEntry_data(){modes();}
    void latestSelectionEntry(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);h.editor.clearSelection();
        auto map=h.control("mapView");QVERIFY(map);const auto camera=h.editor.mapViewState();
        QPoint labelPoint;
        for(const auto row:h.editor.placedLabels()){const auto label=row.toMap();if(label["ref"].toMap()["id"]=="A"){labelPoint=map->mapToScene(QPointF(label["x"].toDouble(),label["y"].toDouble())).toPoint();break;}}
        if(!mobile){
            const auto revision=h.editor.revision();QTest::mouseMove(h.window,labelPoint,50);
            QTRY_VERIFY(h.control("territorialToolbar")->isVisible());QVERIFY(h.editor.selectionItems().isEmpty());QCOMPARE(h.editor.revision(),revision);QVERIFY(!h.control("objectPropertyPanel")->isVisible());
              QVERIFY(h.click("selectionLockButton"));QVERIFY(h.editor.objectProperties()["locked"].toBool());QVERIFY(!h.control("objectPropertyPanel")->isVisible());
              QVERIFY(h.click("selectionLockButton"));QVERIFY(!h.editor.objectProperties()["locked"].toBool());
              QVERIFY(h.click("selectionVisibilityButton"));QVERIFY(!h.editor.countryVisuals()["A"].toMap()["visible"].toBool());QVERIFY(!h.control("objectPropertyPanel")->isVisible());
              QVERIFY(h.click("selectionVisibilityButton"));QVERIFY(h.editor.countryVisuals()["A"].toMap()["visible"].toBool());
            const auto captures=qEnvironmentVariable("PANDOEDITOR_UI_CAPTURE_DIR");if(!captures.isEmpty()){QVERIFY(QDir().mkpath(captures));QVERIFY(h.window->grabWindow().save(captures+"/step4-hover-desktop.png"));}
            QTest::mouseMove(h.window,QPoint(0,0),50);QTRY_VERIFY(!h.control("territorialToolbar")->isVisible());
        }
        if(mobile){static auto device=QTest::createTouchDevice();QTest::touchEvent(h.window,device).press(0,labelPoint,h.window).commit();QTest::touchEvent(h.window,device).release(0,labelPoint,h.window).commit();}else QTest::mouseClick(h.window,Qt::LeftButton,Qt::NoModifier,labelPoint);QTRY_COMPARE(h.editor.selectedId(),QString("A"));QTRY_VERIFY(h.control("objectPropertyPanel")->isVisible());QCOMPARE(h.editor.mapViewState(),camera);
        QVERIFY(h.control("territorialToolbar")->property("editorOpen").toBool());QCOMPARE(h.control("selectionCardName")->property("text").toString(),QString("Alpha"));
        QVERIFY(h.click("flagMenuButton"));auto menu=h.window->findChild<QObject*>("flagMenu");QVERIFY(menu);QTRY_VERIFY(menu->property("visible").toBool());QVERIFY(h.control("flagLibraryButton")->hasActiveFocus());
        QTest::keyClick(h.window,Qt::Key_Escape);QTRY_VERIFY(!menu->property("visible").toBool());QVERIFY(h.control("flagMenuButton")->hasActiveFocus());
        QVERIFY(h.click("flagMenuButton"));QVERIFY(h.click("flagRemoveButton"));auto document=projectcodec::decode(h.editor.documentBytes());QCOMPARE(document.symbols.at(territorialRef("A")).policy,FlagPolicy::None);
        QVERIFY(h.click("flagMenuButton"));QVERIFY(!h.control("flagRemoveButton")->isEnabled());QVERIFY(h.click("flagDefaultButton"));
        QVERIFY(!h.editor.flagLibrary().isEmpty());QVERIFY(h.click("flagMenuButton"));QVERIFY(h.click("flagLibraryButton"));auto gallery=h.window->findChild<QObject*>("flagLibraryDialog");QVERIFY(gallery);QTRY_VERIFY(gallery->property("visible").toBool());QVERIFY(h.enter("flagLibrarySearch",h.editor.flagLibrary().front().toMap()["code"].toString(),false));QTRY_VERIFY(h.control("flagLibraryEntry_"+h.editor.flagLibrary().front().toMap()["code"].toString()));QVERIFY(h.click("flagLibraryEntry_"+h.editor.flagLibrary().front().toMap()["code"].toString()));QVERIFY(h.click("flagLibraryApply"));
          QTRY_VERIFY(!gallery->property("visible").toBool());document=projectcodec::decode(h.editor.documentBytes());QCOMPARE(document.symbols.at(territorialRef("A")).policy,FlagPolicy::Embedded);QVERIFY(!document.symbols.at(territorialRef("A")).embeddedDataUrl.empty());
          QTRY_COMPARE(h.control("selectionCardFlag")->property("status").toInt(),1);
        const auto captures=qEnvironmentVariable("PANDOEDITOR_UI_CAPTURE_DIR");if(!captures.isEmpty()){QVERIFY(QDir().mkpath(captures));QVERIFY(h.window->grabWindow().save(captures+(mobile?"/step4-direct-mobile.png":"/step4-direct-desktop.png")));}
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join("\n")));
    }
    void libraryPresentationWorkflow_data(){modes();}
    void libraryPresentationWorkflow(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);
        QFile file(h.dir.filePath("library.json"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(R"({"schemaVersion":2,"entities":[{"libraryId":"historical-country:fixture","type":"country","canonicalName":"Historical Fixture","geometryVersions":[{"id":"v1","geometry":{"type":"Polygon","coordinates":[[[70,0],[72,0],[72,2],[70,2],[70,0]]]}}]}],"snapshots":[]})");file.close();
        QVERIFY(h.editor.loadHistoricalLibrary(QUrl::fromLocalFile(file.fileName())));QVERIFY(h.click("historicalLibraryButton"));
        auto panel=h.window->findChild<QObject*>("historicalLibraryPanel");QVERIFY(panel);QTRY_VERIFY(panel->property("visible").toBool());QVERIFY(panel->property("width").toDouble()<=760);
        const auto before=h.editor.documentBytes();QVERIFY(h.control("historicalSelect_historical-country:fixture")->property("text").toString().contains(QStringLiteral("국가")));QVERIFY(h.click("historicalSelect_historical-country:fixture"));QVERIFY(h.editor.historicalPreview()["name"]=="Historical Fixture");
        QVERIFY(!h.control("historicalGeometryVersion"));
        QVERIFY(h.enter("historicalSearch","No matching fixture",false));
        QTRY_VERIFY(panel->property("selectedId").toString().isEmpty());QVERIFY(!h.control("historicalAddButton")->isEnabled());
        QVERIFY(h.click("historicalSearchClear"));QVERIFY(h.control("historicalSearch")->hasActiveFocus());
        QVERIFY(h.enter("historicalSearch","Historical Fixture",false));
        QTest::keyClick(h.window,Qt::Key_Down);QTest::keyClick(h.window,Qt::Key_Return);
        QTRY_COMPARE(panel->property("selectedId").toString(),QString("historical-country:fixture"));
        QVERIFY(h.click("historicalAddButton"));QTRY_COMPARE(h.editor.historicalStage(),QString("impact"));QCOMPARE(h.editor.documentBytes(),before);
        const auto captures=qEnvironmentVariable("PANDOEDITOR_UI_CAPTURE_DIR");if(!captures.isEmpty()){QVERIFY(QDir().mkpath(captures));QVERIFY(h.window->grabWindow().save(captures+(mobile?"/step4-library-mobile.png":"/step4-library-desktop.png")));}
        QMetaObject::invokeMethod(panel,"close");QCOMPARE(h.editor.documentBytes(),before);QVERIFY(h.click("historicalLibraryButton"));QVERIFY(h.control("historicalSelect_historical-country:fixture")->property("text").toString().contains(QStringLiteral("국가")));QVERIFY(h.click("historicalSelect_historical-country:fixture"));QVERIFY(h.click("historicalAddButton"));QTRY_COMPARE(h.editor.historicalStage(),QString("impact"));QVERIFY(h.click("historicalAddButton"));QVERIFY(h.editor.documentBytes()!=before);h.editor.undo();QCOMPARE(h.editor.documentBytes(),before);
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join("\n")));
    }
    void auxiliarySheetGestures(){
        Harness h(true);QVERIFY(h.window);const auto camera=h.editor.mapViewState();
        const QStringList triggers={"searchTab","mapDisplayButton","createMenuButton"};
        const QStringList handles={"searchSheetHandle","viewSheetHandle","createSheetHandle"};
        const QStringList surfaces={"objectSearchSurface","mapDisplayPopup","createMenu"};
        for(int i=0;i<triggers.size();++i){
            QVERIFY(h.click(triggers[i]));auto handle=h.control(handles[i]);QVERIFY(handle&&handle->isVisible());
            auto surface=h.window->findChild<QObject*>(surfaces[i]);QVERIFY(surface);
            handle->forceActiveFocus();QTest::keyClick(h.window,Qt::Key_Home);QTest::qWait(220);
            QVERIFY(std::abs(surface->property("height").toDouble()-84)<1);
            QTest::keyClick(h.window,Qt::Key_End);QTest::qWait(220);QVERIFY(surface->property("height").toDouble()>400);
            QCOMPARE(h.editor.mapViewState(),camera);
            QVERIFY(QMetaObject::invokeMethod(h.window,"handleBack"));QTest::qWait(50);
        }
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join("\n")));
    }
    void commonPresentationAndSheets_data(){modes();}
    void commonPresentationAndSheets(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);h.editor.selectCountry("A");
        QVERIFY(h.control("selectionCardName"));QCOMPARE(h.control("selectionCardName")->property("text").toString(),QString("Alpha"));
        QVERIFY(h.control("objectPropertyPanel")->isVisible());QVERIFY(h.click("openObjectEditor"));
        const auto camera=h.editor.mapViewState();auto side=h.control("workspaceSidePanel");
        if(mobile){
            auto handle=h.control("workspaceSheetHandle");QVERIFY(handle&&handle->isVisible());
            double heights[3];
            for(int level=0;level<3;++level){handle->forceActiveFocus();QTest::keyClick(h.window,Qt::Key_Down);QTest::keyClick(h.window,Qt::Key_Down);for(int i=0;i<level;++i)QTest::keyClick(h.window,Qt::Key_Up);QTest::qWait(220);heights[level]=side->height();QCOMPARE(h.editor.mapViewState(),camera);}
            QVERIFY(heights[0]<heights[1]&&heights[1]<heights[2]);
            const auto pos=handle->mapToScene(QPointF(handle->width()/2,handle->height()/2)).toPoint();
            QTest::mousePress(h.window,Qt::LeftButton,Qt::NoModifier,pos);QTest::mouseMove(h.window,pos+QPoint(0,10),30);QTest::mouseMove(h.window,pos+QPoint(0,180),50);QTest::mouseRelease(h.window,Qt::LeftButton,Qt::NoModifier,pos+QPoint(0,180));
            QTRY_COMPARE(h.control("desktopWorkspace")->property("sheetLevel").toInt(),1);
        }
        h.editor.selectObject(ref("S","general"));QVERIFY(h.click("objectRelationsTab"));auto parent=h.control("relationParent");QVERIFY(parent);QCOMPARE(parent->property("text").toString(),QStringLiteral("상위 단위: Alpha"));
        QVERIFY(h.click("toggleObjectEditor"));QVERIFY(h.click("preferencesButton"));
        auto preferences=h.window->findChild<QObject*>("appearancePreferencesDialog");QVERIFY(preferences);QTRY_VERIFY(preferences->property("visible").toBool());
        const auto theme=h.editor.appearancePreferences()["theme"];
        QVERIFY(h.click("themeDarkButton"));QCOMPARE(h.editor.appearancePreferences()["theme"].toString(),QString("dark"));
        QVERIFY(h.click("accentPurpleButton"));QCOMPARE(h.editor.appearancePreferences()["accentPreset"].toString(),QString("purple"));
        QVERIFY(h.click("preferencesCancelButton"));QCOMPARE(h.editor.appearancePreferences()["theme"],theme);
        QVERIFY(h.click("helpButton"));QTRY_VERIFY(h.window->findChild<QObject*>("helpPopup")->property("visible").toBool());
        auto help=h.window->findChild<QObject*>("helpPopup");QMetaObject::invokeMethod(help,"close");
        QVERIFY(QMetaObject::invokeMethod(h.window,"notify",Q_ARG(QVariant,QVariant("검증 알림")),Q_ARG(QVariant,QVariant("error"))));
        auto notice=h.window->findChild<QObject*>("errorDialog");QVERIFY(notice);QTRY_VERIFY(notice->property("visible").toBool());QVERIFY(!notice->property("modal").toBool());
        QVERIFY(h.click("notificationDismiss"));QTRY_VERIFY(!notice->property("visible").toBool());
        const auto captures=qEnvironmentVariable("PANDOEDITOR_UI_CAPTURE_DIR");if(!captures.isEmpty()){QVERIFY(QDir().mkpath(captures));h.editor.selectCountry("A");QVERIFY(h.window->grabWindow().save(captures+(mobile?"/step4-card-mobile.png":"/step4-card-desktop.png")));}
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join("\n")));
    }
    void gisPresentationWorkflow_data(){modes();}
    void gisPresentationWorkflow(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);
        QFile file(h.dir.filePath("test.geojson"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(R"({"type":"FeatureCollection","features":[{"type":"Feature","id":"Imported","properties":{"name":"Imported country"},"geometry":{"type":"Polygon","coordinates":[[[70,0],[72,0],[72,2],[70,2],[70,0]]]}}]})");file.close();
        const auto before=h.editor.documentBytes();QVERIFY(h.click("gisImportButton"));QVERIFY(h.editor.loadGisSource(QUrl::fromLocalFile(file.fileName())));QTRY_COMPARE(h.editor.gisImportState()["stage"].toString(),QString("mapping"));
        auto choice=h.control("gisTargetChoice");QVERIFY(choice);choice->setProperty("currentIndex",0);QTest::qWait(60);QCOMPARE(choice->property("currentText").toString(),QStringLiteral("일반 객체 · 독립"));
        QVERIFY(h.click("gisPrepare"));QTRY_COMPARE(h.editor.gisImportState()["stage"].toString(),QString("impact"));QCOMPARE(h.control("gisImportStep")->property("text").toString(),QStringLiteral("3 · 결과 검토"));QCOMPARE(h.editor.documentBytes(),before);
        const auto captures=qEnvironmentVariable("PANDOEDITOR_UI_CAPTURE_DIR");if(!captures.isEmpty()){QVERIFY(QDir().mkpath(captures));QVERIFY(h.window->grabWindow().save(captures+(mobile?"/step4-gis-mobile.png":"/step4-gis-desktop.png")));}
        const auto oldSession=h.editor.gisImportState()["session"].toULongLong();QVERIFY(h.click("gisReviewBack"));QCOMPARE(h.editor.gisImportState()["stage"].toString(),QString("mapping"));QVERIFY(!h.editor.confirmGisImport(oldSession));QCOMPARE(h.editor.documentBytes(),before);QVERIFY(h.click("gisPrepare"));QTRY_COMPARE(h.editor.gisImportState()["stage"].toString(),QString("impact"));
        QVERIFY(h.click("gisConfirm"));const auto after=h.editor.documentBytes();QVERIFY(after!=before);h.editor.undo();QCOMPARE(h.editor.documentBytes(),before);h.editor.redo();QCOMPARE(h.editor.documentBytes(),after);
        QVERIFY(h.click("gisImportButton"));QVERIFY(h.editor.loadGisSource(QUrl::fromLocalFile(file.fileName())));QTRY_COMPARE(h.editor.gisImportState()["stage"].toString(),QString("mapping"));QMetaObject::invokeMethod(h.window->findChild<QObject*>("gisImportPanel"),"close");QCOMPARE(h.editor.gisImportState()["stage"].toString(),QString("empty"));QCOMPARE(h.editor.documentBytes(),after);
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join("\n")));
    }
    void geometryTaskWorkflow_data(){modes();}
    void geometryTaskWorkflow(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);h.editor.selectCountry("A");
        const auto before=h.editor.documentBytes();
        QVERIFY(h.editor.beginGeometryDraw());QTest::qWait(100);
        auto dock=h.control("geometryToolDock");QVERIFY(dock);QVERIFY(dock->isVisible());
        QCOMPARE(h.control("geometryTaskName")->property("text").toString(),QStringLiteral("다시 그리기"));
        QVERIFY(h.editor.geometryEditState()["targets"].toList().front().toMap()["name"]=="Alpha");
        QVERIFY(h.click("geometryPreview"));QVERIFY(!h.editor.geometryEditState()["error"].toString().isEmpty());
        QVERIFY(h.control("geometryTaskError")->isVisible());
        const auto error=h.editor.geometryEditState()["error"];
        QVERIFY(h.click("geometryMinimize"));QVERIFY(dock->property("minimized").toBool());
        QVERIFY(h.editor.geometryEditState()["active"].toBool());QCOMPARE(h.editor.geometryEditState()["error"],error);
        QVERIFY(h.click("geometryMinimize"));QVERIFY(!dock->property("minimized").toBool());
        MapProjection projection;projection.rebuild(fixture());
        for(const auto point:{Point{0,0},Point{9,0},Point{9,9},Point{0,9}}){const auto xy=projection.project(point);QVERIFY(h.editor.geometryAddPoint(xy.x,xy.y,0));}
        const auto draft=h.editor.geometryDraftPaths();
        QVERIFY(h.click("geometryMinimize"));QCOMPARE(h.editor.geometryDraftPaths(),draft);
        QVERIFY(h.click("geometryMinimize"));QCOMPARE(h.editor.geometryDraftPaths(),draft);
        QVERIFY(h.click("geometryPreview"));QTRY_VERIFY(h.editor.geometryEditState()["previewReady"].toBool());
        QVERIFY(h.control("geometryConfirm")->isVisible());QVERIFY(!h.control("geometryDeleteVertex")->isVisible());
        QVERIFY(h.click("geometryMinimize"));QVERIFY(h.editor.geometryEditState()["previewReady"].toBool());
        QVERIFY(h.click("geometryMinimize"));QVERIFY(h.editor.geometryEditState()["previewReady"].toBool());
        QVERIFY(h.click("geometryBack"));QCOMPARE(h.editor.geometryDraftPaths(),draft);
        QVERIFY(h.click("geometryPreview"));QTRY_VERIFY(h.editor.geometryEditState()["previewReady"].toBool());
        const auto captures=qEnvironmentVariable("PANDOEDITOR_UI_CAPTURE_DIR");
        if(!captures.isEmpty()){QVERIFY(QDir().mkpath(captures));QVERIFY(h.window->grabWindow().save(captures+(mobile?"/step3-review-mobile.png":"/step3-review-desktop.png")));}
        QVERIFY(h.click("geometryConfirm"));QVERIFY(!h.editor.geometryEditState()["active"].toBool());
        const auto after=h.editor.documentBytes();QVERIFY(after!=before);
        h.editor.undo();QCOMPARE(h.editor.documentBytes(),before);
        h.editor.redo();QCOMPARE(h.editor.documentBytes(),after);
        h.editor.selectCountry("A");QVERIFY(h.editor.beginMergeSelection());QTest::qWait(80);
        QVERIFY(!h.control("geometryDeleteVertex")->isVisible());QVERIFY(!h.control("geometryPreview")->isEnabled());
        QVERIFY(h.editor.geometryToggleProvider(ref("B")));QTest::qWait(80);
        QVERIFY(h.click("geometryRemoveProvider_B"));QVERIFY(h.editor.geometryEditState()["providers"].toList().isEmpty());
        QVERIFY(h.editor.geometryToggleProvider(ref("B")));QVERIFY(h.click("geometryBack"));
        QCOMPARE(h.editor.geometryEditState()["stage"].toString(),QString("setup"));
        QVERIFY(h.click("geometryAdvance"));QCOMPARE(h.editor.geometryEditState()["providers"].toList().size(),1);
        QVERIFY(h.click("geometryMinimize"));QVERIFY(dock->property("minimized").toBool());
        QVERIFY(h.click("geometryMinimize"));QVERIFY(h.click("geometryCancel"));QVERIFY(!h.editor.geometryEditState()["active"].toBool());
        QVERIFY(h.editor.beginSplitGeometry());QTest::qWait(80);QVERIFY(!dock->property("minimized").toBool());
        QVERIFY(h.editor.geometryEditState()["territorySelection"].toBool());QVERIFY(!h.control("geometryDeleteVertex")->isVisible());
        QVERIFY(h.editor.geometryAdvanceStage());QVERIFY(h.editor.geometrySelectTerritoryMethod("line"));QTRY_VERIFY_WITH_TIMEOUT(!h.editor.geometryEditState()["calculating"].toBool(),10000);
        for(const auto point:{Point{2,-1},Point{2,10}}){const auto xy=projection.project(point);QVERIFY(h.editor.geometryAddPoint(xy.x,xy.y,0));}
        QVERIFY(h.click("geometryUndoDraft"));QCOMPARE(h.editor.geometryDraftPaths().front().toMap()["vertices"].toList().size(),1);
        QVERIFY(h.click("geometryCancel"));QCOMPARE(h.editor.documentBytes(),after);
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join("\n")));
    }
    void splitDynamicCandidates_data(){modes();}
    void splitDynamicCandidates(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);h.editor.selectCountry("B");
        const auto before=h.editor.documentBytes();MapProjection projection;projection.rebuild(projectcodec::decode(before));
        QVERIFY(h.editor.beginSplitGeometry());QVERIFY(h.click("geometryAdvance"));QVERIFY(h.click("geometryMethod_line"));
        QTRY_VERIFY_WITH_TIMEOUT(!h.editor.geometryEditState()["calculating"].toBool(),10000);
        for(const auto point:{Point{19,2},Point{31,2},Point{31,5},Point{19,5},Point{19,8},Point{31,8}}){const auto xy=projection.project(point);QVERIFY(h.editor.geometryAddPoint(xy.x,xy.y,0));}
        QVERIFY(h.click("geometryFinishDraft"));
        QTRY_VERIFY_WITH_TIMEOUT(!h.editor.geometryEditState()["calculating"].toBool(),15000);
        QVERIFY2(h.editor.geometryEditState()["error"].toString().isEmpty(),qPrintable(h.editor.geometryEditState()["error"].toString()));
        const auto candidates=h.editor.geometryEditState()["candidates"].toList();QCOMPARE(candidates.size(),4);QString picked;
        for(const auto candidate:candidates)if(!candidate.toMap()["selected"].toBool()){picked=candidate.toMap()["id"].toString();break;}
        QVERIFY(h.click("geometryCandidate_"+picked));QTRY_VERIFY_WITH_TIMEOUT(h.editor.geometryEditState()["canAddPart"].toBool(),15000);
        QCOMPARE(h.editor.geometryEditState()["selectedCandidateIds"].toList().size(),2);
        QVERIFY(h.click("geometryArchivePart"));QTRY_VERIFY_WITH_TIMEOUT(h.editor.geometryEditState()["canAdvance"].toBool(),15000);
        QVERIFY(h.click("geometryReview"));QCOMPARE(h.editor.geometryEditState()["stage"].toString(),QString("review"));
        QVERIFY(h.click("geometryBack"));QCOMPARE(h.editor.geometryEditState()["stage"].toString(),QString("selection"));
        QTRY_VERIFY_WITH_TIMEOUT(h.editor.geometryEditState()["canAdvance"].toBool(),15000);QVERIFY(h.click("geometryReview"));
        QVERIFY(h.click("geometryConfirm"));QTRY_VERIFY_WITH_TIMEOUT(!h.editor.geometryEditState()["active"].toBool(),15000);
        QVERIFY(h.editor.selectedId()!="B");const auto after=h.editor.documentBytes();QVERIFY(after!=before);
        h.editor.undo();QCOMPARE(h.editor.documentBytes(),before);h.editor.redo();QCOMPARE(h.editor.documentBytes(),after);
        h.editor.selectCountry("B");QVERIFY(h.editor.beginSplitGeometry());QVERIFY(h.click("geometryCancel"));QCOMPARE(h.editor.documentBytes(),after);
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join("\n")));
    }
    void overlayAndHierarchicalMenus_data(){modes();}
    void overlayAndHierarchicalMenus(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);h.editor.selectCountry("A");
        QVERIFY(h.editor.setProjectionMode("flat"));
        auto map=h.control("mapView");QVERIFY(map);
        QVERIFY(h.editor.zoomMapCameraAt(1.2,map->width()*.65,map->height()*.4));
        h.editor.beginMapCameraPan();QVERIFY(h.editor.updateMapCameraPan(11,-7));h.editor.endMapCameraPan();
        const auto camera=h.editor.mapViewState();const QSizeF size(map->width(),map->height());
        for(int pass=0;pass<3;++pass){
            QVERIFY(h.click("openObjectEditor"));
            QCOMPARE(QSizeF(map->width(),map->height()),size);QCOMPARE(h.editor.mapViewState(),camera);
            auto side=h.control("workspaceSidePanel");auto bar=h.control("mapCommandToolbar");
            const auto rect=bar->mapRectToScene(QRectF(0,0,bar->width(),bar->height()));
            if(mobile)QVERIFY(rect.bottom()<side->mapToScene({0,0}).y());
            // A blank area of the overlay must consume the click rather than
            // clearing selection through the map below.
            QTest::mouseClick(h.window,Qt::LeftButton,Qt::NoModifier,side->mapToScene({12,12}).toPoint());
            QCOMPARE(h.editor.selectedId(),QString("A"));QCOMPARE(h.editor.mapViewState(),camera);
            QVERIFY(h.click("toggleObjectEditor"));
            QCOMPARE(QSizeF(map->width(),map->height()),size);QCOMPARE(h.editor.mapViewState(),camera);
        }
        QVERIFY(h.click("mapDisplayButton"));
        auto popup=h.window->findChild<QObject*>("mapDisplayPopup");QVERIFY(popup);
        QVERIFY(popup->property("visible").toBool());QVERIFY(!popup->property("modal").toBool());QVERIFY(!popup->property("dim").toBool());
        QVERIFY(h.click("viewGroup_countries"));QCOMPARE(popup->property("section").toString(),QString("countries"));
        const auto before=h.editor.documentBytes();
        QVERIFY(h.click("viewNames"));QVERIFY(h.editor.documentBytes()!=before);
        QTest::keyClick(h.window,Qt::Key_Escape);QTest::qWait(100);
        QVERIFY(popup->property("visible").toBool());QCOMPARE(popup->property("section").toString(),QString());
        QVERIFY(h.control("viewProjectionMenu")->property("activeFocus").toBool());
        QTest::keyClick(h.window,Qt::Key_Right);QTest::qWait(100);
        QCOMPARE(popup->property("section").toString(),QString("projection"));
        QVERIFY(h.control("viewMenuBack")->property("activeFocus").toBool());
        QVERIFY(h.click("projectionGlobeButton"));
        QCOMPARE(h.editor.projectionMode(),QString("globe"));
        QVERIFY(h.click("viewMenuBack"));QVERIFY(h.click("viewTerrainMenu"));QVERIFY(h.click("terrainNoneButton"));
        QCOMPARE(h.editor.terrainMode(),QString("none"));
        QVERIFY(h.click("fileMenuButton"));QVERIFY(!popup->property("visible").toBool());
        auto fileMenu=h.window->findChild<QObject*>("fileMenu");QVERIFY(fileMenu&&fileMenu->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(h.window,"handleBack"));QTest::qWait(100);
        QVERIFY(h.click("mapDisplayButton"));QCOMPARE(popup->property("section").toString(),QString());
        const auto captureDir=qEnvironmentVariable("PANDOEDITOR_UI_CAPTURE_DIR");
        if(!captureDir.isEmpty()){
            QVERIFY(QDir().mkpath(captureDir));
            QVERIFY(h.window->grabWindow().save(captureDir+(mobile?"/step2-view-mobile.png":"/step2-view-desktop.png")));
        }
        QVERIFY(QMetaObject::invokeMethod(h.window,"handleBack"));QTest::qWait(100);QVERIFY(!popup->property("visible").toBool());
        QVERIFY(h.click("openObjectEditor"));
        if(!captureDir.isEmpty())QVERIFY(h.window->grabWindow().save(captureDir+(mobile?"/step2-overlay-mobile.png":"/step2-overlay-desktop.png")));
        const auto preserved=h.editor.documentBytes();
        QVERIFY(h.click("newProjectButton"));QVERIFY(h.window->findChild<QObject*>("unsavedDialog")->property("visible").toBool());
        QVERIFY(h.click("cancelUnsaved"));QCOMPARE(h.editor.documentBytes(),preserved);
        QVERIFY(h.click("newProjectButton"));QVERIFY(h.click("discardUnsaved"));
        QVERIFY(!h.editor.hasFile());QVERIFY(!h.control("objectPropertyPanel")->isVisible());QVERIFY(h.editor.selectedId().isEmpty());
        h.window->resize(mobile?360:800,560);QTest::qWait(150);
        h.editor.selectCountry("DEU");
        const auto narrowCamera=h.editor.mapViewState();
        QVERIFY(h.click("openObjectEditor"));QCOMPARE(h.editor.mapViewState(),narrowCamera);
        auto narrowSide=h.control("workspaceSidePanel");auto narrowBar=h.control("mapCommandToolbar");
        auto barRect=narrowBar->mapRectToScene(QRectF(0,0,narrowBar->width(),narrowBar->height()));
        if(!mobile)QVERIFY(barRect.left()>=narrowSide->width());
        else QVERIFY(barRect.bottom()<narrowSide->mapToScene({0,0}).y());
        QVERIFY(h.click("toggleObjectEditor"));QCOMPARE(h.editor.mapViewState(),narrowCamera);
        for(const auto& route:QStringList{"addPlaceLabel","addRiver","addLake","addDistributionLayer","addDistributionEntry"}){
            QVERIFY(h.click(route));QTRY_VERIFY(h.editor.contentEditState()["active"].toBool());
            QVERIFY(h.editor.contentEditState()["create"].toBool());
            QVERIFY(h.control("objectPropertyPanel")->isVisible());QVERIFY(!h.control("desktopWorkspace")->property("legacyOpen").toBool());
            h.editor.cancelContentEdit();
        }
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
    }
    void integratedContentFields_data(){modes();}
    void integratedContentFields(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);
        h.editor.selectCountry("A");
        QVERIFY(h.click("openObjectEditor"));
        QVERIFY(h.click("contentCapital"));
        QVERIFY(h.enter("contentField_capital","New capital"));
        QCOMPARE(h.editor.contentEditState()["capital"].toString(),QString("New capital"));
        QVERIFY(!h.editor.hasPendingEdits());
        QVERIFY(h.click("contentCancel"));
        QVERIFY(h.click("flagMenuButton"));
        QVERIFY(h.click("flagRemoveButton"));
        auto saved=projectcodec::decode(h.editor.documentBytes());
        QCOMPARE(saved.countryDetails.at(territorialRef("A")).capital,std::string("New capital"));
        QCOMPARE(saved.symbols.at(territorialRef("A")).policy,FlagPolicy::None);
        // Create through the canonical command, then use the integrated field
        // without a second native panel or explicit beginContentEdit call.
        QVERIFY(h.editor.beginContentEdit("distributionLayer","",true));
        QVERIFY(h.editor.updateContentField("name","Population"));
        QVERIFY(h.editor.previewContentEdit(false));QVERIFY(h.editor.confirmContentEdit());
        saved=projectcodec::decode(h.editor.documentBytes());
        const auto layer=QString::fromStdString(saved.distributionLayers.back().id);
        QVERIFY(h.editor.selectObject({{"domain","distributionLayer"},{"id",layer}}));
        QTRY_VERIFY(h.editor.contentEditState()["active"].toBool());
        QCOMPARE(h.editor.contentEditState()["id"].toString(),layer);
        QVERIFY(h.enter("contentField_unit","people"));
        QVERIFY(!h.editor.hasPendingEdits());
        auto before=h.editor.documentBytes();
        QVERIFY(h.enter("contentField_unit","discarded",false));
        h.editor.selectCountry("B");
        QTRY_VERIFY(!h.editor.contentEditState()["active"].toBool());
        QCOMPARE(h.editor.documentBytes(),before);
        QVERIFY(h.editor.selectObject({{"domain","distributionLayer"},{"id",layer}}));
        QTRY_COMPARE(h.editor.contentEditState()["unit"].toString(),QString("people"));
        h.editor.clearSelection();
        QTRY_VERIFY(!h.editor.contentEditState()["active"].toBool());
        QCOMPARE(h.editor.documentBytes(),before);
        QVERIFY(h.editor.selectObject({{"domain","distributionLayer"},{"id",layer}}));
        QTRY_VERIFY(h.editor.contentEditState()["active"].toBool());
        QVERIFY(h.click("contentCancel"));
        QVERIFY(!h.editor.contentEditState()["active"].toBool());
        QVERIFY(h.click("toggleObjectEditor"));QVERIFY(h.click("openObjectEditor"));
        QTRY_COMPARE(h.editor.contentEditState()["unit"].toString(),QString("people"));
        QVERIFY(h.enter("contentField_unit","saved draft",false));
        QVERIFY(h.editor.saveFile(QUrl::fromLocalFile(h.dir.filePath("integrated.json"))));
        h.editor.cancelContentEdit();
        QVERIFY(h.editor.openFile(QUrl::fromLocalFile(h.dir.filePath("integrated.json"))));
        saved=projectcodec::decode(h.editor.documentBytes());
        QCOMPARE(saved.distributionLayers.back().unit,std::string("saved draft"));
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
    }
    void integratedObjectKinds_data(){modes();}
    void integratedObjectKinds(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);
        auto d=fixture();Geometry point;point.type="Point";point.points={{1,1}};
        d.geometries.insert({"city-geometry",1},point);
        PlaceLabel city;city.id="city";city.name="City";city.geometry={"city-geometry",1};d.labels.push_back(city);
        HydroFeature lake;lake.id="lake";lake.name="Lake";lake.kind="lake";lake.geometry=staticGeometryBinding(d,d.units.front().id).geometryRef;d.hydro.push_back(lake);
        DistributionLayer layer;layer.id="population";layer.name="Population";layer.unit="people";d.distributionLayers.push_back(layer);
        DistributionEntry entry;entry.id="population-A";entry.layerId=layer.id;entry.territory=territorialRef("A");entry.value=10;d.distributionEntries.push_back(entry);
        Project source;source.replace(d);QFile file(h.dir.filePath("kinds.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));file.write(projectcodec::encode(source));file.close();
        QVERIFY(h.editor.openFile(QUrl::fromLocalFile(file.fileName())));
        QVERIFY(h.editor.selectObject({{"domain","label"},{"id","city"}}));
        QVERIFY(h.click("openObjectEditor"));
        QTRY_COMPARE(h.editor.contentEditState()["domain"].toString(),QString("label"));
        QVERIFY(h.enter("contentField_name","Edited city"));
        QVERIFY(h.enter("contentField_notes","City memo"));
        auto kind=h.control("contentKind");QVERIFY(kind&&kind->isVisible());
        kind->setProperty("currentIndex",1);QVERIFY(QMetaObject::invokeMethod(kind,"activated",Q_ARG(int,1)));
        QVERIFY(h.editor.selectObject({{"domain","hydro"},{"id","lake"}}));
        QTRY_COMPARE(h.editor.contentEditState()["domain"].toString(),QString("hydro"));
        QVERIFY(h.enter("contentField_name","Edited lake"));
        QVERIFY(h.enter("contentField_notes","Lake memo"));
        QVERIFY(h.enter("contentField_color","#123456"));
        QVERIFY(h.control("contentKind")->isVisible());
        QVERIFY(h.editor.selectObject({{"domain","distributionEntry"},{"id","population-A"}}));
        QTRY_COMPARE(h.editor.contentEditState()["domain"].toString(),QString("distributionEntry"));
        QVERIFY(h.enter("contentField_value","42.5"));
        QVERIFY(h.enter("contentField_certainty","high"));
        QVERIFY(h.editor.selectObject({{"domain","distributionLayer"},{"id","population"}}));
        QTRY_COMPARE(h.editor.contentEditState()["domain"].toString(),QString("distributionLayer"));
        QCOMPARE(h.control("selectionCardName")->property("text").toString(),QString("Population"));
        QVERIFY(h.enter("contentField_unit","residents"));
        const auto captureDir=qEnvironmentVariable("PANDOEDITOR_UI_CAPTURE_DIR");
        if(!captureDir.isEmpty()){
            QVERIFY(QDir().mkpath(captureDir));
            QVERIFY(h.window->grabWindow().save(captureDir+(mobile?"/step1-content-mobile.png":"/step1-content-desktop.png")));
        }
        QVERIFY(h.editor.saveFile(QUrl::fromLocalFile(h.dir.filePath("kinds-saved.json"))));
        auto saved=projectcodec::decode(h.editor.documentBytes());
        QCOMPARE(saved.labels.front().name,std::string("Edited city"));
        QCOMPARE(saved.labels.front().kind,std::string("city"));
        QCOMPARE(saved.labels.front().notes,std::string("City memo"));
        QCOMPARE(saved.hydro.front().name,std::string("Edited lake"));
        QCOMPARE(saved.hydro.front().notes,std::string("Lake memo"));
        QCOMPARE(saved.hydro.front().color,std::uint32_t(0x123456));
        QCOMPARE(saved.distributionEntries.front().value,42.5);
        QCOMPARE(saved.distributionEntries.front().certainty,std::string("high"));
        QCOMPARE(saved.distributionLayers.front().unit,std::string("residents"));
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
    }
    void contentPanelSharedCommands_data(){modes();}
    void contentPanelSharedCommands(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY2(h.window,qPrintable(h.warnings.join('\n')));
        h.editor.selectCountry("A");
        QVERIFY(h.click("addDistributionLayer"));
        QVERIFY(h.control("objectPropertyPanel")->isVisible());
        QVERIFY(!h.control("desktopWorkspace")->property("legacyOpen").toBool());
        QVERIFY(h.editor.updateContentField("name",QStringLiteral("언어 분포")));
        const auto before=h.editor.documentBytes();QTest::qWait(50);
        QVERIFY(!h.editor.saveFile(QUrl::fromLocalFile(h.dir.filePath("unconfirmed.json"))));
        QVERIFY(h.click("contentPreview"));QCOMPARE(h.editor.documentBytes(),before);
        QVERIFY(!h.editor.saveFile(QUrl::fromLocalFile(h.dir.filePath("unconfirmed.json"))));
        QVERIFY(h.click("contentConfirm"));QVERIFY(!h.editor.contentEditState()["active"].toBool());
        auto d=projectcodec::decode(h.editor.documentBytes());QCOMPARE(d.distributionLayers.size(),std::size_t(1));
        QCOMPARE(d.distributionLayers.front().name,std::string("언어 분포"));h.editor.undo();QCOMPARE(h.editor.documentBytes(),before);
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
    }
    void objectNamesRenderAsLiteralText_data(){modes();}
    void objectNamesRenderAsLiteralText(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);h.editor.selectCountry("A");
        const QString name="<b>literal</b>";h.editor.setNameDraft(name);QVERIFY(h.editor.commitObjectField("name"));
        QVERIFY(h.click("openObjectEditor"));
        auto field=h.control("detailObjectName");QVERIFY(field&&field->isVisible());
        QCOMPARE(field->property("displayText").toString(),name);
        QCOMPARE(h.control("selectionCardName")->property("text").toString(),name);
        QVERIFY(h.search("literal"));QTRY_VERIFY(h.control("searchSelect_A"));auto row=h.control("searchSelect_A");
        auto content=qvariant_cast<QQuickItem*>(row->property("contentItem"));QVERIFY(content);
        QCOMPARE(content->property("textFormat").toInt(),int(Qt::PlainText));QCOMPARE(h.editor.selectedName(),name);
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
    }
    void backClosesNewEditorBeforeProject_data(){modes();}
    void backClosesNewEditorBeforeProject(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);h.editor.selectCountry("A");
        h.editor.setNameDraft("Unsaved");QVERIFY(h.editor.commitObjectField("name"));
        const auto bytes=h.editor.documentBytes();const auto revision=h.editor.revision();
        QVERIFY(h.click("openObjectEditor"));QVERIFY(h.control("objectPropertyPanel")->isVisible());
        QVERIFY(QMetaObject::invokeMethod(h.window,"handleBack"));
        QVERIFY(!h.control("objectPropertyPanel")->isVisible());QVERIFY(h.window->isVisible());
        QCOMPARE(h.editor.documentBytes(),bytes);QCOMPARE(h.editor.revision(),revision);
        auto unsaved=h.window->findChild<QObject*>("unsavedDialog");QVERIFY(unsaved);QVERIFY(!unsaved->property("visible").toBool());
        QVERIFY(h.click("searchTab"));QVERIFY(h.control("objectSearchField")->isVisible());
        QVERIFY(QMetaObject::invokeMethod(h.window,"handleBack"));QVERIFY(!h.control("objectSearchField")->isVisible());QVERIFY(!unsaved->property("visible").toBool());
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
    }
    void colorKeyboardIsolation_data(){modes();}
    void colorKeyboardIsolation(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);
        h.editor.selectCountry("A");h.editor.setNameDraft("History");QVERIFY(h.editor.commitObjectField("name"));
        const auto bytes=h.editor.documentBytes();const auto revision=h.editor.revision();
        QVERIFY(h.click("objectColorTrigger"));QVERIFY(h.click("customColorButton"));
        auto plane=h.control("colorSVPlane");QVERIFY(plane);plane->forceActiveFocus();
        QTest::keyClick(h.window,Qt::Key_Z,Qt::ControlModifier);QTest::qWait(100);
        QCOMPARE(h.editor.revision(),revision);QCOMPARE(h.editor.documentBytes(),bytes);QVERIFY(h.editor.canUndo());
        auto last=h.control("customColorApply");QVERIFY(last);last->forceActiveFocus();
        QTest::keyClick(h.window,Qt::Key_Tab);
        QVERIFY(h.control("customColorClose"));QVERIFY2(h.control("customColorClose")->hasActiveFocus(),"Tab must wrap within the custom colour controls");
        QTest::keyClick(h.window,Qt::Key_Backtab,Qt::ShiftModifier);QVERIFY(last->hasActiveFocus());
        QTest::keyClick(h.window,Qt::Key_Escape);QVERIFY(!h.editor.colorEditOpen());
        QCOMPARE(h.editor.documentBytes(),bytes);QCOMPARE(h.editor.revision(),revision);
    }

 void originalToolbarAndTypedFields_data(){modes();}
 void originalToolbarAndTypedFields(){
   QFETCH(bool,mobile);Harness h(mobile);QVERIFY2(h.window,qPrintable(h.warnings.join('\n')));h.editor.selectCountry("A");
   QVERIFY(h.control("territorialToolbar")->isVisible());
   QVERIFY(h.control("objectPropertyPanel")->isVisible());
   QVERIFY(h.enter("detailObjectName","  Renamed A  "));QCOMPARE(h.editor.selectedName(),QString("Renamed A"));QCOMPARE(h.editor.revision(),qulonglong(1));
   QVERIFY(h.enter("detailObjectName",""));QCOMPARE(h.editor.selectedName(),QString("Alpha"));QCOMPARE(h.editor.revision(),qulonglong(2));
   h.editor.selectObject(ref("R","regional"));QVERIFY(h.enter("detailObjectName",QStringLiteral("지방 수정")));QCOMPARE(h.editor.selectedName(),QStringLiteral("지방 수정"));
   QVERIFY(h.control("objectPropertyPanel")->isVisible());QVERIFY(!h.control("regionValidFrom")->isVisible());
   h.window->grabWindow().save(mobile?"properties-mobile-360.png":"properties-desktop.png");
   h.editor.selectObject(ref("S","general"));QVERIFY(!h.control("regionValidFrom")->isVisible());QVERIFY(!h.control("detailObjectName")->isEnabled());
   QVERIFY(h.click("objectLockButton"));QVERIFY(h.control("detailObjectName")->isEnabled());QVERIFY(h.enter("detailObjectName","  "));QCOMPARE(h.editor.selectedName(),QStringLiteral("이름 없는 일반객체"));
   QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
 }
 void customColorIsPresentationOnlyUntilApply_data(){modes();}
 void customColorIsPresentationOnlyUntilApply(){
   QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);h.editor.selectCountry("A");
   auto bytes=h.editor.documentBytes();auto rev=h.editor.revision();
   QVERIFY(h.click("objectColorTrigger"));QVERIFY(h.click("customColorButton"));
   QVERIFY(h.enter("customColorHex","#abc",false));QCOMPARE(h.control("customColorEditor")->property("colorValue").toString(),QString("#aabbcc"));
   QCOMPARE(h.editor.documentBytes(),bytes);QCOMPARE(h.editor.revision(),rev);QVERIFY(!h.editor.dirty());
   h.window->grabWindow().save(mobile?"color-mobile-360.png":"color-desktop.png");
   QVERIFY(h.click("customColorCancel",Qt::NoModifier,mobile));QVERIFY(!h.editor.colorEditOpen());QCOMPARE(h.editor.documentBytes(),bytes);
   QVERIFY(h.click("objectColorTrigger"));QVERIFY(h.click("customColorButton"));QVERIFY(h.enter("customColorHex","#xyz",false));QVERIFY(!h.control("customColorApply")->isEnabled());
   QVERIFY(h.enter("customColorHex","#345",false));QVERIFY(h.control("customColorApply")->isEnabled());QVERIFY(h.click("customColorApply",Qt::NoModifier,mobile));
   QCOMPARE(h.editor.colors()["A"].toString(),QString("#334455"));QCOMPARE(h.editor.revision(),rev+1);h.editor.undo();QCOMPARE(h.editor.documentBytes(),bytes);
   QVERIFY(h.click("objectColorTrigger"));QVERIFY(h.click("customColorButton"));
   QTest::keyClick(h.window,Qt::Key_Escape);QTRY_VERIFY(!h.editor.colorEditOpen());QVERIFY(h.editor.canRedo());QVERIFY(h.window->isVisible());
   QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
 }
 void customChannelsKeyboardAndPalette_data(){modes();}
 void customChannelsKeyboardAndPalette(){
   QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);h.editor.selectCountry("A");const auto bytes=h.editor.documentBytes();
   QVERIFY(h.click("objectColorTrigger"));
   auto palette=h.control("webColorPalette");QVERIFY(palette);
   int swatches=0;for(auto c:palette->childItems())if(c->objectName().startsWith("palette"))++swatches;QCOMPARE(swatches,65);
   QVERIFY(h.click("customColorButton"));QVERIFY(h.enter("customColorHex","#ffffff",false));QVERIFY(h.click("formatHsl"));
   QVERIFY(h.enter("customChannel0","137",false));QVERIFY(h.enter("customChannel1","43",false));QVERIFY(h.enter("customChannel2","51",false));
   QCOMPARE(h.control("customChannel0")->property("text").toString(),QString("137"));QCOMPARE(h.control("customChannel1")->property("text").toString(),QString("43"));
   QVERIFY(h.enter("customChannel2","101",false));QVERIFY(!h.control("customColorApply")->isEnabled());
   QVERIFY(h.click("formatRgb"));QVERIFY(h.enter("customChannel0","256",false));QVERIFY(!h.control("customColorApply")->isEnabled());
   QVERIFY(h.enter("customColorHex","#ff0000",false));auto plane=h.control("colorSVPlane");QVERIFY(plane);plane->forceActiveFocus();
   QTest::keyClick(h.window,Qt::Key_Left,Qt::ShiftModifier);QTest::qWait(30);
   const auto hsv=h.control("customColorEditor")->property("hsv").value<QJSValue>().toVariant().toList();QVERIFY(hsv.size()==3);QVERIFY(std::abs(hsv[1].toDouble()-.9)<1e-9);
   QCOMPARE(h.editor.documentBytes(),bytes);QVERIFY(h.click("customColorCancel"));
   QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
 }
 void notesAreIndependentAndLockedReadable_data(){modes();}
 void notesAreIndependentAndLockedReadable(){
   QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);h.editor.selectCountry("A");
   QVERIFY(h.enter("detailObjectName","Notes owner",false));QCOMPARE(h.editor.revision(),qulonglong(0));
   QVERIFY(h.enter("detailObjectNotes",QStringLiteral("  메모\n둘째  "),false));QTRY_COMPARE(h.editor.revision(),qulonglong(1));
   QVERIFY(h.control("detailObjectNotes")->hasActiveFocus());QCOMPARE(h.editor.memoDraft(),QStringLiteral("  메모\n둘째  "));
   QVERIFY(h.click("objectActionsTab"));QCOMPARE(h.editor.revision(),qulonglong(2));QCOMPARE(h.editor.memoDraft(),QStringLiteral("  메모\n둘째  "));
   QVERIFY(h.click("objectLockButton"));QVERIFY(h.click("objectInfoTab"));QVERIFY(!h.control("detailObjectName")->isEnabled());
   QVERIFY(h.control("detailObjectNotes")->property("readOnly").toBool());
   h.editor.selectObject(ref("R","regional"));QVERIFY(h.enter("detailObjectNotes",QStringLiteral("  지방\n메모  "),false));QVERIFY(h.click("objectActionsTab"));QCOMPARE(h.editor.selectedId(),QString("R"));QCOMPARE(h.editor.memoDraft(),QStringLiteral("지방\n메모"));
   QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
 }
 void regionDatesRejectWithoutChangingOtherDrafts_data(){modes();}
 void regionDatesRejectWithoutChangingOtherDrafts(){
   QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);h.editor.selectObject(ref("R","regional"));QVERIFY(h.click("openObjectEditor"));
   QVERIFY(!h.control("regionValidFrom")->isVisible());QVERIFY(!h.control("regionValidTo")->isVisible());
   h.editor.setMemoDraft("keep notes");
   const auto bytes=h.editor.documentBytes();const auto rev=h.editor.revision();
   h.editor.setValidFromDraft("-0001");QVERIFY(!h.editor.commitObjectField("validFrom"));QCOMPARE(h.editor.revision(),rev);QCOMPARE(h.editor.documentBytes(),bytes);QCOMPARE(h.editor.validFromDraft(),QString());QCOMPARE(h.editor.memoDraft(),QString("keep notes"));
   auto error=h.window->findChild<QObject*>("errorDialog");QVERIFY(error);QTRY_VERIFY(error->property("visible").toBool());
   QVERIFY(QMetaObject::invokeMethod(error,"close"));QTest::qWait(200);
   h.editor.setValidFromDraft("0000");QVERIFY(!h.editor.commitObjectField("validFrom"));QCOMPARE(h.editor.revision(),rev);QCOMPARE(h.editor.documentBytes(),bytes);
   QVERIFY(QMetaObject::invokeMethod(error,"close"));QTest::qWait(200);
   h.editor.setValidToDraft("0001-01-01");QVERIFY(!h.editor.commitObjectField("validTo"));QCOMPARE(h.editor.revision(),rev);QCOMPARE(h.editor.documentBytes(),bytes);QCOMPARE(h.editor.validToDraft(),QString());QCOMPARE(h.editor.memoDraft(),QString("keep notes"));
   QVERIFY(QMetaObject::invokeMethod(error,"close"));h.editor.discardPendingEdits();QTest::qWait(50);
   QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
 }
 void multiLockAndSameColorCheckpoint_data(){modes();}
 void multiLockAndSameColorCheckpoint(){
   QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);
   QVERIFY(h.editor.setSelection({ref("A"),ref("S","general")}));QVERIFY(h.editor.objectProperties()["someLocked"].toBool());
   QVERIFY(h.click("openObjectEditor"));QVERIFY(h.click("objectLockButton"));QVERIFY(h.editor.objectProperties()["allLocked"].toBool());
   QVERIFY(h.control("multiObjectColorTrigger")->isEnabled());
   QVERIFY(h.click("multiObjectColorTrigger"));QVERIFY(h.click("customColorButton"));QVERIFY(h.enter("customColorHex","#123456",false));QVERIFY(h.click("customColorApply"));
   QCOMPARE(h.editor.colors()["S"].toString(),QString("#123456"));
   const auto before=h.editor.documentBytes();auto rev=h.editor.revision();
   QVERIFY(h.click("multiObjectColorTrigger"));QVERIFY(h.click("customColorButton"));QVERIFY(h.enter("customColorHex","#123456",false));QVERIFY(h.click("customColorApply"));
   QCOMPARE(h.editor.documentBytes(),before);QCOMPARE(h.editor.revision(),rev+1);h.editor.undo();QCOMPARE(h.editor.documentBytes(),before);QVERIFY(h.editor.canRedo());
   QVERIFY(h.click("objectLockButton"));QVERIFY(!h.editor.objectProperties()["allLocked"].toBool());QVERIFY(!h.editor.canRedo());
   QCOMPARE(ids(h.editor.selectionItems()),QStringList({"A","S"}));
   QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
 }
 void selectionAndBackDoNotLeakColorOrDrafts_data(){modes();}
 void selectionAndBackDoNotLeakColorOrDrafts(){
   QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);h.editor.selectCountry("A");
   QVERIFY(h.enter("detailObjectName","Kept A",false));const auto bytes=h.editor.documentBytes();const auto rev=h.editor.revision();
   QVERIFY(h.search("Beta"));QCOMPARE(h.editor.documentBytes(),bytes);QVERIFY(h.click("searchSelect_B"));QCOMPARE(h.editor.revision(),rev);
   h.editor.selectCountry("A");QCOMPARE(h.editor.nameDraft(),QString("Kept A"));
   QVERIFY(h.click("objectColorTrigger"));QVERIFY(h.click("customColorButton"));QVERIFY(h.enter("customColorHex","#ff00aa",false));
   h.editor.selectCountry("B");QVERIFY(!h.editor.colorEditOpen());QCOMPARE(h.editor.documentBytes(),bytes);
   QVERIFY(h.click("objectColorTrigger"));QVERIFY(h.click("customColorButton"));QVERIFY(QMetaObject::invokeMethod(h.window,"handleBack"));QTRY_VERIFY(!h.editor.colorEditOpen());QVERIFY(h.window->isVisible());
   QCOMPARE(h.editor.documentBytes(),bytes);QCOMPARE(h.editor.revision(),rev);
   QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
 }

};
int main(int argc,char** argv){QQuickStyle::setStyle("Basic");QGuiApplication app(argc,argv);registerWindowsFrameType();PropertyUiTests test;return QTest::qExec(&test,argc,argv);}
#include "property_ui_tests.moc"
