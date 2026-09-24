#include "hydrodataprovider.h"
#include <QtTest>
#include <QDir>
#include <QFileInfo>

#ifndef WEB_HYDRO_FIXTURE
#error WEB_HYDRO_FIXTURE must identify the pinned miniature dataset
#endif

class HydroManifestTests : public QObject {
    Q_OBJECT
private slots:
    void resolvesIndexFromSiblingVersion() {
        const QString manifest=QStringLiteral(WEB_HYDRO_FIXTURE)+"/v0.13.1/manifest.json";
        QVERIFY(QFileInfo::exists(manifest));
        const auto inspected=inspectHydroData(manifest);
        QVERIFY2(inspected.ready,qPrintable(inspected.error));
        QCOMPARE(inspected.version,QStringLiteral("0.13.1"));
        QCOMPARE(QDir(inspected.root).canonicalPath(),
                 QDir(QStringLiteral(WEB_HYDRO_FIXTURE)+"/v0.13.1").canonicalPath());
    }
};

QTEST_GUILESS_MAIN(HydroManifestTests)
#include "hydro_manifest_tests.moc"
