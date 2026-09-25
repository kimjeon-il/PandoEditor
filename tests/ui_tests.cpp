#include "ui_navigation.h"
#include "editorcontroller.h"
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
#include <QImage>
#include <QFontDatabase>
#include <functional>
#include <QFile>
#include <algorithm>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#undef near
#endif

static QQuickItem* visualItem(QQuickItem* root,const QString& name)
{
    if(root->objectName()==name) return root;
    for(auto child:root->childItems()) if(auto item=visualItem(child,name)) return item;
    return nullptr;
}
static void exposeForTest(QQuickWindow* window)
{
    if(qEnvironmentVariable("QT_QPA_PLATFORM")=="windows") {
        window->hide(); window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    }
    QTest::qWait(200); window->grabWindow();
}
static QImage capture(QQuickWindow* window)
{
    window->grabWindow(); QTest::qWait(100);
    window->grabWindow(); QTest::qWait(100);
    return window->grabWindow();
}
static void typeText(QQuickWindow* window,const QByteArray& text)
{
    for(char character:text) QTest::keyClick(window,character);
}
static QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
static bool clickControl(QQuickWindow* window,const QString& name)
{
    enterExistingControlRoute(window,name);
    auto item=visualItem(window->contentItem(),name);
    if(!item||!item->isVisible()||!item->isEnabled()) return false;
    for(auto parent=item->parentItem();parent;parent=parent->parentItem()) {
        if(parent->property("contentY").isValid()) {
            auto content=qvariant_cast<QQuickItem*>(parent->property("contentItem"));
            if(content) {
                auto y=item->mapToItem(content,QPointF()).y();
                auto maxY=std::max(0.0,parent->property("contentHeight").toDouble()-parent->height());
                parent->setProperty("contentY",std::clamp(y-16.0,0.0,maxY));
            }
            break;
        }
    }
    window->grabWindow(); QTest::qWait(100);
    auto center=item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint();
    if(!QRect(QPoint(),window->size()).contains(center)) return false;
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,center);
    QTest::qWait(80); return true;
}

class UiTests:public QObject {
    Q_OBJECT
private slots:
    void historicalLibraryPanelAtDesktopAnd360px() {
        QTemporaryDir dir;QVERIFY(dir.isValid());
        QFile file(dir.filePath("historical.json"));QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray json=R"({"schemaVersion":2,"entities":[{"libraryId":"historical-country:fixture","type":"country","canonicalName":"Fixture","geometryVersions":[{"id":"v1","geometry":{"type":"Polygon","coordinates":[[[70,0],[72,0],[72,2],[70,2],[70,0]]]}}]}],"snapshots":[]})";
        QCOMPARE(file.write(json),json.size());file.close();
        for(bool mobile:{false,true}) {
            EditorController editor(EditorControllerConfig{mobile,{}});
            QVERIFY(editor.loadHistoricalLibrary(QUrl::fromLocalFile(file.fileName())));
            QQmlApplicationEngine engine;QStringList warnings;
            connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){
                for(const auto& e:errors)warnings<<e.toString();
            });
            engine.rootContext()->setContextProperty("editor",&editor);
            engine.load(QUrl("qrc:/common/Main.qml"));
            QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));
            auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);QVERIFY(window);
            window->resize(mobile?360:1100,mobile?640:760);exposeForTest(window);
            QVERIFY(clickControl(window,"historicalLibraryButton"));
            auto* panel=window->findChild<QObject*>("historicalLibraryPanel");QVERIFY(panel);
            QTRY_VERIFY(panel->property("visible").toBool());
            QVERIFY(panel->property("width").toDouble()<=window->width());
            QVERIFY(editor.historicalResults().size()==1);
            QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
            QMetaObject::invokeMethod(panel,"close");
            window->close();
        }
    }
    void frameHitTargetsAtFractionalScale() {
        // Losing local DPI conversion would route a scaled maximize click to
        // the map; treating maximized corners as resize would break snapping.
        const std::array<QRectF,3> buttons{QRectF(862,0,46,32),
                                         QRectF(908,0,46,32),QRectF(954,0,46,32)};
        for(qreal ratio : {1.0,1.25,1.5}) {
            auto hit=[&](QPointF pixel,bool max=false) {
                return WindowsFrame::hitTest(WindowsFrame::logicalPoint(pixel,ratio),
                    QSizeF(1000,720),8,max,QRectF(0,0,1000,32),buttons);
            };
            QCOMPARE(hit(QPointF(931,16)*ratio),WindowsFrame::Maximize);
            QCOMPARE(hit(QPointF(977,16)*ratio),WindowsFrame::Close);
            QCOMPARE(hit(QPointF(885,16)*ratio),WindowsFrame::Minimize);
            QCOMPARE(hit(QPointF(300,16)*ratio),WindowsFrame::Caption);
            QCOMPARE(hit(QPointF(300,60)*ratio),WindowsFrame::Client);
            QCOMPARE(hit(QPointF(2,2)*ratio),WindowsFrame::TopLeft);
            QCOMPARE(hit(QPointF(998,2)*ratio),WindowsFrame::TopRight);
            QCOMPARE(hit(QPointF(2,718)*ratio),WindowsFrame::BottomLeft);
            QCOMPARE(hit(QPointF(998,718)*ratio),WindowsFrame::BottomRight);
            QCOMPARE(hit(QPointF(2,300)*ratio),WindowsFrame::Left);
            QCOMPARE(hit(QPointF(998,300)*ratio),WindowsFrame::Right);
            QCOMPARE(hit(QPointF(500,2)*ratio),WindowsFrame::Top);
            QCOMPARE(hit(QPointF(500,718)*ratio),WindowsFrame::Bottom);
            QCOMPARE(hit(QPointF(2,2)*ratio,true),WindowsFrame::Caption);
            QCOMPARE(hit(QPointF(998,2)*ratio,true),WindowsFrame::Close);
            QCOMPARE(hit(QPointF(500,718)*ratio,true),WindowsFrame::Client);
        }
    }
    void nativeFrameAdapterHasSafeFallback() {
        EditorController editor;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("editor", &editor);
        engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]); QVERIFY(window);
        auto frame=window->findChild<QObject*>("windowsFrame");
        QVERIFY2(frame,"Main window must own the native frame lifecycle adapter");
        if(QGuiApplication::platformName()!="windows") {
            QVERIFY(!frame->property("active").toBool());
            QVERIFY(!window->flags().testFlag(Qt::FramelessWindowHint));
            QVERIFY(!visualItem(window->contentItem(),"desktopTitleBar")->isVisible());
        }
        window->close();
    }
    void desktopOpenRequestDoesNotCommitDraftBeforeFileValidation() {
        EditorController editor;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]); QVERIFY(window);
        exposeForTest(window);
        editor.selectCountry("DEU"); editor.setNameDraft("uncommitted");
        const auto original=editor.selectedName();
        QVERIFY(QMetaObject::invokeMethod(window,"requestAction",Q_ARG(QVariant,QVariant("open"))));
        QCOMPARE(editor.selectedName(),original);
        QCOMPARE(editor.nameDraft(),QString("uncommitted")); QVERIFY(!editor.canUndo());
        QVERIFY(clickControl(window,"cancelUnsaved"));
        auto notice=visualItem(window->contentItem(),"documentFormatNotice"); QVERIFY(notice);
        QVERIFY(notice->property("text").toString().contains("Qt v7"));
        QVERIFY(clickControl(window,"documentFormatNotice"));
        QVERIFY(notice->property("expanded").toBool());
        window->setProperty("allowClose",true); window->close();
    }
    void desktopTitleBarKeepsWorkspaceBelowLargeWindowFrame() {
        EditorController editor;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("editor", &editor);
        engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]); QVERIFY(window);
        exposeForTest(window);
        auto titleBar=visualItem(window->contentItem(),"desktopTitleBar"); QVERIFY(titleBar);
        auto toolbar=visualItem(window->contentItem(),"storageToolbar"); QVERIFY(toolbar);
        if(QGuiApplication::platformName()!="windows") {
        QVERIFY(!titleBar->isVisible());
        QCOMPARE(toolbar->mapToScene(QPointF()).y(),0.0);
        window->close();
        return;
        }
