#include "physicaldatastore.h"
#include "terrainprovider.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>

namespace {
QString hash(const QByteArray& bytes) {return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
class HttpFixture final : public QTcpServer {
public:
    QHash<QString,QList<QByteArray>> replies;
    QHash<QString,int> requests;
    HttpFixture() {
        connect(this,&QTcpServer::newConnection,this,[this] {
            while(auto* socket=nextPendingConnection())connect(socket,&QTcpSocket::readyRead,socket,[this,socket] {
                const auto request=socket->readAll();const auto first=request.left(request.indexOf('\n')).trimmed().split(' ');
                const auto path=first.size()>1?QString::fromLatin1(first[1]):QString();++requests[path];
                auto& queue=replies[path];const auto body=queue.isEmpty()?QByteArray("missing"):
                    (queue.size()>1?queue.takeFirst():queue.front());
                const auto status=queue.isEmpty()&&body=="missing"?"404 Not Found":"200 OK";
                socket->write("HTTP/1.1 "+QByteArray(status)+"\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
                socket->disconnectFromHost();
            });
        });
        listen(QHostAddress::LocalHost);
    }
    QUrl url(const QString& path) const {return QUrl(QString("http://127.0.0.1:%1%2").arg(serverPort()).arg(path));}
};
}

class PhysicalDataStoreTests final : public QObject {
    Q_OBJECT
private slots:
    void inventoryRejectsEscapesAndParsesIdentity();
    void verifiedDownloadRetryPartPromotionOfflineReuseAndQuarantine();
    void checksumFailureKeepsNoPromotedFileAndCleanupKeepsCurrentVersion();
    void pinnedInventoryAndViewportSelectionAreLazy();
};

void PhysicalDataStoreTests::inventoryRejectsEscapesAndParsesIdentity() {
        const auto good=parsePhysicalInventory(R"({"schema":"pandoeditor-physical-inventory","version":1,"dataset":"terrain","datasetVersion":"v1","baseUrl":"https://example.invalid/data/","assets":[{"path":"0/0-0.webp","bytes":3,"sha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"}]})");
        QVERIFY2(good.valid(),qPrintable(good.error));QCOMPARE(good.assets.size(),1);QCOMPARE(good.assets.front().path,QString("0/0-0.webp"));
        for(const auto& path:{QString("../escape"),QString("/absolute"),QString("a\\b")}) {
            auto json=QByteArray(R"({"schema":"pandoeditor-physical-inventory","version":1,"dataset":"terrain","datasetVersion":"v1","baseUrl":"https://example.invalid/","assets":[{"path":"PATH","bytes":3,"sha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"}]})");
            json.replace("PATH",path.toUtf8());QVERIFY(!parsePhysicalInventory(json).valid());
        }
}
void PhysicalDataStoreTests::verifiedDownloadRetryPartPromotionOfflineReuseAndQuarantine() {
        QTemporaryDir dir;QVERIFY(dir.isValid());HttpFixture http;
        const QByteArray good="verified physical bytes",bad="wrong";
        http.replies["/asset.bin"]={bad,good};
        PhysicalAssetSpec asset{"hydro","v1","shards/s0.bin",good.size(),hash(good),http.url("/asset.bin")};
        PhysicalDataStore store(dir.path(),2,2);QSignalSpy ready(&store,&PhysicalDataStore::assetReady);QSignalSpy failed(&store,&PhysicalDataStore::assetFailed);
        const auto target=store.cachePath(asset);QDir().mkpath(QFileInfo(target).absolutePath());
        QFile stale(target+".part");QVERIFY(stale.open(QIODevice::WriteOnly));stale.write("stale");stale.close();
        store.request(asset);QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,5000);QCOMPARE(failed.count(),0);
        QCOMPARE(http.requests["/asset.bin"],2);QVERIFY(QFile::exists(target));QVERIFY(!QFile::exists(target+".part"));
        QFile installed(target);QVERIFY(installed.open(QIODevice::ReadOnly));QCOMPARE(installed.readAll(),good);installed.close();

        http.close();store.request(asset);QTRY_COMPARE_WITH_TIMEOUT(ready.count(),2,1000);
        QCOMPARE(ready.at(1).at(2).toBool(),true);

        QVERIFY(installed.open(QIODevice::WriteOnly|QIODevice::Truncate));installed.write("corrupt");installed.close();
        HttpFixture replacement;replacement.replies["/asset.bin"]={good};asset.url=replacement.url("/asset.bin");
        store.request(asset);QTRY_COMPARE_WITH_TIMEOUT(ready.count(),3,5000);
        QVERIFY(QDir(QFileInfo(target).absolutePath()).entryList({QFileInfo(target).fileName()+".corrupt*"},QDir::Files).size()==1);
}
void PhysicalDataStoreTests::checksumFailureKeepsNoPromotedFileAndCleanupKeepsCurrentVersion() {
        QTemporaryDir dir;QVERIFY(dir.isValid());HttpFixture http;http.replies["/bad.bin"]={QByteArray("bad")};
        PhysicalAssetSpec asset{"terrain","v2","0/0-0.webp",4,hash("good"),http.url("/bad.bin")};
        PhysicalDataStore store(dir.path(),1,0);QSignalSpy failed(&store,&PhysicalDataStore::assetFailed);
        store.request(asset);QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,3000);
        QVERIFY(!QFile::exists(store.cachePath(asset)));QVERIFY(!QFile::exists(store.cachePath(asset)+".part"));
        QDir().mkpath(QDir(dir.path()).filePath("terrain/v1"));QDir().mkpath(QDir(dir.path()).filePath("terrain/v2"));
        QVERIFY(store.cleanupVersions("terrain","v2"));
        QVERIFY(!QDir(QDir(dir.path()).filePath("terrain/v1")).exists());
        QVERIFY(QDir(QDir(dir.path()).filePath("terrain/v2")).exists());
}
void PhysicalDataStoreTests::pinnedInventoryAndViewportSelectionAreLazy() {
    QFile inventoryFile(QStringLiteral(PHYSICAL_INVENTORY));QVERIFY(inventoryFile.open(QIODevice::ReadOnly));
    const auto inventory=parsePhysicalInventory(inventoryFile.readAll());QVERIFY2(inventory.valid(),qPrintable(inventory.error));
    QCOMPARE(inventory.dataset,QStringLiteral("world"));QCOMPARE(inventory.version,QStringLiteral("c0bd31d1"));
    QCOMPARE(inventory.assets.size(),342);
    QVERIFY(std::any_of(inventory.assets.cbegin(),inventory.assets.cend(),[](const auto& asset){return asset.path=="terrain/v0.12.6/4/21-10.webp";}));
    QVERIFY(std::any_of(inventory.assets.cbegin(),inventory.assets.cend(),[](const auto& asset){return asset.path=="hydro/v0.13.0/shards/s2.bin";}));

    const QByteArray manifest=R"({"version":"0.12.6","crs":"EPSG:4326","tileFormat":"lossless WebP RGBA","gutter":1,"levels":[{"id":0,"width":1350,"height":675,"columns":2,"rows":1,"tileSize":1024},{"id":1,"width":2700,"height":1350,"columns":3,"rows":2,"tileSize":1024},{"id":2,"width":5400,"height":2700,"columns":6,"rows":3,"tileSize":1024},{"id":3,"width":10800,"height":5400,"columns":11,"rows":6,"tileSize":1024},{"id":4,"width":21600,"height":10800,"columns":22,"rows":11,"tileSize":1024}]})";
    QStringList resolved;TerrainTileProvider provider(manifest,QString(),[&](const QString& path){resolved.push_back(path);return path;});
    QVERIFY2(provider.available(),qPrintable(provider.error()));
    MapViewState view;view.viewportWidth=800;view.viewportHeight=600;view.scale=150;view.translateX=400;view.translateY=300;
    const auto tiles=provider.tilesForView(view);QVERIFY(!tiles.empty());QVERIFY(resolved.size()<334);
    for(const auto& path:resolved)QVERIFY(path.startsWith("terrain/v0.12.6/"));
}

QTEST_GUILESS_MAIN(PhysicalDataStoreTests)
#include "physical_data_store_tests.moc"
