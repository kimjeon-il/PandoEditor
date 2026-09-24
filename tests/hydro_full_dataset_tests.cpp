#include "hydromanifest.h"
#include "hydroruntimeprovider.h"
#include <QtTest>
#include <QSignalSpy>
#include <QFileInfo>

class HydroFullDatasetTests:public QObject {
    Q_OBJECT
private slots:
    void loadsReal0131IndexMetadataAndViewport(){
        const auto path=qEnvironmentVariable("PANDOEDITOR_HYDRO_FULL_MANIFEST");
        if(path.isEmpty())QSKIP("Set PANDOEDITOR_HYDRO_FULL_MANIFEST to run the optional real dataset gate");
        QVERIFY2(QFileInfo::exists(path),qPrintable(path));
        const auto manifest=readHydroManifest(path);
        QVERIFY2(manifest.valid(),qPrintable(manifest.error));
        QCOMPARE(manifest.indexTileCount,953);
        QCOMPARE(manifest.logicalFeatureCount,5173);
        QCOMPARE(manifest.metadataFeatureCount,16548);
        HydroRuntimeProvider provider;QString error;
        QVERIFY2(provider.open(path,"full-dataset",false,error),qPrintable(error));
        QCOMPARE(provider.coreMetadata()->size(),16548);
        QSignalSpy accepted(&provider,&HydroRuntimeProvider::frameChanged);
        provider.requestViewport({7.5,800,500,1500,30,20});
        QTRY_VERIFY_WITH_TIMEOUT(accepted.count()>=1&&provider.frame()!=nullptr,20000);
        QVERIFY(!provider.frame()->packIds.empty());
        QVERIFY(!provider.frame()->packet.rivers.empty()||!provider.frame()->packet.lakes.empty());
    }
};
QTEST_GUILESS_MAIN(HydroFullDatasetTests)
#include "hydro_full_dataset_tests.moc"