#ifdef Q_OS_WIN
        auto frame=window->findChild<WindowsFrame*>("windowsFrame"); QVERIFY(frame);
        QTRY_VERIFY(frame->active());
        QCOMPARE(titleBar->height(),32.0);
        QCOMPARE(toolbar->mapToScene(QPointF()).y(),32.0);
        QVERIFY(visualItem(window->contentItem(),"minimizeWindowButton"));
        QVERIFY(visualItem(window->contentItem(),"maximizeWindowButton"));
        QVERIFY(visualItem(window->contentItem(),"closeWindowButton"));
        auto maxButton=visualItem(window->contentItem(),"maximizeWindowButton");
        QCOMPARE(maxButton->width(),46.0);
        QCOMPARE(maxButton->height(),32.0);
        const auto hwnd=reinterpret_cast<HWND>(window->winId());
        auto nativePoint=[&](QPointF local) {
            POINT point{qRound(local.x()*window->devicePixelRatio()),qRound(local.y()*window->devicePixelRatio())};
            ClientToScreen(reinterpret_cast<HWND>(window->winId()),&point);
            return MAKELPARAM(point.x,point.y);
        };
        QCOMPARE(SendMessage(hwnd,WM_NCHITTEST,0,nativePoint(maxButton->mapToScene(QPointF(23,16)))),LRESULT(HTMAXBUTTON));
        QCOMPARE(SendMessage(hwnd,WM_NCHITTEST,0,nativePoint(QPointF(250,16))),LRESULT(HTCAPTION));
        QCOMPARE(SendMessage(hwnd,WM_NCHITTEST,0,nativePoint(QPointF(1,1))),LRESULT(HTTOPLEFT));
        MSG queuedClick{};
        queuedClick.hwnd=hwnd;
        queuedClick.message=WM_NCLBUTTONDOWN;
        queuedClick.wParam=HTMAXBUTTON;
        queuedClick.lParam=nativePoint(maxButton->mapToScene(QPointF(23,16)));
        QVERIFY(frame->nativeEventFilter("windows_generic_MSG",&queuedClick,nullptr));
        QCOMPARE(frame->pressedButton(),2);
        const auto releasePoint=maxButton->mapToScene(QPointF(23,16))*window->devicePixelRatio();
        PostMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(qRound(releasePoint.x()),qRound(releasePoint.y())));
        QTRY_COMPARE(window->visibility(),QWindow::Maximized);
        QVERIFY(clickControl(window,"maximizeWindowButton"));
        QTRY_COMPARE(window->visibility(),QWindow::Windowed);
        QVERIFY(capture(window).save("titlebar-normal.png"));
        const auto normalSize=window->size();
        QVERIFY(clickControl(window,"maximizeWindowButton"));
        QTRY_COMPARE(window->visibility(),QWindow::Maximized);
        QCOMPARE(titleBar->height(),32.0);
        QCOMPARE(toolbar->mapToScene(QPointF()).y(),32.0);
        QVERIFY(capture(window).save("titlebar-maximized.png"));
        RECT client{}; GetClientRect(hwnd,&client);
        POINT origin{0,0}; ClientToScreen(hwnd,&origin);
        MONITORINFO monitor{sizeof(MONITORINFO)};
        QVERIFY(GetMonitorInfo(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor));
        QCOMPARE(origin.x,monitor.rcWork.left);
        QCOMPARE(origin.y,monitor.rcWork.top);
        QCOMPARE(client.right,monitor.rcWork.right-monitor.rcWork.left);
        QCOMPARE(client.bottom,monitor.rcWork.bottom-monitor.rcWork.top);
        QVERIFY(clickControl(window,"maximizeWindowButton"));
        QTRY_COMPARE(window->visibility(),QWindow::Windowed);
        QTRY_COMPARE(window->size(),normalSize);
        SendMessage(hwnd,WM_NCLBUTTONDBLCLK,HTCAPTION,nativePoint(QPointF(250,16)));
        QTRY_COMPARE(window->visibility(),QWindow::Maximized);
        SendMessage(hwnd,WM_NCLBUTTONDBLCLK,HTCAPTION,nativePoint(QPointF(250,16)));
        QTRY_COMPARE(window->visibility(),QWindow::Windowed);
        QVERIFY(clickControl(window,"minimizeWindowButton"));
        QTRY_COMPARE(window->visibility(),QWindow::Minimized);
        window->showNormal(); exposeForTest(window);
        editor.selectCountry("DEU"); editor.setColor("#123456");
        QVERIFY(editor.dirty());
        QVERIFY(clickControl(window,"closeWindowButton"));
        auto unsaved=window->findChild<QObject*>("unsavedDialog"); QVERIFY(unsaved);
        QTRY_VERIFY(unsaved->property("visible").toBool());
        QVERIFY(clickControl(window,"cancelUnsaved"));
        QVERIFY(window->isVisible());
        // SC_CLOSE is shared by Alt+F4 and system-menu Close, never DestroyWindow.
        SendMessage(hwnd,WM_SYSCOMMAND,SC_CLOSE,0);
        QTRY_VERIFY(unsaved->property("visible").toBool());
        QVERIFY(clickControl(window,"cancelUnsaved"));
        // The native frame can be detached/re-attached without a frameless orphan.
        frame->setEnabled(false);
        QVERIFY(!frame->active());
        QVERIFY(!titleBar->isVisible());
        QCOMPARE(toolbar->mapToScene(QPointF()).y(),0.0);
        frame->setEnabled(true);
        QTRY_VERIFY(frame->active());
        QCOMPARE(titleBar->height(),32.0);
        window->hide();
        window->destroy();
        QVERIFY(!frame->active());
        QTest::qWait(100);
        QVERIFY(!frame->active());
        window->create(); window->show();
        QTRY_VERIFY(frame->active());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QCOMPARE(SendMessage(reinterpret_cast<HWND>(window->winId()),WM_NCHITTEST,0,
                   nativePoint(maxButton->mapToScene(QPointF(23,16)))),LRESULT(HTMAXBUTTON));
        window->setProperty("allowClose",true);
        QTest::qWait(250);
        QVERIFY(clickControl(window,"closeWindowButton"));
        QTRY_VERIFY(!window->isVisible());
