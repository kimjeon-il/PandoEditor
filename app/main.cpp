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
#include <QTimer>
#include <QFont>
#include <QRawFont>
#include <QImage>
#include <QDebug>

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

    const bool smokeCheck=QCoreApplication::arguments().contains(QStringLiteral("--smoke-check"));
    if(smokeCheck)QStandardPaths::setTestModeEnabled(true);
    EditorControllerConfig editorConfig;
    editorConfig.bootstrapWorld=!smokeCheck;
    editorConfig.autosaveEnabled=!smokeCheck;
    editorConfig.projectPreviewEnabled=!smokeCheck;
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

    if(smokeCheck)QTimer::singleShot(1500,&application,[&] {
        if(engine.rootObjects().isEmpty()){application.exit(EXIT_FAILURE);return;}
        const auto font=engine.rootObjects().front()->property("font").value<QFont>();
        const auto rawFont=QRawFont::fromFont(font);
        const bool fontReady=font.family().contains(QStringLiteral("Pretendard"))&&rawFont.supportsCharacter(0xD310);
        const bool flagReady=!QImage(QStringLiteral(":/defaults/flags/native/ad.svg")).isNull();
        qInfo().noquote()<<QStringLiteral("PORTABLE_SMOKE qml=PASS font=%1 korean=%2 svg=%3 autosave=OFF world=OFF")
            .arg(font.family()).arg(fontReady?"PASS":"FAIL").arg(flagReady?"PASS":"FAIL");
        application.exit(fontReady&&flagReady?EXIT_SUCCESS:EXIT_FAILURE);
    });

    return application.exec();
}
