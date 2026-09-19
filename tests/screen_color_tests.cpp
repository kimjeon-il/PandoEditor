#include "screencolorpicker.h"
#include <QBackingStore>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>
#include <QWindow>
class SolidWindow final:public QWindow {
    QBackingStore store{this};
    void paint(){if(!isExposed())return;store.resize(size());QRect r(QPoint(),size());store.beginPaint(r);{QPainter p(store.paintDevice());p.fillRect(r,QColor("#2764a3"));}store.endPaint();store.flush(r);}
    void exposeEvent(QExposeEvent*)override{paint();}
    void resizeEvent(QResizeEvent*)override{paint();}
public: SolidWindow(){setSurfaceType(QSurface::RasterSurface);setGeometry(80,80,400,260);}
};
class ScreenColorTests:public QObject {
    Q_OBJECT
private slots:
    void wholeScreenSelectionAndCancellation(){
        ScreenColorPicker picker;QSignalSpy selected(&picker,&ScreenColorPicker::colorSelected),cancelled(&picker,&ScreenColorPicker::cancelled);
        if(!picker.available()){
            QVERIFY(!picker.start());QVERIFY(!picker.busy());picker.cancel();QCOMPARE(selected.count(),0);QCOMPARE(cancelled.count(),0);
            return; // This explicitly tests unsupported-host behavior, not sampling.
        }
        SolidWindow base;base.show();QVERIFY(QTest::qWaitForWindowExposed(&base));QTest::qWait(100);
        QVERIFY(picker.start());QVERIFY(picker.busy());
        QWindow* overlay=nullptr;for(auto window:QGuiApplication::allWindows())if(window->objectName()=="screenColorPickerOverlay")overlay=window;
        QVERIFY(overlay);QVERIFY(QTest::qWaitForWindowExposed(overlay));
        const auto point=overlay->mapFromGlobal(base.mapToGlobal(QPoint(100,100)));
        QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,point);
        QTRY_COMPARE(selected.count(),1);QCOMPARE(selected.front().front().toString(),QString("#2764a3"));QVERIFY(!picker.busy());
        QVERIFY(picker.start());for(auto window:QGuiApplication::allWindows())if(window->objectName()=="screenColorPickerOverlay")overlay=window;
        QVERIFY(overlay);QTest::keyClick(overlay,Qt::Key_Escape);QTRY_COMPARE(cancelled.count(),1);QVERIFY(!picker.busy());QCOMPARE(selected.count(),1);
        // A selection queued just before cancellation cannot publish a stale result.
        QVERIFY(picker.start());for(auto window:QGuiApplication::allWindows())if(window->objectName()=="screenColorPickerOverlay")overlay=window;
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(point),QPointF(overlay->mapToGlobal(point)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QCoreApplication::sendEvent(overlay,&release);picker.cancel();QCoreApplication::processEvents();
        QCOMPARE(selected.count(),1);QVERIFY(!picker.busy());
    }
};
QTEST_MAIN(ScreenColorTests)
#include "screen_color_tests.moc"