#endif
    }
    void mobileStorageFlow() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto privatePath=directory.filePath("private.pando.json");
        EditorController editor(EditorControllerConfig{true,privatePath});
        QQmlApplicationEngine engine;
        QStringList warnings;
        QSignalSpy errors(&editor,&EditorController::errorOccurred);
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]); QVERIFY(window);
        exposeForTest(window);
        window->resize(360,640); QTest::qWait(200);
        QCOMPARE(window->minimumWidth(),0);
        QCOMPARE(window->minimumHeight(),0);
        auto desktopTitleBar=visualItem(window->contentItem(),"desktopTitleBar"); QVERIFY(desktopTitleBar);
        QVERIFY(!desktopTitleBar->isVisible());
        QVERIFY(!window->flags().testFlag(Qt::FramelessWindowHint));

        auto toolbar=visualItem(window->contentItem(),"storageToolbar"); QVERIFY(toolbar);
        const QStringList buttonNames{"importButton","deviceSaveButton","exportButton","undoButton","redoButton"};
        QList<QQuickItem*> buttons;
        for(const auto& name:buttonNames) {
            auto button=visualItem(window->contentItem(),name); QVERIFY2(button,qPrintable(name));
            QVERIFY(button->isVisible());
            const auto bounds=button->mapRectToItem(toolbar,QRectF(0,0,button->width(),button->height()));
            QVERIFY2(bounds.left()>=-0.5 && bounds.right()<=toolbar->width()+0.5,qPrintable(name));
            buttons.append(button);
        }
        QCOMPARE(buttons[0]->property("text").toString(),QString("가져오기"));
        QCOMPARE(buttons[1]->property("text").toString(),QString("기기에 저장"));
        QCOMPARE(buttons[2]->property("text").toString(),QString("내보내기"));

        editor.selectCountry("DEU");
        editor.setMemoDraft("rotation draft");
        window->resize(720,360); QTest::qWait(120);
        window->resize(360,720); QTest::qWait(120);
        QCOMPARE(editor.selectedId(),QString("DEU"));
        QCOMPARE(editor.memoDraft(),QString("rotation draft"));
        QVERIFY(!editor.canUndo());
        auto unsaved=window->findChild<QObject*>("unsavedDialog"); QVERIFY(unsaved);
        QVERIFY(clickControl(window,"countryPicker"));
        auto picker=visualItem(window->contentItem(),"countryPicker"); QVERIFY(picker);
        auto pickerPopup=qvariant_cast<QObject*>(picker->property("popup")); QVERIFY(pickerPopup);
        QTRY_VERIFY(pickerPopup->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(window,"handleBack"));
        QTRY_VERIFY(!pickerPopup->property("visible").toBool());
        QVERIFY(!unsaved->property("visible").toBool());
        QVERIFY(clickControl(window,"countryColor"));
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); typeText(window,"#bad");
        QCOMPARE(editor.colorDraft(),QString("#bad"));
        QVERIFY(clickControl(window,"importButton"));
        QTRY_VERIFY(unsaved->property("visible").toBool());
        QCOMPARE(editor.memoDraft(),QString("rotation draft"));
        QCOMPARE(editor.colorDraft(),QString("#bad"));
        QVERIFY(!editor.canUndo());
        QCOMPARE(errors.count(),0);
        QVERIFY(clickControl(window,"cancelUnsaved"));
        QCOMPARE(editor.memoDraft(),QString("rotation draft"));
        QCOMPARE(editor.colorDraft(),QString("#bad"));
        QVERIFY(!editor.canUndo());
        editor.setColorDraft(editor.colors()["DEU"].toString());

        QVERIFY(clickControl(window,"deviceSaveButton"));
        QVERIFY(QFile::exists(privatePath));
        QVERIFY(!editor.dirty());
        QVERIFY(capture(window).save("mobile-storage.png"));
        editor.setColor("#123456");
        QVERIFY(editor.dirty());
        QVERIFY(QMetaObject::invokeMethod(window,"requestExport"));
        QVERIFY(QFile::exists(privatePath));
        QVERIFY(!editor.dirty());

        editor.setColor("#654321");
        QVERIFY(QMetaObject::invokeMethod(window,"handleBack"));
        QTRY_VERIFY(unsaved->property("visible").toBool());
        QVERIFY(clickControl(window,"cancelUnsaved"));
        QVERIFY(window->isVisible());
        QVERIFY(editor.dirty());
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->setProperty("allowClose",true); window->close();
    }

    void mobileCorruptRecoveryCanBeReopened() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto privatePath=directory.filePath("private.pando.json");
        QFile corrupt(privatePath);
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        QCOMPARE(corrupt.write("corrupt project"),qint64(15));
        corrupt.close();

        EditorController editor(EditorControllerConfig{true,privatePath});
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& values){for(const auto& e:values) warnings<<e.toString();});
        engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]); QVERIFY(window);
        exposeForTest(window);
        window->resize(360,640); QTest::qWait(120);
        auto recovery=window->findChild<QObject*>("recoveryDialog"); QVERIFY(recovery);
        QTRY_VERIFY(recovery->property("visible").toBool());
        QVERIFY(clickControl(window,"cancelRecovery"));
        QTRY_VERIFY(!recovery->property("visible").toBool());
        QTest::qWait(250); // let the modal exit transition release its input overlay
        QCOMPARE(readFile(privatePath),QByteArray("corrupt project"));

        editor.selectCountry("DEU"); editor.setColor("#123456");
        QVERIFY(clickControl(window,"deviceSaveButton"));
        QTRY_VERIFY(recovery->property("visible").toBool());
        QVERIFY(clickControl(window,"confirmRecovery"));
        QTRY_VERIFY(!recovery->property("visible").toBool());
        QCOMPARE(readFile(privatePath+".corrupt"),QByteArray("corrupt project"));
        QVERIFY(clickControl(window,"deviceSaveButton"));
        QVERIFY(!editor.dirty());
        QVERIFY(readFile(privatePath).startsWith('{'));
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->setProperty("allowClose",true); window->close();
    }

    void attributesAndLayers() {
        EditorController editor;
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
        engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]); QVERIFY(window);
        exposeForTest(window);
        editor.selectCountry("DEU");
        QVERIFY(clickControl(window,"countryName"));
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); typeText(window,"Draft name");
        QCOMPARE(editor.nameDraft(),QString("Draft name"));
        window->resize(390,760); QTest::qWait(200);
        QCOMPARE(editor.nameDraft(),QString("Draft name"));
        QTest::keyClick(window,Qt::Key_Return);
        QCOMPARE(editor.selectedName(),QString("Draft name"));
        QVERIFY(!editor.hasPendingEdits()); // web name change commits independently, no whole-form Apply.
        QVERIFY(clickControl(window,"countryColor"));
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); typeText(window,"#123456");
        QTest::keyClick(window,Qt::Key_Return);
        QVERIFY(clickControl(window,"applyEdits"));
        QTRY_COMPARE(editor.colors()["DEU"].toString(),QString("#123456"));
        // Position the slider in the scroll area, then discard the positioning
        // click's draft before measuring the actual drag's baseline.
        QVERIFY(clickControl(window,"countryOpacity"));
        editor.discardPendingEdits();
        // A drag remains a draft until Apply; Undo restores its baseline.
        auto slider=visualItem(window->contentItem(),"countryOpacity"); QVERIFY(slider);
        double before=editor.countryOpacity();
        window->grabWindow(); QTest::qWait(100); // settle the reset handle position
        auto handle=qvariant_cast<QQuickItem*>(slider->property("handle")); QVERIFY(handle);
        auto start=handle->mapToScene(QPointF(handle->width()/2,handle->height()/2)).toPoint();
        auto end=slider->mapToScene(QPointF(slider->width()*0.2,slider->height()/2)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
        QTest::mouseMove(window,(start+end)/2,20); QTest::mouseMove(window,end,20);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,end);
        QVERIFY(editor.countryOpacity()<before);
        QVERIFY(clickControl(window,"applyEdits"));
        QTRY_VERIFY(!editor.jobBusy());
        editor.undo(); QCOMPARE(editor.countryOpacity(),before); editor.redo();
        QVERIFY(clickControl(window,"layersTab"));
        QVERIFY(clickControl(window,"addLayer"));
        auto id=editor.selectedLayerId(); QVERIFY(id!="countries");
        QVERIFY(clickControl(window,"layerName"));
        QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); typeText(window,"Upper");
        QTest::keyClick(window,Qt::Key_Return);
        QCOMPARE(editor.layerNameDraft(),QString("Upper"));
        editor.moveCountry(id); QCOMPARE(editor.countryLayerId(),id);
        QVERIFY(!editor.canDeleteLayer());
        QVERIFY(clickControl(window,"layerLocked"));
        QCOMPARE(editor.selectedId(),QString("DEU"));
        QVERIFY(!editor.selectedEditable());
        editor.selectCountry("DEU"); QVERIFY(!editor.selectedEditable());
        QVERIFY(clickControl(window,"layerLocked"));
        editor.selectCountry("DEU"); QVERIFY(editor.selectedEditable());
        QVERIFY(clickControl(window,"layerVisible")); QCOMPARE(editor.selectedId(),QString("DEU"));
        QVERIFY(!editor.countryVisuals()["DEU"].toMap()["visible"].toBool());
        QVERIFY(clickControl(window,"layerVisible"));
        editor.selectCountry("DEU");
        editor.previewCountryOpacity(0); QVERIFY(editor.commitPendingEdits());
        editor.selectCountry("DEU"); QVERIFY(editor.selectedEditable());
        QTemporaryDir dir; auto path=QUrl::fromLocalFile(dir.path()+QString::fromUtf8("/속성 레이어.pando.json"));
        QVERIFY(editor.saveFile(path)); QVERIFY(editor.openFile(path));
        editor.selectCountry("DEU"); QCOMPARE(editor.selectedName(),QString("Draft name"));
        QCOMPARE(editor.countryLayerId(),id); QCOMPARE(editor.countryOpacity(),0.0);
        capture(window).save("layers-compact.png");
        window->resize(1100,720); QTest::qWait(150); capture(window).save("layers-desktop.png");
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->close();
    }
    void webFieldsAndAsyncApplyCancelAcrossPcAnd360px() {
        for(bool mobile:{false,true}) {
            QTemporaryDir dir; QVERIFY(dir.isValid());
            EditorController editor(EditorControllerConfig{mobile,dir.filePath("private.json")});
            QQmlApplicationEngine engine; QStringList warnings;
            connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
            engine.rootContext()->setContextProperty("editor",&editor);
            engine.load(QUrl("qrc:/common/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
            auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]); QVERIFY(window);
            window->resize(mobile?360:1100,mobile?640:760); exposeForTest(window);
            editor.selectCountry("DEU");
            const auto name=editor.selectedName(),layerName=editor.layerNameDraft(),color=editor.colorDraft();
            QVERIFY(clickControl(window,"countryName"));
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); typeText(window,"Web name");
            QVERIFY(clickControl(window,"countryMemo"));
            // Web change events commit each text field independently, without Apply.
            QCOMPARE(editor.selectedName(),QString("Web name")); QCOMPARE(editor.revision(),qulonglong(1));
            typeText(window,"Web notes"); QVERIFY(clickControl(window,"countryColor"));
            QCOMPARE(editor.revision(),qulonglong(2));
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); typeText(window,"#102030");
            QVERIFY(clickControl(window,"layersTab"));
            QVERIFY(clickControl(window,"layerName"));
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); typeText(window,"Atomic layer");
            QVERIFY(clickControl(window,"applyEdits")); QTRY_COMPARE(editor.revision(),qulonglong(3));
            QCOMPARE(editor.colorDraft(),QString("#102030")); QCOMPARE(editor.layerNameDraft(),QString("Atomic layer"));
            QVERIFY(!editor.hasPendingEdits());
            QVERIFY(clickControl(window,"undoButton"));
            QCOMPARE(editor.selectedName(),QString("Web name")); QCOMPARE(editor.memoDraft(),QString("Web notes"));
            QCOMPARE(editor.colorDraft(),color); QCOMPARE(editor.layerNameDraft(),layerName);
            QVERIFY(clickControl(window,"undoButton")); QCOMPARE(editor.memoDraft(),QString());
            QVERIFY(clickControl(window,"undoButton")); QCOMPARE(editor.selectedName(),name);
            QCOMPARE(editor.revision(),qulonglong(6)); QVERIFY(!editor.canUndo()); QVERIFY(!editor.dirty());
            for(int i=0;i<3;++i) QVERIFY(clickControl(window,"redoButton"));
            QCOMPARE(editor.revision(),qulonglong(9));
            QVERIFY(clickControl(window,"layerName"));
            QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier); typeText(window,"Discard me");
            QVERIFY(clickControl(window,"cancelEdits"));
            QCOMPARE(editor.layerNameDraft(),QString("Atomic layer")); QCOMPARE(editor.revision(),qulonglong(9));
            editor.setLayerNameDraft("Keep cancelled draft"); QVERIFY(editor.applyPendingEditsAsync()); QVERIFY(editor.jobBusy());
            auto cancel=visualItem(window->contentItem(),"cancelBackgroundWork"); QVERIFY(cancel);
            // Directly dispatch the real button signal before processing queued
            // completion events, so fast machines cannot race this cancellation.
            QVERIFY(QMetaObject::invokeMethod(cancel,"clicked")); QVERIFY(!editor.jobBusy());
            QCoreApplication::processEvents(); QCOMPARE(editor.revision(),qulonglong(9));
            QCOMPARE(editor.layerNameDraft(),QString("Keep cancelled draft")); editor.discardPendingEdits();
            QVERIFY(capture(window).save(mobile?"jobs-mobile-360.png":"jobs-desktop.png"));
            QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
            window->setProperty("allowClose",true); window->close();
        }
    }
    void territorialStructureDeleteDialogAcrossPcAnd360px() {
        for(bool mobile:{false,true}) {
            EditorController editor(EditorControllerConfig{mobile,{}}); QQmlApplicationEngine engine; QStringList warnings;
            connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
            engine.rootContext()->setContextProperty("editor",&editor);engine.load(QUrl("qrc:/common/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());
            auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);QVERIFY(window);window->resize(mobile?360:1100,mobile?640:760);exposeForTest(window);QVERIFY(clickControl(window,"countryTab"));editor.selectCountry("DEU");
            auto panel=visualItem(window->contentItem(),"territorialStructurePanel");QVERIFY(panel&&panel->isVisible());
            auto deleteButton=visualItem(window->contentItem(),"deleteTerritorial");QVERIFY(deleteButton&&deleteButton->isVisible()&&deleteButton->isEnabled());
            QVERIFY(QMetaObject::invokeMethod(deleteButton,"clicked"));QTRY_VERIFY(editor.structureDialogOpen());
            auto dialog=window->findChild<QObject*>("territorialStructureDialog");QVERIFY(dialog&&dialog->property("visible").toBool());
            QVERIFY(capture(window).save(mobile?"structure-mobile-360.png":"structure-desktop-1100.png"));
            QVERIFY(clickControl(window,"confirmTerritorialStructure"));QTRY_VERIFY(!editor.structureDialogOpen());editor.undo();editor.selectCountry("DEU");QCOMPARE(editor.selectedId(),QString("DEU"));
            QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));window->setProperty("allowClose",true);window->close();
        }
    }
    void territorialConversionSetupDoesNotMutateDocumentAcrossPcAnd360px() {
        for(bool mobile:{false,true}) {
            EditorController editor(EditorControllerConfig{mobile,{}}); QQmlApplicationEngine engine; QStringList warnings;
            connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors) warnings<<e.toString();});
            engine.rootContext()->setContextProperty("editor",&editor);engine.load(QUrl("qrc:/common/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());
            auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);QVERIFY(window);window->resize(mobile?360:1100,mobile?640:760);exposeForTest(window);QVERIFY(clickControl(window,"countryTab"));editor.selectCountry("DEU");
            const auto revision=editor.revision();auto button=visualItem(window->contentItem(),"convertTerritorial");QVERIFY(button&&button->isVisible());QVERIFY(QMetaObject::invokeMethod(button,"clicked"));QTRY_VERIFY(editor.structureDialogOpen());
            QCOMPARE(editor.revision(),revision);QVERIFY(editor.structureState().value("conversionSetup").toBool());QVERIFY(!editor.structureState().value("generatedId").toString().isEmpty());
            auto dialog=window->findChild<QObject*>("territorialStructureDialog");QVERIFY(dialog&&dialog->property("visible").toBool());auto confirm=visualItem(window->contentItem(),"confirmTerritorialStructure");QVERIFY(confirm&&!confirm->isEnabled());
            editor.cancelStructureMutation();QTRY_VERIFY(!editor.structureDialogOpen());QCOMPARE(editor.revision(),revision);
            QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));window->setProperty("allowClose",true);window->close();
        }
    }
    void territorialCreateSetupWaitsForPreparedGeometryAcrossPcAnd360px() {
        using namespace pandoeditor;
        for(bool mobile:{false,true}) {
            EditorController editor(EditorControllerConfig{mobile,{}});QQmlApplicationEngine engine;QStringList warnings;connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto& e:errors)warnings<<e.toString();});engine.rootContext()->setContextProperty("editor",&editor);engine.load(QUrl("qrc:/common/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]);QVERIFY(window);window->resize(mobile?360:1100,mobile?640:760);exposeForTest(window);QVERIFY(clickControl(window,"countryTab"));editor.selectCountry("DEU");
            const auto revision=editor.revision();auto add=visualItem(window->contentItem(),"createRegion");QVERIFY(add&&add->isVisible());QVERIFY(QMetaObject::invokeMethod(add,"clicked"));QTRY_VERIFY(editor.structureDialogOpen());QVERIFY(editor.structureState().value("createSetup").toBool());QVERIFY(editor.structureState().value("geometryRequired").toBool());QCOMPARE(editor.revision(),revision);auto confirm=visualItem(window->contentItem(),"confirmTerritorialStructure");QVERIFY(confirm&&!confirm->isEnabled());editor.cancelStructureMutation();
            Geometry geometry;geometry.type="Polygon";geometry.polygons.push_back(pandoeditor::Polygon{Ring{{30,30},{31,30},{31,31},{30,31},{30,30}}});CreateTerritorialIntent intent;intent.kind=UnitKind::Region;intent.id=mobile?"prepared-mobile":"prepared-desktop";intent.name="Prepared";intent.geometry=geometry;QVERIFY(editor.beginTerritorialCreatePrepared(intent));QVERIFY(!editor.structureState().value("geometryRequired").toBool());QVERIFY(editor.confirmStructureMutation());QCOMPARE(editor.revision(),revision+1);editor.undo();QCOMPARE(editor.revision(),revision+2);
            QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));window->setProperty("allowClose",true);window->close();
        }
    }
    void compositing() {
        using namespace pandoeditor;
        Project project;
        project.replace(ProjectDocument{
            {{"A","Red",{{{{0,0},{4,0},{4,4},{0,4},{0,0}}}},0xff0000},
             {"B","Blue",{{{{2,0},{6,0},{6,4},{2,4},{2,0}}}},0x0000ff}},
            {{"countries","Base",true,false,0.5}}});
        QTemporaryDir dir;
        auto path=QUrl::fromLocalFile(dir.path()+"/overlap.pando.json");
        QFile file(path.toLocalFile()); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(projectcodec::encode(project)); file.close();
        EditorController editor; QVERIFY(editor.openFile(path));
        QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]); exposeForTest(window);
        auto map=window->findChild<QQuickItem*>("mapView"); QVERIFY(map);
        auto pixel=[&](double lon,double lat) {
            auto image=capture(window);
            auto scale=map->property("mapScale").toDouble();
            auto point=map->mapToScene(QPointF(map->property("originX").toDouble()+lon/6*editor.mapWidth()*scale,
                                              map->property("originY").toDouble()+(4-lat)/4*editor.mapHeight()*scale));
            return image.pixelColor((point*image.devicePixelRatio()).toPoint());
        };
        auto near=[](QColor a,QColor b) {return std::abs(a.red()-b.red())<=3 && std::abs(a.green()-b.green())<=3 && std::abs(a.blue()-b.blue())<=3;};
        // Two overlapping opaque countries are composited once at 50% layer opacity.
        auto overlap=pixel(3,2); QVERIFY2(near(overlap,QColor(116,120,250)),qPrintable(overlap.name()));
        auto red=pixel(1,2); QVERIFY2(near(red,QColor(244,120,122)),qPrintable(red.name()));
        editor.selectCountry("A"); editor.previewCountryOpacity(0.5); QVERIFY(editor.commitPendingEdits());
        editor.selectCountry("");
        auto quarter=pixel(1,2); QVERIFY2(near(quarter,QColor(238,179,183)),qPrintable(quarter.name()));
        editor.selectCountry("A"); editor.previewCountryOpacity(1); QVERIFY(editor.commitPendingEdits());
        editor.selectLayer("countries"); editor.previewLayerOpacity(1); QVERIFY(editor.commitPendingEdits());
        editor.addLayer(); auto top=editor.selectedLayerId();
        editor.selectCountry("B"); editor.moveCountry(top);
        editor.previewLayerOpacity(0.5); QVERIFY(editor.commitPendingEdits()); editor.selectCountry("");
        auto purple=pixel(3,2); QVERIFY2(near(purple,QColor(127,0,128)),qPrintable(purple.name()));
        editor.selectCountry("B"); editor.previewCountryOpacity(0.5); QVERIFY(editor.commitPendingEdits()); editor.selectCountry("");
        auto nested=pixel(3,2); QVERIFY2(near(nested,QColor(191,0,64)),qPrintable(nested.name()));
        editor.moveLayer(-1);
        auto reordered=pixel(3,2); QVERIFY2(near(reordered,QColor(255,0,0)),qPrintable(reordered.name()));
        editor.undo(); QVERIFY(near(pixel(3,2),QColor(191,0,64)));
        editor.setLayerVisible(false); QVERIFY(near(pixel(3,2),QColor(255,0,0)));
        editor.setLayerVisible(true); editor.setLayerLocked(true);
        QVERIFY(near(pixel(3,2),QColor(191,0,64)));
        window->setProperty("allowClose",true); window->close();
    }
    void editingFlow() {
        EditorController editor;
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){ for(const auto& e:errors) warnings<<e.toString(); });
        engine.rootContext()->setContextProperty("editor",&editor);
        engine.load(QUrl("qrc:/common/Main.qml"));
        QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects()[0]); QVERIFY(window);
        if (qEnvironmentVariable("QT_QPA_PLATFORM")=="windows") {
            window->hide(); window->show();
            QVERIFY(QTest::qWaitForWindowExposed(window));
        }
        QTest::qWait(500);
        // Hidden Windows launches defer scene polish until the first render.
        QVERIFY(!window->grabWindow().isNull());
        auto map=window->findChild<QQuickItem*>("mapView"); QVERIFY(map);
        auto panel=window->findChild<QQuickItem*>("editorPanel"); QVERIFY(panel);
        QVERIFY(!panel->property("compact").toBool());
        auto clickItem=[&](const char* name) {
            enterExistingControlRoute(window,QString::fromLatin1(name));
            QTest::qWait(80); // settle layout before reading delegate coordinates
            window->grabWindow();
            std::function<QQuickItem*(QQuickItem*)> find=[&](QQuickItem* node)->QQuickItem* {
                if (node->objectName()==QString::fromLatin1(name)) return node;
                for(auto child:node->childItems()) if(auto result=find(child)) return result;
                return nullptr;
            };
            auto item=find(window->contentItem());
            if (!item) return false;
            const auto center=item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint();
            if (!QRect(QPoint(0,0),window->size()).contains(center)) return false;
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,center);
            QTest::qWait(50); return true;
        };
        double px=0,py=0;
        // Find a point inside Germany, then exercise actual UI hit testing with a click.
        for (int y=50;y<99 && editor.selectedId()!="DEU";++y) for(int x=20;x<99;++x) {
            px=editor.mapWidth()*x/100; py=editor.mapHeight()*y/100;
            editor.selectAt(px,py); if(editor.selectedId()=="DEU") break;
        }
        QCOMPARE(editor.selectedId(),QString("DEU"));
        editor.selectAt(-100,-100);
        auto clickGermany=[&]() {
            auto scale=map->property("mapScale").toDouble();
            auto local=QPointF((map->width()-editor.mapWidth()*scale)/2+px*scale+map->property("panX").toDouble(),
                              (map->height()-editor.mapHeight()*scale)/2+py*scale+map->property("panY").toDouble());
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,map->mapToScene(local).toPoint());
        };
        clickGermany(); QTRY_COMPARE(editor.selectedId(),QString("DEU"));
        QVERIFY(QMetaObject::invokeMethod(map,"zoomAt",Q_ARG(QVariant,1.5),Q_ARG(QVariant,map->width()/2),Q_ARG(QVariant,map->height()/2)));
        QCOMPARE(map->property("zoom").toDouble(),1.5);
        QVERIFY(QMetaObject::invokeMethod(map,"fit"));
        auto dragStart=map->mapToScene(QPointF(10,100)).toPoint();
        QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,dragStart);
        QTest::mouseMove(window,dragStart+QPoint(25,0),30);
        QTest::mouseMove(window,dragStart+QPoint(60,20),30);
        QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,dragStart+QPoint(60,20));
        QVERIFY(map->property("panX").toDouble()!=0);
        QCOMPARE(editor.selectedId(),QString("DEU"));
        editor.selectAt(-100,-100); clickGermany(); QTRY_COMPARE(editor.selectedId(),QString("DEU"));
        QVERIFY(QMetaObject::invokeMethod(map,"fit"));
        QVERIFY(clickItem("swatche56b6f")); QVERIFY(editor.dirty());
        QCOMPARE(editor.colors()["DEU"].toString(),QString("#e56b6f"));
        QVERIFY(clickItem("undoButton")); QVERIFY(!editor.dirty());
        QVERIFY(clickItem("redoButton")); QVERIFY(editor.dirty());
        QTest::qWait(150);
        window->grabWindow(); QTest::qWait(100);
        auto desktop=window->grabWindow(); QVERIFY(!desktop.isNull()); QVERIFY(desktop.save("desktop.png"));
        window->resize(390,760); QTest::qWait(300);
        QVERIFY(panel->property("compact").toBool());
        QCOMPARE(editor.selectedId(),QString("DEU"));
        QVERIFY(editor.dirty());
        QVERIFY(map->height()>200);
        QVERIFY(clickItem("swatch499c91"));
        QCOMPARE(editor.colors()["DEU"].toString(),QString("#499c91"));
        QVERIFY(clickItem("undoButton"));
        QCOMPARE(editor.colors()["DEU"].toString(),QString("#e56b6f"));
        QVERIFY(clickItem("redoButton"));
        QCOMPARE(editor.colors()["DEU"].toString(),QString("#499c91"));
        QTest::qWait(150);
        window->grabWindow(); QTest::qWait(100);
        auto mobile=window->grabWindow(); QVERIFY(!mobile.isNull()); QVERIFY(mobile.save("compact.png"));
        auto scale=map->property("mapScale").toDouble();
        auto colorPoint=map->mapToScene(QPointF((map->width()-editor.mapWidth()*scale)/2+px*scale,
                                              (map->height()-editor.mapHeight()*scale)/2+py*scale));
        QCOMPARE(mobile.pixelColor((colorPoint*mobile.devicePixelRatio()).toPoint()).name(),QString("#499c91"));
        QTemporaryDir temporary;
        auto path=QUrl::fromLocalFile(temporary.path()+QString::fromUtf8("/화면 테스트.pando.json"));
        QVERIFY(editor.saveFile(path)); QVERIFY(!editor.dirty());
        editor.setColor("#a8c7db"); QVERIFY(editor.dirty());
        QVERIFY(QMetaObject::invokeMethod(window,"requestAction",Q_ARG(QVariant,QVariant("open"))));
        auto unsaved=window->findChild<QObject*>("unsavedDialog"); QVERIFY(unsaved);
        QTRY_VERIFY(unsaved->property("visible").toBool());
        QTest::qWait(200);
        QVERIFY(clickItem("cancelUnsaved"));
        QTRY_VERIFY(!unsaved->property("visible").toBool());
        QVERIFY(editor.dirty());
        // Cancelling a Save As dialog also cancels the deferred destructive action.
        auto saveDialog=window->findChild<QObject*>("saveDialog"); QVERIFY(saveDialog);
        window->setProperty("pendingAction","close");
        QVERIFY(QMetaObject::invokeMethod(saveDialog,"rejected"));
        QCOMPARE(window->property("pendingAction").toString(),QString());
        QVERIFY(window->isVisible() && editor.dirty());
        window->close(); QTRY_VERIFY(unsaved->property("visible").toBool());
        QTest::qWait(200); QVERIFY(clickItem("cancelUnsaved"));
        QVERIFY(window->isVisible());
        QVERIFY(editor.openFile(path));
        QCOMPARE(editor.colors()["DEU"].toString(),QString("#499c91"));
        QVERIFY(editor.selectedId().isEmpty());
        QVERIFY(!editor.canUndo());
        QCOMPARE(map->property("zoom").toDouble(),1.0);
        QTest::qWait(250); // let the modal exit transition release its input overlay
        clickGermany(); QTRY_COMPARE(editor.selectedId(),QString("DEU"));
        editor.setColor("#e56b6f");
        window->close(); QTRY_VERIFY(unsaved->property("visible").toBool());
        QTest::qWait(200); QVERIFY(clickItem("saveUnsaved"));
        QVERIFY(!editor.dirty());
        QVERIFY(!window->isVisible());
        window->setProperty("allowClose",false); window->show();
        editor.setColor("#a8c7db");
        window->close(); QTRY_VERIFY(unsaved->property("visible").toBool());
        QTest::qWait(200); QVERIFY(clickItem("discardUnsaved"));
        QVERIFY(!window->isVisible());
        QVERIFY(editor.openFile(path));
        QCOMPARE(editor.colors()["DEU"].toString(),QString("#e56b6f"));
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        window->close();
    }
};
int main(int argc,char** argv) {
    QQuickStyle::setStyle("Basic");
    QGuiApplication app(argc,argv);
    registerWindowsFrameType();
    if (qEnvironmentVariable("QT_QPA_PLATFORM")=="offscreen") {
        int font=QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/malgun.ttf");
        if (font>=0) app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).first()));
    }
    UiTests test; return QTest::qExec(&test,argc,argv);
}
#include "ui_tests.moc"
