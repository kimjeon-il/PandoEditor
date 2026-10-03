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
#include <QJSValue>
using namespace pandoeditor;

namespace {
QVariantMap ref(QString id,QString type="country") {return {{"domain","territorial"},{"type",type},{"id",id}};}
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
        d.units.push_back({id,id,"",kind,geometry,locked});
        d.presentation.membership[territorialRef(id)]=layer;d.presentation.objectStyles[territorialRef(id)]={0xabcdef,.8};
    };
    add("S",UnitKind::Subunit,"other",1,4,true);
    add("R",UnitKind::Region,"other",6,8,false);
    add("H",UnitKind::Region,"hidden",40,42,false);
    d.relations.push_back({"base-s",territorialRef("S"),territorialRef("A"),territorialRef("A")});
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
        engine.load(QUrl("qrc:/common/Main.qml"));
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
                }
                break;
            }
        }
        QTest::qWait(50);auto pos=c->mapToScene(QPointF(c->width()/2,c->height()/2)).toPoint();
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
        QTest::qWait(160);return true;
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
class PropertyUiTests:public QObject {
 Q_OBJECT
private slots:
    void contentPanelSharedCommands_data(){modes();}
    void contentPanelSharedCommands(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY2(h.window,qPrintable(h.warnings.join('\n')));
        QVERIFY(h.click("contentPanelButton"));QVERIFY(h.editor.beginContentEdit("distributionLayer","language",true));
        QVERIFY(h.editor.updateContentField("name",QStringLiteral("언어 분포")));
        const auto before=h.editor.documentBytes();QTest::qWait(50);
        QVERIFY(h.click("contentPreview"));QCOMPARE(h.editor.documentBytes(),before);
        QVERIFY(h.click("contentConfirm"));QVERIFY(!h.editor.contentEditState()["active"].toBool());
        auto d=projectcodec::decode(h.editor.documentBytes());QCOMPARE(d.distributionLayers.size(),std::size_t(1));
        QCOMPARE(d.distributionLayers.front().name,std::string("언어 분포"));h.editor.undo();QCOMPARE(h.editor.documentBytes(),before);
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
    }
    void objectNamesRenderAsLiteralText_data(){modes();}
    void objectNamesRenderAsLiteralText(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);h.editor.selectCountry("A");
        const QString name="<b>literal</b>";h.editor.setNameDraft(name);QVERIFY(h.editor.commitObjectField("name"));
        QVERIFY(h.click("toggleObjectEditor"));
        auto field=h.control("detailObjectName");QVERIFY(field&&field->isVisible());
        QCOMPARE(field->property("displayText").toString(),name);
        QCOMPARE(h.control("countryName")->property("displayText").toString(),name);
        QVERIFY(h.search("literal"));auto row=h.control("searchSelect_A");QVERIFY(row);
        auto content=qvariant_cast<QQuickItem*>(row->property("contentItem"));QVERIFY(content);
        QCOMPARE(content->property("textFormat").toInt(),int(Qt::PlainText));QCOMPARE(h.editor.selectedName(),name);
        QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
    }
    void backClosesNewEditorBeforeProject_data(){modes();}
    void backClosesNewEditorBeforeProject(){
        QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);h.editor.selectCountry("A");
        h.editor.setNameDraft("Unsaved");QVERIFY(h.editor.commitObjectField("name"));
        const auto bytes=h.editor.documentBytes();const auto revision=h.editor.revision();
        QVERIFY(h.click("toggleObjectEditor"));QVERIFY(h.control("objectPropertyPanel")->isVisible());
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
   QVERIFY(!h.control("objectPropertyPanel")->isVisible());
   QVERIFY(h.enter("countryName","  Renamed A  "));QCOMPARE(h.editor.selectedName(),QString("Renamed A"));QCOMPARE(h.editor.revision(),qulonglong(1));
   QVERIFY(h.enter("countryName",""));QCOMPARE(h.editor.selectedName(),QString("Alpha"));QCOMPARE(h.editor.revision(),qulonglong(2));
   h.editor.selectObject(ref("R","region"));QVERIFY(h.enter("regionName",QStringLiteral("지방 수정")));QCOMPARE(h.editor.selectedName(),QStringLiteral("지방 수정"));
   QVERIFY(h.click("toggleObjectEditor",Qt::NoModifier,mobile));QVERIFY(h.control("objectPropertyPanel")->isVisible());QVERIFY(h.control("regionValidFrom")->isVisible());
   h.window->grabWindow().save(mobile?"properties-mobile-360.png":"properties-desktop.png");
   h.editor.selectObject(ref("S","subunit"));QVERIFY(!h.control("regionValidFrom")->isVisible());QVERIFY(!h.control("subunitName")->isEnabled());
   QVERIFY(h.click("objectLockButton"));QVERIFY(h.control("subunitName")->isEnabled());QVERIFY(h.enter("subunitName","  "));QCOMPARE(h.editor.selectedName(),QStringLiteral("이름 없는 하위단위"));
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
   QVERIFY(h.enter("countryName","Notes owner",false));QCOMPARE(h.editor.revision(),qulonglong(0));
   QVERIFY(h.click("objectNotesTrigger"));QVERIFY(h.control("countryMemo"));QTRY_COMPARE(h.editor.revision(),qulonglong(1));
   QVERIFY(h.enter("countryMemo",QStringLiteral("  메모\n둘째  "),false));QCOMPARE(h.editor.revision(),qulonglong(1));
   QVERIFY(h.click("closeObjectNotes"));QCOMPARE(h.editor.revision(),qulonglong(2));QCOMPARE(h.editor.memoDraft(),QStringLiteral("  메모\n둘째  "));
   QVERIFY(h.click("toggleObjectEditor"));QVERIFY(h.click("objectLockButton"));QVERIFY(!h.control("countryName")->isEnabled());
   QVERIFY(h.click("objectNotesTrigger"));QVERIFY(h.control("countryMemo")->property("readOnly").toBool());QVERIFY(h.click("closeObjectNotes"));
   h.editor.selectObject(ref("R","region"));QVERIFY(h.click("objectNotesTrigger"));QVERIFY(h.enter("regionMemo",QStringLiteral("  지방\n메모  "),false));QVERIFY(h.click("closeObjectNotes"));QCOMPARE(h.editor.selectedId(),QString("R"));QCOMPARE(h.editor.memoDraft(),QStringLiteral("지방\n메모"));
   QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
 }
 void regionDatesRejectWithoutChangingOtherDrafts_data(){modes();}
 void regionDatesRejectWithoutChangingOtherDrafts(){
   QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);h.editor.selectObject(ref("R","region"));QVERIFY(h.click("toggleObjectEditor"));
   QVERIFY(h.enter("regionValidFrom","-0001"));QCOMPARE(h.editor.validFromDraft(),QString("-0001"));
   QVERIFY(h.enter("regionValidTo","0001-01-01"));QCOMPARE(h.editor.validToDraft(),QString("0001-01-01"));
   const auto bytes=h.editor.documentBytes();const auto rev=h.editor.revision();
   QVERIFY(h.enter("regionValidFrom","0000"));QCOMPARE(h.editor.revision(),rev);QCOMPARE(h.editor.documentBytes(),bytes);QCOMPARE(h.editor.validFromDraft(),QString("-0001"));
   auto error=h.window->findChild<QObject*>("errorDialog");QVERIFY(error);QTRY_VERIFY(error->property("visible").toBool());
   QVERIFY(QMetaObject::invokeMethod(error,"close"));QTest::qWait(200);
   QVERIFY(h.enter("regionValidFrom","0002"));QCOMPARE(h.editor.revision(),rev);QCOMPARE(h.editor.documentBytes(),bytes);
   QVERIFY(QMetaObject::invokeMethod(error,"close"));QTest::qWait(200);
   QVERIFY(h.enter("regionValidTo",""));QCOMPARE(h.editor.validToDraft(),QString());
   QVERIFY2(h.warnings.isEmpty(),qPrintable(h.warnings.join('\n')));
 }
 void multiLockAndSameColorCheckpoint_data(){modes();}
 void multiLockAndSameColorCheckpoint(){
   QFETCH(bool,mobile);Harness h(mobile);QVERIFY(h.window);
   QVERIFY(h.editor.setSelection({ref("A"),ref("S","subunit")}));QVERIFY(h.editor.objectProperties()["someLocked"].toBool());
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
   QVERIFY(h.enter("countryName","Kept A",false));const auto bytes=h.editor.documentBytes();const auto rev=h.editor.revision();
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
