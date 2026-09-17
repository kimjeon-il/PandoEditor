#include <pandoeditor/version.h>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QString>
#include <QQmlContext>
#include "editorcontroller.h"
#include <QQuickStyle>

#include <cstdlib>

int main(int argc, char *argv[])
{
    QQuickStyle::setStyle("Basic");
    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Pandoeditor"));

    const auto version = pandoeditor::version();
    QCoreApplication::setApplicationVersion(
        QString::fromUtf8(version.data(), static_cast<qsizetype>(version.size())));

    EditorController editor;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("editor", &editor);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &application, [] { QCoreApplication::exit(EXIT_FAILURE); },
                     Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/common/Main.qml")));

    return application.exec();
}
