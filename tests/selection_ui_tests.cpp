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
#include <QInputMethodEvent>
#include <memory>
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
        if(!editor.setProjectionMode("flat"))throw std::runtime_error("flat fixture view");
        QObject::connect(&engine,&QQmlEngine::warnings,&engine,[this](const QList<QQmlError>& errors){
            for(const auto& e:errors)warnings<<e.toString();
        });
        engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml"));
        if(!engine.rootObjects().isEmpty())window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        if(window){window->resize(mobile?360:1100,mobile?640:760);window->show();QTest::qWait(150);window->grabWindow();}
    }
    ~Harness(){if(window){window->setProperty("allowClose",true);window->close();}engine.clearComponentCache();}
    QQuickItem* control(const QString& name)const{return window?item(window->contentItem(),name):nullptr;}
    bool click(const QString& name,Qt::KeyboardModifiers mods=Qt::NoModifier,bool touch=false) {
        enterExistingControlRoute(window,name);
        auto c=control(name);if(!c||!c->isVisible()||!c->isEnabled())return false;
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
        QTest::qWait(50);QRectF visibleRect(c->mapToScene(QPointF()),QSizeF(c->width(),c->height()));
        for(auto p=c->parentItem();p;p=p->parentItem())if(p->clip())visibleRect=visibleRect.intersected(QRectF(p->mapToScene(QPointF()),QSizeF(p->width(),p->height())));
        visibleRect=visibleRect.intersected(QRectF(QPointF(),window->size()));if(visibleRect.isEmpty())return false;auto pos=visibleRect.center().toPoint();
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
        QInputMethodEvent event;event.setCommitString(value);QGuiApplication::sendEvent(c,&event);
        if(!QTest::qWaitFor([&]{return editor.searchQuery()==value;},3000))return false;
        window->grabWindow();return true;
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
        QTest::qWait(220); // settle the 170ms sheet transition before checking its hit area
        if(id=="H"){editor.fitMapCamera();QTest::qWait(100);}
        auto panel=control("objectPropertyPanel");if(panel&&panel->isVisible())click("toggleObjectEditor");
        // Let focus/fit requests publish their new viewport before translating
        // project coordinates back into the window's scene coordinates.
        QTest::qWait(100);
        if(id=="S"){auto map=control("mapView");const auto local=map->mapFromScene(mapPoint(id));if(map->property("zoom").toDouble()<3){editor.zoomMapCameraAt(3,local.x(),local.y());QTest::qWait(100);}}
        auto pos=mapPoint(id);
        if(id=="A"||id=="S"){
            // At 360px the web's 12px subunit tolerance reaches the rectangle
            // center. Use an interior point away from S/R for a single-country hit.
            for(auto value:editor.paths()){
                const auto p=value.toMap();if(p["countryId"]!=id)continue;
                auto map=control("mapView");
                const QPointF projected(p["left"].toDouble()+p["width"].toDouble()*(id=="S"?.15:.85),
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
class SelectionUiTests:public QObject{
    Q_OBJECT
private slots:
    void territorialGeometryPreview_data(){modes();}
    void territorialGeometryPreview(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);
        auto document=fixture();document.units[2].locked=false;
        Project p;p.replace(document);QFile f(h.path.toLocalFile());QVERIFY(f.open(QIODevice::WriteOnly));f.write(projectcodec::encode(p));f.close();
        QVERIFY(h.editor.openFile(h.path));QVERIFY(h.editor.selectObject(ref("S","general"),"replace","test"));
        const auto before=h.editor.documentBytes();QVERIFY(h.editor.transferSelectedSubunit("B"));
        QTRY_VERIFY_WITH_TIMEOUT(!h.editor.structureState()["calculating"].toBool(),10000);
        QVERIFY2(!h.editor.structureState()["geometryRequired"].toBool(),qPrintable(h.editor.structureState()["detail"].toString()));
        auto preview=h.control("territorialGeometryPreview"),confirm=h.control("confirmTerritorialStructure");
        QVERIFY(preview&&preview->isVisible());QVERIFY(confirm&&confirm->isEnabled());QCOMPARE(h.editor.documentBytes(),before);
        QTest::qWait(100);QVERIFY(h.window->grabWindow().save(mobile?"m4-preview-mobile-360.png":"m4-preview-desktop-1100.png"));
        QVERIFY(QMetaObject::invokeMethod(confirm,"clicked"));QVERIFY(h.editor.documentBytes()!=before);
        h.editor.undo();QCOMPARE(h.editor.documentBytes(),before);h.editor.redo();QVERIFY(h.editor.documentBytes()!=before);
        QVERIFY2(h.warnings.empty(),qPrintable(h.warnings.join('\n')));
    }
    void presentationMenu_data(){modes();}
    void presentationMenu(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY2(h.window,qPrintable(h.warnings.join('\n')));
        h.editor.selectCountry("A");const auto revision=h.editor.revision(),selection=h.editor.selectionRevision();
        QVERIFY(h.click("mapDisplayButton"));QTest::qWait(100);
        auto popup=h.window->findChild<QObject*>("mapDisplayPopup");QVERIFY(popup);QVERIFY(popup->property("visible").toBool());
        QVERIFY(h.editor.setPresentationVisibility("countries",false));
        QCOMPARE(h.editor.revision(),revision);QCOMPARE(h.editor.selectionRevision(),selection);
        QVERIFY(!h.editor.countryVisuals()["A"].toMap()["visible"].toBool());
        h.window->grabWindow().save(mobile?"presentation-mobile-360.png":"presentation-desktop-1100.png");
        QVERIFY2(h.warnings.empty(),qPrintable(h.warnings.join('\n')));
        QVERIFY(h.editor.discardPresentationRecovery());
    }
    void searchAndModifiers_data(){modes();}
    void searchAndModifiers(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY2(h.window,qPrintable(h.warnings.join('\n')));
        const auto bytes=h.editor.documentBytes();const auto rev=h.editor.revision();
        QVERIFY2(h.search(QStringLiteral("일반객체")),"search tab and field must be reachable");
        QCOMPARE(ids(h.editor.searchResults()),QStringList({"A","B","S"}));
        QVERIFY(h.click("searchSelect_A",Qt::ControlModifier));
        QVERIFY(h.click("searchSelect_B",Qt::ControlModifier));
        QCOMPARE(ids(h.editor.selectionItems()),QStringList({"A","B"}));
        QCOMPARE(h.editor.selectedId(),QString("B"));
        h.window->grabWindow().save(mobile?"search-mobile-360.png":"search-desktop.png");
        // The current web search handler supplies the visible ordered result refs:
        // Shift selects the visible result range and makes its endpoint primary.
        QVERIFY(h.click("searchSelect_A",Qt::ShiftModifier));
        QCOMPARE(ids(h.editor.selectionItems()),QStringList({"A","B"}));QCOMPARE(h.editor.selectedId(),QString("A"));
        QVERIFY(h.click("searchSelect_B",Qt::ControlModifier));
        QCOMPARE(ids(h.editor.selectionItems()),QStringList({"A"}));
        QCOMPARE(h.editor.selectedId(),QString("A"));
        QVERIFY(h.click("searchSelect_B",Qt::MetaModifier));
        QCOMPARE(ids(h.editor.selectionItems()),QStringList({"A","B"}));
        QVERIFY(h.click("searchSelect_A",Qt::NoModifier,mobile));
        QCOMPARE(ids(h.editor.selectionItems()),QStringList({"A"}));
        QVERIFY(h.control("selectionCardName")->isVisible()); // single result closes search
        QCOMPARE(h.editor.documentBytes(),bytes);QCOMPARE(h.editor.revision(),rev);
        QVERIFY(!h.editor.dirty());QVERIFY(!h.editor.canUndo());QVERIFY(!h.editor.canRedo());
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
    }
    void overlapChooserAndMapModifiers_data(){modes();}
    void overlapChooserAndMapModifiers(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);
        h.editor.selectCountry("B");const auto bytes=h.editor.documentBytes();
        h.mapClick("A");QCOMPARE(h.editor.selectedId(),QString("A"));
        h.mapClick("B",Qt::ControlModifier);QCOMPARE(ids(h.editor.selectionItems()),QStringList({"A","B"}));
        h.mapClick("B",Qt::ControlModifier);QCOMPARE(ids(h.editor.selectionItems()),QStringList({"A"}));
        h.mapClick("S",Qt::ControlModifier);
        auto chooser=h.window->findChild<QObject*>("objectChooser");QVERIFY2(chooser,"overlapping objects require a chooser");
        QTRY_VERIFY(chooser->property("visible").toBool());
        QCOMPARE(ids(h.editor.selectionItems()),QStringList({"A"}));
        h.window->grabWindow().save(mobile?"chooser-mobile-360.png":"chooser-desktop.png");
        QVERIFY(h.click("chooserSelect_S",Qt::NoModifier,mobile)); // retains the opening Ctrl intent
        QCOMPARE(ids(h.editor.selectionItems()),QStringList({"A","S"}));QCOMPARE(h.editor.selectedId(),QString("S"));
        QVERIFY(!h.editor.selectedEditable()); // selection != edit permission
        h.window->grabWindow().save(mobile?"selection-mobile-360.png":"selection-desktop.png");
        QTRY_VERIFY(!chooser->property("visible").toBool());
        h.mapClick("S",Qt::NoModifier,mobile);QTRY_VERIFY(chooser->property("visible").toBool());
        QVERIFY(h.click("chooserSelect_S",Qt::NoModifier,mobile));QCOMPARE(ids(h.editor.selectionItems()),QStringList({"S"}));
        h.mapClick("H");QVERIFY(h.editor.selectionItems().isEmpty()); // hidden is not a map hit
        QCOMPARE(h.editor.documentBytes(),bytes);QCOMPARE(h.editor.revision(),qulonglong(0));QVERIFY(!h.editor.dirty());
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
    }
    void focusAndHoverDoNotEdit_data(){modes();}
    void focusAndHoverDoNotEdit(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);
        h.editor.selectCountry("A");h.editor.setNameDraft("Saved A");QVERIFY(h.editor.commitCountryField("name"));
        h.editor.setMemoDraft("Redo");QVERIFY(h.editor.commitCountryField("notes"));h.editor.undo();
        const auto bytes=h.editor.documentBytes();const auto rev=h.editor.revision();const bool dirty=h.editor.dirty();
        QSignalSpy changes(&h.editor,&EditorController::dirtyChanged);
        QVERIFY(h.search("Beta"));QVERIFY(h.click("searchFocus_B",Qt::NoModifier,mobile));
        QCOMPARE(h.editor.selectedId(),QString("A")); // Focus is not implicit selection
        auto map=h.control("mapView");QVERIFY(map);
        const auto pt=h.mapPoint("B");const auto center=map->mapToScene({map->width()/2,map->height()/2});
        QVERIFY(std::abs(pt.x()-center.x())<=2);QVERIFY(std::abs(pt.y()-center.y())<=2);
        QVERIFY(map->property("zoom").toDouble()>=1.25);
        QVERIFY(map->property("zoom").toDouble()<=(mobile?12.:10.));
        auto select=h.control("searchSelect_B");QVERIFY(select);
        QTest::mouseMove(h.window,select->mapToScene({8,8}).toPoint());QTest::qWait(70);
        QCOMPARE(h.editor.hoverObject()["id"].toString(),QString("B"));
        QVERIFY(h.click("searchClear"));QTRY_VERIFY(h.editor.searchResults().isEmpty());
        QCOMPARE(h.editor.documentBytes(),bytes);QCOMPARE(h.editor.revision(),rev);QCOMPARE(h.editor.dirty(),dirty);
        QCOMPARE(changes.count(),0);QVERIFY(h.editor.canUndo());QVERIFY(h.editor.canRedo());
        h.editor.redo();h.editor.selectCountry("A");QCOMPARE(h.editor.memoDraft(),QString("Redo"));
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
    }
    void draftsSurviveRealNavigation_data(){modes();}
    void draftsSurviveRealNavigation(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);
        h.editor.selectCountry("A");QVERIFY(h.click("detailObjectName"));
        QTest::keyClick(h.window,Qt::Key_A,Qt::ControlModifier);for(char c:QByteArray("Draft A"))QTest::keyClick(h.window,c);
        QCOMPARE(h.editor.nameDraft(),QString("Draft A"));
        const auto bytes=h.editor.documentBytes();const auto rev=h.editor.revision();
        QVERIFY(h.search("Beta"));QCOMPARE(h.editor.revision(),rev);
        QVERIFY(h.click("searchSelect_B"));QCOMPARE(h.editor.selectedId(),QString("B"));
        QCOMPARE(h.editor.documentBytes(),bytes);QCOMPARE(h.editor.revision(),rev);QVERIFY(h.editor.hasPendingEdits());
        h.editor.selectCountry("A");QCOMPARE(h.editor.nameDraft(),QString("Draft A"));
        auto map=h.control("mapView");QVERIFY(map);
        const auto localTarget=map->mapFromScene(h.mapPoint("S"));
        const auto deltaY=80-localTarget.y();
        h.editor.beginMapInteraction();h.editor.beginMapCameraPan();
        // Keep the map gesture above the compact sheet and the memo popup.
        QVERIFY(h.editor.updateMapCameraPan(mobile?map->width()/2-localTarget.x():0,deltaY));
        h.editor.endMapCameraPan();h.editor.endMapInteraction();
        QVERIFY(h.click("detailObjectName"));
        QVERIFY(h.click("detailObjectNotes"));
        // Switching to another field still performs the web's normal independent commit.
        QCOMPARE(h.editor.revision(),rev+1);
        QInputMethodEvent event;event.setCommitString(QStringLiteral("메모 초안"));QGuiApplication::sendEvent(h.control("detailObjectNotes"),&event);
        const auto after=h.editor.documentBytes();const auto memoRev=h.editor.revision();
        h.mapClick("S");auto chooser=h.window->findChild<QObject*>("objectChooser");QVERIFY(chooser);
        QTRY_VERIFY(chooser->property("visible").toBool());QCOMPARE(h.editor.revision(),memoRev);
        QVERIFY(h.click("chooserSelect_S"));QCOMPARE(h.editor.selectedId(),QString("S"));
        QCOMPARE(h.editor.documentBytes(),after);QCOMPARE(h.editor.revision(),memoRev);
        h.editor.selectCountry("A");QCOMPARE(h.editor.memoDraft(),QStringLiteral("메모 초안"));
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
    }
    void keyboardSelectionAndHiddenSearch_data(){modes();}
    void keyboardSelectionAndHiddenSearch(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);
        const auto bytes=h.editor.documentBytes();
        h.editor.selectCountry("A");QVERIFY(h.search(QStringLiteral("일반객체")));
        auto results=h.control("objectSearchResults");QVERIFY(results);
        results->setProperty("currentIndex",1);results->forceActiveFocus();
        QTest::keyClick(h.window,Qt::Key_Return,Qt::ControlModifier);
        QCOMPARE(ids(h.editor.selectionItems()),QStringList({"A","B"}));
        QVERIFY(h.control("objectSearchField")->isVisible());
        QVERIFY(h.search("H"));QVERIFY(h.click("searchSelect_H"));
        QCOMPARE(h.editor.selectedId(),QString("H"));
        QVERIFY(!h.editor.countryVisuals()["H"].toMap()["visible"].toBool());
        QVERIFY(h.click("focusSelection"));
        h.mapClick("H");QVERIFY(h.editor.selectionItems().isEmpty());
        QMetaObject::invokeMethod(h.control("mapView"),"fit");QTest::qWait(80);
        h.mapClick("S");auto chooser=h.window->findChild<QObject*>("objectChooser");QVERIFY(chooser);
        QTRY_VERIFY(chooser->property("visible").toBool());
        QTest::keyClick(h.window,Qt::Key_Down);
        QTest::keyClick(h.window,Qt::Key_Return);
        QTRY_VERIFY(!chooser->property("visible").toBool());
        QCOMPARE(h.editor.selectedId(),QString("A")); // S is first; down selects country A
        QCOMPARE(h.editor.documentBytes(),bytes);QCOMPARE(h.editor.revision(),qulonglong(0));
        QVERIFY(!h.editor.dirty());QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
    }
    void chooserCancelAndStaleResults_data(){modes();}
    void chooserCancelAndStaleResults(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);h.editor.selectCountry("B");
        const auto bytes=h.editor.documentBytes();
        h.mapClick("S");auto chooser=h.window->findChild<QObject*>("objectChooser");QVERIFY(chooser);
        QTRY_VERIFY(chooser->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(h.window,"handleBack"));QTRY_VERIFY(!chooser->property("visible").toBool());
        QCOMPARE(h.editor.selectedId(),QString("B"));QVERIFY(h.window->isVisible());
        h.mapClick("S");QTRY_VERIFY(chooser->property("visible").toBool());
        QTest::keyClick(h.window,Qt::Key_Escape);QTRY_VERIFY(!chooser->property("visible").toBool());
        h.mapClick("S");QTRY_VERIFY(chooser->property("visible").toBool());
        h.window->resize(mobile?640:980,mobile?360:740);QTest::qWait(100);
        QTRY_VERIFY(!chooser->property("visible").toBool());QCOMPARE(h.editor.documentBytes(),bytes);
        h.window->resize(mobile?360:1100,mobile?640:760);QTest::qWait(100);
        h.mapClick("S");QTRY_VERIFY(chooser->property("visible").toBool());
        QVERIFY(h.editor.openFile(h.path));QTRY_VERIFY(!chooser->property("visible").toBool());
        QVERIFY(h.editor.selectionItems().isEmpty());QCOMPARE(h.editor.documentBytes(),bytes);
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
    }
};
int main(int argc,char** argv){
    QQuickStyle::setStyle("Basic");QGuiApplication app(argc,argv);registerWindowsFrameType();
    SelectionUiTests test;return QTest::qExec(&test,argc,argv);
}
#include "selection_ui_tests.moc"
