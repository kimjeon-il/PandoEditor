#include "editorcontroller.h"
#include "projectcodec.h"
#include "territorial_fixture.h"
#include <pandoeditor/project.h>
#include <QFile>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QWheelEvent>
#include <QInputMethodEvent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickView>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <stdexcept>

// Fixed Web a1555722813fbb7f2ae397f1999e21dd9fa0ed66 browser/info-tab-v9:
// a single period input, inert relation names, GPS-only camera focus retaining
// primary selection, no clipping at mobile width and atomic error feedback.
namespace {
QQmlEngine* sharedUiEngine=nullptr;
QQuickView* sharedUiView=nullptr;
EditorController* desktopUiEditor=nullptr;
EditorController* mobileUiEditor=nullptr;
QVariantMap ref(const QString& id) {return {{"domain","territorial"},{"id",id}};}
QQuickItem* item(QQuickItem* root,const QString& name) {
    if(root->objectName()==name)return root;
    for(auto* child:root->childItems())if(auto* found=item(child,name))return found;
    return nullptr;
}
struct Harness {
    QTemporaryDir directory;
    EditorController& editor;
    QStringList warnings;
    QQuickView& view=*sharedUiView;
    QMetaObject::Connection warningConnection;
    explicit Harness(bool mobile):editor(*(mobile?mobileUiEditor:desktopUiEditor)) {
        using namespace pandoeditor;
        ProjectDocument document({{"A","Alpha",{{{{0,0},{12,0},{12,12},{0,12},{0,0}}}},0x336699}},{{"countries","Countries"}});
        const auto add=[&](std::string id,std::string name,UnitKind kind,double x,double size,const std::string& parent) {
            Geometry geometry{"Polygon",{},{},{{{{x,1},{x+size,1},{x+size,1+size},{x,1+size},{x,1}}}}};
            GeometryRef key{"info-ui:"+id,1};document.geometries.insert(key,geometry);
            appendTerritory(document,{id,name,"",kind,false},key,parent);
            document.presentation.membership[territorialRef(id)]="countries";document.presentation.objectStyles[territorialRef(id)]={0,1,false};
        };
        add("B","Beta",UnitKind::General,1,10,"A");add("C","Child",UnitKind::General,2,2,"B");
        add("D",std::string(120,'D'),UnitKind::General,7,2,"B");add("R","Independent region",UnitKind::Regional,20,2,"");
        Project project;project.replace(std::move(document));const auto bytes=projectcodec::encode(project);
        QFile file(directory.filePath("info-ui.pando.json"));
        if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())throw std::runtime_error("UI fixture write failed");
        file.close();if(!editor.openFile(QUrl::fromLocalFile(file.fileName())))throw std::runtime_error("UI fixture open failed");
        warningConnection=QObject::connect(view.engine(),&QQmlEngine::warnings,&view,[this](const QList<QQmlError>& errors){for(const auto& error:errors)warnings.push_back(error.toString());});
        view.setResizeMode(QQuickView::SizeRootObjectToView);view.rootContext()->setContextProperty("editor",&editor);
        if(!view.rootObject())view.setSource(QUrl("qrc:/common/ObjectPropertyPanel.qml"));
        view.resize(mobile?360:600,mobile?640:760);
        if(view.rootObject()) {
            view.rootObject()->setProperty("compact",mobile);
            view.rootObject()->setSize(QSizeF(view.size()));
        }
        view.show();
        if(!QTest::qWaitForWindowExposed(&view))throw std::runtime_error("UI fixture window was not exposed");
    }
    ~Harness() {
        QObject::disconnect(warningConnection);
        // Project replacement resets each case through production owner/revision
        // signals; the application's controller and window persist across files.
        if(view.rootObject())view.rootObject()->forceActiveFocus();
    }
    QQuickItem* control(const QString& name) {return view.rootObject()?item(view.rootObject(),name):nullptr;}
    bool click(const QString& name) {
        view.requestActivate();
        if(!QTest::qWaitForWindowActive(&view,1000))return false;
        QCoreApplication::processEvents();
        auto* target=control(name);if(!target||!target->isVisible()||!target->isEnabled())return false;
        const auto viewport=[&] {
            QRectF visible(QPointF(),view.size());
            for(auto* ancestor=target->parentItem();ancestor;ancestor=ancestor->parentItem())
                if(ancestor->clip())visible=visible.intersected(ancestor->mapRectToScene(ancestor->boundingRect()));
            return visible;
        };
        auto point=target->mapToScene(QPointF(target->width()/2,target->height()/2)).toPoint();
        for(int step=0;step<15&&!viewport().contains(point);++step) {
            const QPointF position=viewport().center();
            QWheelEvent wheel(position,view.mapToGlobal(position.toPoint()),QPoint(),QPoint(0,point.y()>viewport().bottom()?-120:120),Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);
            QGuiApplication::sendEvent(&view,&wheel);QTest::qWait(10);
            point=target->mapToScene(QPointF(target->width()/2,target->height()/2)).toPoint();
        }
        if(!viewport().contains(point))return false;
        QTest::mouseClick(&view,Qt::LeftButton,Qt::NoModifier,point);return true;
    }
    bool enterPeriod(const QString& value,bool finish=true) {
        if(!click("territorialPeriodInput"))return false;
        QTest::keyClick(&view,Qt::Key_A,Qt::ControlModifier);
        QInputMethodEvent input;input.setCommitString(value);QGuiApplication::sendEvent(control("territorialPeriodInput"),&input);
        if(finish)QTest::keyClick(&view,Qt::Key_Return);return true;
    }
};
void modes() {QTest::addColumn<bool>("mobile");QTest::newRow("desktop")<<false;QTest::newRow("mobile")<<true;}
}
class TerritorialInfoUiTests : public QObject {
    Q_OBJECT
private slots:
    void singlePeriodIsVisibleForBothTerritorialKinds_data() {
        QTest::addColumn<bool>("mobile");QTest::addColumn<QString>("id");
        QTest::newRow("desktop-General")<<false<<QString("B");QTest::newRow("mobile-General")<<true<<QString("B");
        QTest::newRow("desktop-Regional")<<false<<QString("R");QTest::newRow("mobile-Regional")<<true<<QString("R");
    }
    void singlePeriodIsVisibleForBothTerritorialKinds() {
        QFETCH(bool,mobile);QFETCH(QString,id);Harness harness(mobile);QVERIFY(harness.view.status()==QQuickView::Ready);QVERIFY(harness.editor.selectObject(ref(id)));
        QTRY_VERIFY(harness.control("territorialPeriodInput"));QTRY_VERIFY(harness.control("territorialPeriodInput")->isVisible());
        QCOMPARE(harness.control("territorialPeriodInput")->property("text").toString(),QString());
        QVERIFY(!harness.control("regionValidFrom"));QVERIFY(!harness.control("regionValidTo"));
        QVERIFY2(harness.warnings.isEmpty(),qPrintable(harness.warnings.join('\n')));
    }
    void invalidAndDatedInputRemainVisibleWithoutPartialCommit_data() {modes();}
    void invalidAndDatedInputRemainVisibleWithoutPartialCommit() {
        QFETCH(bool,mobile);Harness harness(mobile);QVERIFY(harness.view.status()==QQuickView::Ready);QVERIFY(harness.editor.selectObject(ref("B")));
        QTRY_VERIFY(harness.control("territorialPeriodInput"));const auto before=harness.editor.documentBytes();const auto revision=harness.editor.revision();
        for(const auto& input:{QString("1900 ~~ 1901"),QString("1871-01-18 ~ 1918-11-09")}) {
            QVERIFY(harness.enterPeriod(input));QTRY_VERIFY(harness.control("territorialPeriodValidationError")&&harness.control("territorialPeriodValidationError")->isVisible());
            QCOMPARE(harness.control("territorialPeriodInput")->property("text").toString(),input);QCOMPARE(harness.editor.documentBytes(),before);QCOMPARE(harness.editor.revision(),revision);
        }
        QVERIFY(harness.enterPeriod(" ~ "));QTRY_COMPARE(harness.control("territorialPeriodInput")->property("text").toString(),QString());
        QVERIFY(!harness.control("territorialPeriodValidationError")->isVisible());QCOMPARE(harness.editor.documentBytes(),before);QCOMPARE(harness.editor.revision(),revision);
        QVERIFY2(harness.warnings.isEmpty(),qPrintable(harness.warnings.join('\n')));
    }
    void focusedUndoReplacesLocalInputAndAllowsFreshCommit_data() {modes();}
    void focusedUndoReplacesLocalInputAndAllowsFreshCommit() {
        QFETCH(bool,mobile);Harness harness(mobile);QVERIFY(harness.view.status()==QQuickView::Ready);
        QVERIFY(harness.editor.selectObject(ref("B")));
        harness.editor.setNameDraft("Renamed Beta");QVERIFY(harness.editor.commitObjectField("name"));
        QTRY_VERIFY(harness.control("territorialPeriodInput"));
        QVERIFY(harness.enterPeriod("1900 ~~ 1901",false));
        QVERIFY(harness.control("territorialPeriodInput")->hasActiveFocus());
        const auto previousRevision=harness.editor.revision();
        harness.editor.undo();QVERIFY(harness.editor.revision()!=previousRevision);
        QTRY_COMPARE(harness.control("territorialPeriodInput")->property("text").toString(),QString());
        const auto canonical=harness.editor.documentBytes();
        QVERIFY(harness.enterPeriod(" ~ "));
        QTRY_COMPARE(harness.control("territorialPeriodInput")->property("text").toString(),QString());
        QVERIFY(!harness.control("territorialPeriodValidationError")->isVisible());
        QCOMPARE(harness.editor.documentBytes(),canonical);
        QVERIFY2(harness.warnings.isEmpty(),qPrintable(harness.warnings.join('\n')));
    }
    void sameIdProjectReplacementEndsFocusedToken_data() {modes();}
    void sameIdProjectReplacementEndsFocusedToken() {
        QFETCH(bool,mobile);Harness harness(mobile);QVERIFY(harness.view.status()==QQuickView::Ready);
        QVERIFY(harness.editor.selectObject(ref("B")));
        QTRY_VERIFY(harness.control("territorialPeriodInput"));
        QVERIFY(harness.enterPeriod("1900 ~~ 1901",false));
        QVERIFY(harness.control("territorialPeriodInput")->hasActiveFocus());
        const auto previousProject=harness.editor.projectInstanceId();
        QVERIFY(harness.editor.openFile(QUrl::fromLocalFile(harness.directory.filePath("info-ui.pando.json"))));
        QTRY_VERIFY(harness.editor.projectInstanceId()!=previousProject);
        QVERIFY(harness.editor.selectObject(ref("B")));
        QTRY_COMPARE(harness.control("territorialPeriodInput")->property("text").toString(),QString());
        const auto canonical=harness.editor.documentBytes();
        QVERIFY(harness.enterPeriod(" ~ "));
        QTRY_COMPARE(harness.control("territorialPeriodInput")->property("text").toString(),QString());
        QVERIFY(!harness.control("territorialPeriodValidationError")->isVisible());
        QCOMPARE(harness.editor.documentBytes(),canonical);
        QVERIFY2(harness.warnings.isEmpty(),qPrintable(harness.warnings.join('\n')));
    }
    void relationNameIsInertAndGpsRetainsSelection_data() {modes();}
    void relationNameIsInertAndGpsRetainsSelection() {
        QFETCH(bool,mobile);Harness harness(mobile);QVERIFY(harness.view.status()==QQuickView::Ready);QVERIFY(harness.editor.selectObject(ref("B")));
        QTRY_VERIFY(harness.control("territorialInfoName_D"));const auto before=harness.editor.mapViewState();const auto selected=harness.editor.primaryObject();
        QVERIFY(harness.click("territorialInfoName_C"));QCOMPARE(harness.editor.mapViewState(),before);QCOMPARE(harness.editor.primaryObject(),selected);
        auto* name=harness.control("territorialInfoName_D");QVERIFY(name->width()>0);QVERIFY(name->width()<harness.view.width());
        auto* button=harness.control("territorialInfoFocus_D");QVERIFY(button);QVERIFY(button->mapToScene(QPointF(button->width(),0)).x()<=harness.view.width());
        QSignalSpy focused(&harness.editor,&EditorController::focusRequested);QVERIFY(harness.click("territorialInfoFocus_C"));QTRY_COMPARE(focused.size(),qsizetype(1));QCOMPARE(harness.editor.primaryObject(),selected);
        QVERIFY2(harness.warnings.isEmpty(),qPrintable(harness.warnings.join('\n')));
    }
};
int main(int argc,char** argv) {
    QQuickStyle::setStyle("Basic");QGuiApplication application(argc,argv);
    const int font=QFontDatabase::addApplicationFont(":/fonts/Pretendard-Regular.otf");
    QFontDatabase::addApplicationFont(":/fonts/Pretendard-SemiBold.otf");
    if(font>=0)application.setFont(QFont(QFontDatabase::applicationFontFamilies(font).first()));
    // Production panels share the application's engine and compiled components.
    // Each test opens an isolated project through the real production file reader.
    QQmlEngine engine;sharedUiEngine=&engine;
    QTemporaryDir settings;
    EditorController desktop(EditorControllerConfig{false,settings.filePath("desktop.json")});desktopUiEditor=&desktop;
    EditorController mobile(EditorControllerConfig{true,settings.filePath("mobile.json")});mobileUiEditor=&mobile;
    QQuickView view(sharedUiEngine,nullptr);sharedUiView=&view;
    TerritorialInfoUiTests test;return QTest::qExec(&test,argc,argv);
}
#include "territorial_info_ui_tests.moc"
