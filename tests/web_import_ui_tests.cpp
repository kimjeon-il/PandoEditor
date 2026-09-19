#include "editorcontroller.h"
#include "windowsframe.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QFile>
#include <QtTest>

namespace {
QQuickItem* item(QQuickItem* root,const QString& name) {
    if(root->objectName()==name)return root;
    for(auto c:root->childItems())if(auto found=item(c,name))return found;
    return nullptr;
}
void click(QQuickWindow* window,QQuickItem* control) {
    QVERIFY(control);QVERIFY(control->isVisible());QVERIFY(control->isEnabled());
    const auto p=control->mapToScene(QPointF(control->width()/2,control->height()/2)).toPoint();
    QVERIFY2(QRect(QPoint(),window->size()).contains(p),qPrintable(control->objectName()));
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,p);QTest::qWait(100);
}
}
class WebImportUiTests:public QObject {
    Q_OBJECT
private slots:
    void fileChooserAndReportDoNotCommitFocusedName() {
        QTemporaryDir dir;EditorController editor(EditorControllerConfig{false,dir.filePath("private.json")});
        QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("editor",&editor);engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
        QTest::qWait(150);editor.selectCountry("DEU");auto original=editor.selectedName();auto field=item(window->contentItem(),"countryName");QVERIFY(field);
        field->forceActiveFocus();QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);for(char c:QByteArray("Focus pending"))QTest::keyClick(window,c);
        QCOMPARE(editor.nameDraft(),QString("Focus pending"));QCOMPARE(editor.revision(),qulonglong(0));
        click(window,item(window->contentItem(),"webImportButton"));
        QVERIFY(window->property("webImportFlowActive").toBool());QCOMPARE(editor.selectedName(),original);QCOMPARE(editor.revision(),qulonglong(0));
        auto picker=window->findChild<QObject*>("webOpenDialog");QVERIFY(picker);QVERIFY(QMetaObject::invokeMethod(picker,"reject"));
        QTest::qWait(150);QCOMPARE(editor.nameDraft(),QString("Focus pending"));QCOMPARE(editor.revision(),qulonglong(0));
        field->forceActiveFocus();
        QVERIFY(QMetaObject::invokeMethod(window,"beginWebImport",Q_ARG(QVariant,QUrl::fromLocalFile(QStringLiteral(WEB_IMPORT_FIXTURES)+"/v5.input.json"))));
        QTRY_VERIFY(editor.hasWebImportPreview());QCOMPARE(editor.revision(),qulonglong(0));QCOMPARE(editor.nameDraft(),QString("Focus pending"));
        auto dialog=window->findChild<QObject*>("webImportDialog");QVERIFY(dialog);
        // The offscreen platform keeps the dismissed native chooser's window
        // active. Reactivate the tested window before delivering a real key.
        window->requestActivate();QTRY_VERIFY(dialog->property("activeFocus").toBool());
        QTest::keyClick(window,Qt::Key_Escape);
        QTRY_VERIFY(!editor.hasWebImportPreview());QCOMPARE(editor.revision(),qulonglong(0));
        window->setProperty("allowClose",true);window->close();
    }
    void reviewFlow_data() {QTest::addColumn<bool>("mobile");QTest::newRow("desktop")<<false;QTest::newRow("mobile-360")<<true;}
    void reviewFlow() {
        QFETCH(bool,mobile);QTemporaryDir dir;
        EditorController editor(EditorControllerConfig{mobile,dir.filePath("private.json")});
        QQmlApplicationEngine engine;QStringList warnings;
        connect(&engine,&QQmlApplicationEngine::warnings,this,[&](const QList<QQmlError>& list){for(const auto& e:list)warnings<<e.toString();});
        engine.rootContext()->setContextProperty("editor",&editor);engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());QVERIFY(window);
        window->resize(mobile?360:1100,720);QTest::qWait(200);window->grabWindow();
        auto button=item(window->contentItem(),"webImportButton");QVERIFY2(button,"web import must have a real PC/mobile entry");
        QCOMPARE(button->property("text").toString(),QString("웹 프로젝트 가져오기"));
        editor.selectCountry("DEU");editor.setColor("#102030");editor.undo();editor.setNameDraft("uncommitted name");
        auto revision=editor.revision();auto name=editor.selectedName();
        // The file dialog callback and tests share the same QML entry. Opening
        // it must not turn a pending field into a committed edit.
        QVERIFY(QMetaObject::invokeMethod(window,"beginWebImport",Q_ARG(QVariant,QUrl::fromLocalFile(QStringLiteral(WEB_IMPORT_FIXTURES)+"/v5.input.json"))));
        QTRY_VERIFY(editor.hasWebImportPreview());
        auto report=window->findChild<QObject*>("webImportDialog");QVERIFY(report);QTRY_VERIFY(report->property("visible").toBool());
        QCOMPARE(editor.revision(),revision);QCOMPARE(editor.nameDraft(),QString("uncommitted name"));QVERIFY(editor.canRedo());
        auto list=item(window->contentItem(),"webImportReportList");QVERIFY(list);QVERIFY(list->height()>50);
        QTest::qWait(150);auto image=window->grabWindow();QVERIFY(!image.isNull());
        QVERIFY(image.save(mobile?"web-import-mobile-360.png":"web-import-desktop.png"));
        click(window,item(window->contentItem(),"cancelWebImport"));
        QVERIFY(!editor.hasWebImportPreview());QCOMPARE(editor.revision(),revision);QCOMPARE(editor.selectedName(),name);
        QCOMPARE(editor.nameDraft(),QString("uncommitted name"));QVERIFY(editor.canRedo());
        QVERIFY(QMetaObject::invokeMethod(window,"beginWebImport",Q_ARG(QVariant,QUrl::fromLocalFile(QStringLiteral(WEB_IMPORT_FIXTURES)+"/v5.input.json"))));
        QTRY_VERIFY(editor.hasWebImportPreview());QTest::qWait(150);
        click(window,item(window->contentItem(),"discardAndWebImport"));
        QTRY_COMPARE(editor.countryRows().size(),qsizetype(2));QCOMPARE(editor.revision(),qulonglong(0));QVERIFY(editor.dirty());
        QVERIFY(!editor.canUndo()&&!editor.canRedo());QVERIFY(!editor.hasWebImportPreview());
        editor.selectCountry("A");auto limited=item(window->contentItem(),"preservedDataNotice");QVERIFY(limited);QVERIFY(limited->isVisible());
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->setProperty("allowClose",true);window->close();
    }
};
int main(int argc,char** argv) {
    QQuickStyle::setStyle("Basic");QGuiApplication app(argc,argv);registerWindowsFrameType();
    WebImportUiTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "web_import_ui_tests.moc"
