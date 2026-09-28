#include <pandoeditor/version.h>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QString>
#include <QQmlContext>
#include "editorcontroller.h"
#include "windowsframe.h"
#include "terrainimageprovider.h"
#include <QQuickStyle>
#include <QStandardPaths>
#include <QDir>

#include <cstdlib>

int main(int argc, char *argv[])
{
    QQuickStyle::setStyle("Basic");
    QGuiApplication application(argc, argv);
    registerWindowsFrameType();
    QCoreApplication::setApplicationName(QStringLiteral("Pandoeditor"));

    const auto version = pandoeditor::version();
    QCoreApplication::setApplicationVersion(
        QString::fromUtf8(version.data(), static_cast<qsizetype>(version.size())));

    EditorControllerConfig editorConfig;
    editorConfig.bootstrapWorld=true;
    editorConfig.autosaveEnabled=true;
    editorConfig.projectPreviewEnabled=true;
    editorConfig.appearancePath=QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QStringLiteral("appearance-v2.json"));
    EditorController editor(editorConfig);
    QQmlApplicationEngine engine;
    auto* terrainImages=new TerrainImageProvider;
    terrainImages->setSource(editor.terrainProviderSnapshot());
    engine.addImageProvider(QStringLiteral("terrain"),terrainImages);
    QObject::connect(&editor,&EditorController::terrainChanged,&engine,[&editor,terrainImages] {
        terrainImages->setSource(editor.terrainProviderSnapshot());
    });
    engine.rootContext()->setContextProperty("editor", &editor);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &application, [] { QCoreApplication::exit(EXIT_FAILURE); },
                     Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/common/Main.qml")));

    return application.exec();
}
