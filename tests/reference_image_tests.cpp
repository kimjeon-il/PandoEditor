#include "referenceimagelibrary.h"
#include "referencewarp.h"
#include "referencetracing.h"

#include <QImage>
#include <QAbstractItemModel>
#include <QAbstractItemModelTester>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QFileInfo>
#include <QDir>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonArray>

class ReferenceImageTests : public QObject
{
    Q_OBJECT
private slots:
    void init(){QTest::failOnWarning();}
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void liveWireSessionRejectsWeakEdgesAndStaleResults()
    {
        QTemporaryDir dir;const auto path=dir.filePath("edge.png");QImage image(64,64,QImage::Format_RGB32);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)image.setPixelColor(x,y,x<32?Qt::black:Qt::white);QVERIFY(image.save(path));
        ReferenceImageLibrary library;QVERIFY(library.importImage(QUrl::fromLocalFile(path)));const auto id=library.images().back().toMap()["id"].toString();
        QVERIFY(library.beginCalibration(id));
        QVERIFY(library.setCornerQuad({QVariantList{10.,40.},QVariantList{20.,40.},QVariantList{20.,30.},QVariantList{10.,30.}}, {QVariantList{0,0},QVariantList{100,0},QVariantList{100,100},QVariantList{0,100}}));
        QVERIFY(library.beginTrace({{"session","test"}}));QVERIFY(library.traceAnchor(.5,.15));QTRY_VERIFY(!library.traceSession()["busy"].toBool());
        QVERIFY(library.traceAnchor(.5,.85));QTRY_VERIFY(!library.traceSession()["busy"].toBool());
        QVERIFY2(library.traceSession()["error"].toString().isEmpty(),qPrintable(library.traceSession()["error"].toString()));
        QVERIFY(library.traceSession()["coordinates"].toList().size()>=2);QVERIFY(library.finishTrace());
        library.redrawTrace();QVERIFY(library.traceSession()["coordinates"].toList().isEmpty());
        QVERIFY(library.traceAnchor(.5,.2));library.cancelTrace();QTest::qWait(100);QVERIFY(library.traceSession().isEmpty());
        QVERIFY(library.beginTrace({{"session","test"}}));QVERIFY(library.traceAnchor(.5,.2));QVERIFY(library.updateImage(id,{{"locked",true}}));QTest::qWait(100);QVERIFY(library.traceSession().isEmpty());
        QVERIFY(library.updateImage(id,{{"locked",false}}));
        image.fill(Qt::red);const auto weak=dir.filePath("weak.png");QVERIFY(image.save(weak));QVERIFY(library.importImage(QUrl::fromLocalFile(weak)));
        const auto weakId=library.images().back().toMap()["id"].toString();QVERIFY(library.beginCalibration(weakId));QVERIFY(library.setCornerQuad({QVariantList{10.,40.},QVariantList{20.,40.},QVariantList{20.,30.},QVariantList{10.,30.}}, {QVariantList{0,0},QVariantList{100,0},QVariantList{100,100},QVariantList{0,100}}));
        QVERIFY(library.beginTrace({{"session","weak"}}));QVERIFY(library.traceAnchor(.5,.2));QTRY_VERIFY(!library.traceSession()["busy"].toBool());
        QVERIFY(library.traceAnchor(.5,.8));QTRY_VERIFY(!library.traceSession()["busy"].toBool());QCOMPARE(library.traceSession()["error"].toString(),QString("insufficient-edge-strength"));QVERIFY(!library.finishTrace());
    }

    void fixedWebLiveWireCorpus()
    {
        QFile file(QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath("fixtures/reference-trace.json"));QVERIFY(file.open(QIODevice::ReadOnly));const auto fixtures=QJsonDocument::fromJson(file.readAll()).array();QCOMPARE(fixtures.size(),3);
        QTemporaryDir dir;ReferenceImageLibrary library;int processed=0;
        for(const auto value:fixtures){const auto row=value.toObject();const auto input=row["input"].toObject(),expected=row["expected"].toObject();const auto edge=input["edge"].toString();QImage image(64,64,QImage::Format_RGB32);
            for(int y=0;y<64;++y)for(int x=0;x<64;++x){int c=edge=="weak"?128:(x>=32&&(edge!="turn"||y<54))?255:0;image.setPixelColor(x,y,QColor(c,c,c));}
            const auto path=dir.filePath(edge+".png");QVERIFY(image.save(path));QVERIFY(library.importImage(QUrl::fromLocalFile(path)));const auto id=library.images().back().toMap()["id"].toString();QVERIFY(library.beginCalibration(id));
            QVERIFY(library.setCornerQuad({QVariantList{10.,40.},QVariantList{20.,40.},QVariantList{20.,30.},QVariantList{10.,30.}}, {QVariantList{0,0},QVariantList{100,0},QVariantList{100,100},QVariantList{0,100}}));QVERIFY(library.beginTrace({{"session",edge}}));
            for(const auto anchor:input["anchors"].toArray()){const auto uv=anchor.toArray();QVERIFY(library.traceAnchor(uv[0].toDouble(),uv[1].toDouble()));QTRY_VERIFY(!library.traceSession()["busy"].toBool());}
            if(expected["ok"].toBool()){QVERIFY2(library.finishTrace(),qPrintable(library.traceSession()["error"].toString()));const auto actual=library.traceSession()["coordinates"].toList(),wanted=expected["coordinates"].toArray().toVariantList();QCOMPARE(actual.size(),wanted.size());for(int i=0;i<actual.size();i++)for(int j=0;j<2;j++)QVERIFY(std::abs(actual[i].toList()[j].toDouble()-wanted[i].toList()[j].toDouble())<1e-10);}
            else {QCOMPARE(library.traceSession()["error"].toString(),expected["reason"].toString());QVERIFY(!library.finishTrace());}
            library.cancelTrace();++processed;
        }QCOMPARE(processed,3);
    }

    void lineRefinementUsesProductionSession()
    {
        QFile file(QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath("fixtures/reference-refine.json"));QVERIFY(file.open(QIODevice::ReadOnly));const auto fixtures=QJsonDocument::fromJson(file.readAll()).array();QCOMPARE(fixtures.size(),3);
        QTemporaryDir dir;ReferenceImageLibrary library;int processed=0;
        for(const auto value:fixtures){const auto row=value.toObject();const auto input=row["input"].toObject(),expected=row["expected"].toObject();const auto edge=input["edge"].toString();QImage image(64,64,QImage::Format_RGB32);
            for(int y=0;y<64;++y)for(int x=0;x<64;++x){int c=edge=="weak"?128:(x>=32&&(edge!="turn"||y<54))?255:0;image.setPixelColor(x,y,QColor(c,c,c));}
            const auto path=dir.filePath(edge+".png");QVERIFY(image.save(path));QVERIFY(library.importImage(QUrl::fromLocalFile(path)));const auto id=library.images().back().toMap()["id"].toString();QVERIFY(library.beginCalibration(id));
            QVERIFY(library.setCornerQuad({QVariantList{10.,40.},QVariantList{20.,40.},QVariantList{20.,30.},QVariantList{10.,30.}}, {QVariantList{0,0},QVariantList{100,0},QVariantList{100,100},QVariantList{0,100}}));QVERIFY(library.beginRefine({{"session",edge}},input["uv"].toArray().toVariantList()));QTRY_VERIFY(!library.traceSession()["busy"].toBool());
            if(expected["ok"].toBool()){QVERIFY2(library.finishTrace(),qPrintable(library.traceSession()["error"].toString()));const auto actual=library.traceSession()["coordinates"].toList(),wanted=expected["coordinates"].toArray().toVariantList();QCOMPARE(actual.size(),wanted.size());for(int i=0;i<actual.size();i++)for(int j=0;j<2;j++)QVERIFY(std::abs(actual[i].toList()[j].toDouble()-wanted[i].toList()[j].toDouble())<1e-10);}
            else {QCOMPARE(library.traceSession()["error"].toString(),expected["reason"].toString());QVERIFY(!library.finishTrace());}
            library.cancelTrace();++processed;
        }QCOMPARE(processed,3);
        const auto source=library.images().back().toMap()["source"].toUrl().toLocalFile();QFile corrupt(source);QVERIFY(corrupt.open(QIODevice::WriteOnly|QIODevice::Truncate));corrupt.write("broken image");corrupt.close();
        QVERIFY(library.beginRefine({{"session","damaged"}},{QVariantList{.5,.15},QVariantList{.5,.85}}));QTRY_VERIFY(!library.traceSession()["busy"].toBool());QCOMPARE(library.traceSession()["error"].toString(),QString("invalid-image"));QVERIFY(!library.finishTrace());
        QImage repaired(64,64,QImage::Format_RGB32);repaired.fill(Qt::gray);QVERIFY(repaired.save(source));
    }

    void geographicCalibrationUsesNormalizedUvAndMeters()
    {
        ReferenceImageLibrary library;
        QVariantMap input{{"warpMode","affine"},{"controlPoints",QVariantList{
            QVariantMap{{"id","a"},{"image",QVariantList{0.,0.}},{"coordinate",QVariantList{10.,40.}}},
            QVariantMap{{"id","b"},{"image",QVariantList{1.,0.}},{"coordinate",QVariantList{20.,40.}}},
            QVariantMap{{"id","c"},{"image",QVariantList{0.,1.}},{"coordinate",QVariantList{10.,30.}}}}}};
        QVariantMap result;
        if(library.metaObject()->indexOfMethod("calibration(QVariantMap)")<0) QFAIL("Missing production geographic calibration interface");
        const bool invoked=QMetaObject::invokeMethod(&library,"calibration",Qt::DirectConnection,
            Q_RETURN_ARG(QVariantMap,result),Q_ARG(QVariantMap,input));
        QVERIFY2(invoked,"Geographic calibration must be exposed to the actual editor");
        QVERIFY2(result["ok"].toBool(),qPrintable(result["reason"].toString()));
        const auto mesh=result["mesh"].toMap();
        const auto vertices=mesh["vertices"].toList();
        QVERIFY(!vertices.empty());
        QCOMPARE(vertices.front().toMap()["coordinate"].toList(),(QVariantList{10.,40.}));
        QVERIFY(result["diagnostics"].toMap()["rmsMeters"].toDouble()<0.01);
    }

    void calibrationSessionPersistsPointsWithoutLeakingCanceledInput()
    {
        QTemporaryDir dir;const auto path=dir.filePath("session.png");QImage image(16,16,QImage::Format_RGB32);image.fill(Qt::red);QVERIFY(image.save(path));
        ReferenceImageLibrary library;QVERIFY(library.importImage(QUrl::fromLocalFile(path)));
        const auto id=library.images().back().toMap()["id"].toString();
        if(library.metaObject()->indexOfMethod("beginCalibration(QString)")<0)QFAIL("Missing actual calibration edit session");
        bool result=false;
        QVERIFY(QMetaObject::invokeMethod(&library,"beginCalibration",Qt::DirectConnection,Q_RETURN_ARG(bool,result),Q_ARG(QString,id)));QVERIFY(result);
        QVERIFY(QMetaObject::invokeMethod(&library,"pickImagePoint",Qt::DirectConnection,Q_RETURN_ARG(bool,result),Q_ARG(double,0.25),Q_ARG(double,0.75)));QVERIFY(result);
        QVERIFY(QMetaObject::invokeMethod(&library,"pickMapCoordinate",Qt::DirectConnection,Q_RETURN_ARG(bool,result),Q_ARG(double,126.),Q_ARG(double,37.)));QVERIFY(result);
        auto points=library.images().back().toMap()["geographicPoints"].toList();QCOMPARE(points.size(),1);
        QCOMPARE(points[0].toMap()["image"].toList(),(QVariantList{0.25,0.75}));
        QCOMPARE(points[0].toMap()["coordinate"].toList(),(QVariantList{126.,37.}));
        QVERIFY(library.undo());QVERIFY(library.images().back().toMap()["geographicPoints"].toList().empty());
        QVERIFY(library.redo());ReferenceImageLibrary reopened;QCOMPARE(reopened.images().back().toMap()["geographicPoints"].toList(),points);
        QVERIFY(QMetaObject::invokeMethod(&library,"cancelCalibration"));
        QVERIFY(QMetaObject::invokeMethod(&library,"pickMapCoordinate",Qt::DirectConnection,Q_RETURN_ARG(bool,result),Q_ARG(double,127.),Q_ARG(double,38.)));QVERIFY(!result);
    }

    void fixedWebGeographicCorpus()
    {
        QFile file(QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath("fixtures/reference-calibration.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));const auto fixtures=QJsonDocument::fromJson(file.readAll()).array();QCOMPARE(fixtures.size(),7);
        ReferenceImageLibrary library;int processed=0;
        for(const auto &fixture:fixtures) {
            const auto row=fixture.toObject();const auto expected=row["expected"].toObject().toVariantMap();
            const auto actual=library.calibration(row["input"].toObject().toVariantMap());
            QCOMPARE(actual["ok"].toBool(),expected["ok"].toBool());QCOMPARE(actual["reason"].toString(),expected["reason"].toString());
            if(actual["ok"].toBool()) {
                const auto vertices=actual["mesh"].toMap()["vertices"].toList(),reference=expected["mesh"].toMap()["vertices"].toList();
                QCOMPARE(vertices.size(),reference.size());QCOMPARE(actual["mesh"].toMap()["triangles"],expected["mesh"].toMap()["triangles"]);
                for(int i=0;i<vertices.size();++i){const auto a=vertices[i].toMap()["coordinate"].toList(),b=reference[i].toMap()["coordinate"].toList();QCOMPARE(a.size(),2);for(int j=0;j<2;++j)QVERIFY(std::abs(a[j].toDouble()-b[j].toDouble())<1e-10);}
            }
            ++processed;
        }
        QCOMPARE(processed,7);
    }

    void failedUndoKeepsTheHistoryEntry()
    {
        // Unique app-data namespace: never touch the user's library or another test's records.
        const auto previous=QCoreApplication::applicationName();
        QCoreApplication::setApplicationName("reference-atomic-"+QUuid::createUuid().toString(QUuid::WithoutBraces));
        const auto root=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/reference-images";
        QTemporaryDir source;const auto imagePath=source.filePath("atomic.png");QImage image(4,4,QImage::Format_RGB32);image.fill(Qt::red);QVERIFY(image.save(imagePath));
        ReferenceImageLibrary library;QVERIFY(library.importImage(QUrl::fromLocalFile(imagePath)));
        const auto before=library.images();const auto index=root+"/library.json";const auto backup=root+"/library.backup";
        QVERIFY(QFile::rename(index,backup));QVERIFY(QDir().mkdir(index));
        const bool result=library.undo();const bool historyPreserved=library.canUndo();const auto after=library.images();
        QVERIFY(QDir().rmdir(index));QVERIFY(QFile::rename(backup,index));
        QCoreApplication::setApplicationName(previous);
        QVERIFY(!result);QCOMPARE(after,before);QVERIFY2(historyPreserved,"Failed save consumed the Undo history entry");
    }

    void cornerPinAnchorAndCanceledGesturePreserveGeographicState()
    {
        QTemporaryDir dir;const auto path=dir.filePath("corners.png");QImage image(20,20,QImage::Format_RGB32);image.fill(Qt::red);QVERIFY(image.save(path));
        ReferenceImageLibrary library;QVERIFY(library.importImage(QUrl::fromLocalFile(path)));const auto id=library.images().back().toMap()["id"].toString();QVERIFY(library.beginCalibration(id));
        if(library.metaObject()->indexOfMethod("setCornerQuad(QVariantList,QVariantList)")<0)QFAIL("Missing corner-pin production edit interface");
        const QVariantList quad{QVariantList{10.,40.},QVariantList{20.,40.},QVariantList{20.,30.},QVariantList{10.,30.}};
        const QVariantList screen{QVariantList{0.,0.},QVariantList{100.,0.},QVariantList{100.,100.},QVariantList{0.,100.}};
        bool result=false;
        QVERIFY(QMetaObject::invokeMethod(&library,"setCornerQuad",Qt::DirectConnection,Q_RETURN_ARG(bool,result),Q_ARG(QVariantList,quad),Q_ARG(QVariantList,screen)));QVERIFY(result);
        QCOMPARE(library.images().back().toMap()["mapQuad"].toList(),quad);QVERIFY(library.images().back().toMap()["cornerPinEnabled"].toBool());
        QVERIFY(library.calibrationSession()["result"].toMap()["ok"].toBool());
        QCOMPARE(library.calibrationSession()["result"].toMap()["mode"].toString(),QString("projective"));
        QVERIFY(QMetaObject::invokeMethod(&library,"beginAnchor",Qt::DirectConnection,Q_RETURN_ARG(bool,result)));QVERIFY(result);
        QVERIFY(library.pickImagePoint(.5,.5));QVERIFY(library.pickMapCoordinate(15.,35.));
        const auto record=library.images().back().toMap();QCOMPARE(record["anchor"].toMap()["coordinate"].toList(),(QVariantList{15.,35.}));
        QVERIFY(library.calibrationSession()["result"].toMap()["diagnostics"].toMap()["hardMaxMeters"].toDouble()<=.01);
        QVERIFY(library.beginGesture(id));auto changed=quad;changed[0]=QVariantList{11.,39.};
        QVERIFY(library.updateGesture({{"mapQuad",changed},{"screenQuad",screen}}));library.cancelGesture();QCOMPARE(library.images().back().toMap(),record);
        const QVariantList invalidScreen{QVariantList{0.,0.},QVariantList{100.,100.},QVariantList{100.,0.},QVariantList{0.,100.}};
        QVERIFY(QMetaObject::invokeMethod(&library,"setCornerQuad",Qt::DirectConnection,Q_RETURN_ARG(bool,result),Q_ARG(QVariantList,quad),Q_ARG(QVariantList,invalidScreen)));QVERIFY(!result);QCOMPARE(library.images().back().toMap(),record);
        ReferenceImageLibrary reopened;QCOMPARE(reopened.images().back().toMap()["anchor"],record["anchor"]);QCOMPARE(reopened.images().back().toMap()["mapQuad"],record["mapQuad"]);
    }

    void normalAnchorTranslatesPlacementWithoutEnablingCornerPin()
    {
        QTemporaryDir dir;const auto path=dir.filePath("placement-anchor.png");QImage image(20,20,QImage::Format_RGB32);image.fill(Qt::red);QVERIFY(image.save(path));
        ReferenceImageLibrary library;QVERIFY(library.importImage(QUrl::fromLocalFile(path)));const auto id=library.images().back().toMap()["id"].toString();QVERIFY(library.beginCalibration(id));
        if(library.metaObject()->indexOfMethod("setPlacementQuad(QVariantList,QVariantList)")<0)QFAIL("Missing initial placement mapping for a normal anchor");
        const QVariantList quad{QVariantList{10.,40.},QVariantList{20.,40.},QVariantList{20.,30.},QVariantList{10.,30.}};
        const QVariantList screen{QVariantList{0.,0.},QVariantList{100.,0.},QVariantList{100.,100.},QVariantList{0.,100.}};
        bool result=false;QVERIFY(QMetaObject::invokeMethod(&library,"setPlacementQuad",Qt::DirectConnection,Q_RETURN_ARG(bool,result),Q_ARG(QVariantList,quad),Q_ARG(QVariantList,screen)));QVERIFY(result);
        QVERIFY(!library.images().back().toMap()["cornerPinEnabled"].toBool());QVERIFY(library.beginAnchor());QVERIFY(library.pickImagePoint(.5,.5));QVERIFY(library.pickMapCoordinate(16.,36.));
        const auto aligned=library.images().back().toMap()["mapQuad"].toList();QCOMPARE(aligned.size(),4);
        for(int i=0;i<4;++i){const auto pair=aligned[i].toList(),previous=quad[i].toList();for(int j=0;j<2;++j)QVERIFY(std::abs(pair[j].toDouble()-previous[j].toDouble()-1.)<1e-8);}
        QVERIFY(!library.images().back().toMap()["cornerPinEnabled"].toBool());
    }

    void solvesSimilarityAffineProjectiveAndTps()
    {
        const QVector<ReferenceControlPoint> similarity{{{0,0},{5,7}},{{10,0},{25,7}}};
        auto result=solveReferenceWarp(ReferenceWarpMode::Similarity,similarity);
        QVERIFY(result.valid);QCOMPARE(result.map({4,3}),QPointF(13,13));QVERIFY(result.rmsError<1e-8);

        const QVector<ReferenceControlPoint> affine{{{0,0},{2,3}},{{10,0},{22,3}},{{0,10},{2,33}}};
        result=solveReferenceWarp(ReferenceWarpMode::Affine,affine);
        QVERIFY(result.valid);QVERIFY(QLineF(result.map({4,5}),QPointF(10,18)).length()<1e-8);

        const QVector<ReferenceControlPoint> projective{{{0,0},{0,0}},{{10,0},{12,1}},{{10,10},{11,12}},{{0,10},{1,10}}};
        result=solveReferenceWarp(ReferenceWarpMode::Projective,projective);
        QVERIFY(result.valid);QVERIFY(result.maximumError<1e-7);

        result=solveReferenceWarp(ReferenceWarpMode::ThinPlateSpline,affine);
        QVERIFY(result.valid);QVERIFY(result.maximumError<1e-7);
    }

    void rejectsInsufficientAndDuplicateControlPoints()
    {
        QVERIFY(!solveReferenceWarp(ReferenceWarpMode::Affine,{{{0,0},{0,0}},{{1,0},{1,0}}}).valid);
        QVERIFY(!solveReferenceWarp(ReferenceWarpMode::Similarity,{{{0,0},{0,0}},{{0,0},{1,1}}}).valid);
    }

    void persistsSeparatelyAndKeepsFiftyStepHistory()
    {
        QTemporaryDir source;QVERIFY(source.isValid());const QString path=source.path()+"/reference.png";
        QImage image(8,6,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::red);QVERIFY(image.save(path));
        ReferenceImageLibrary library;for(const auto &item:library.images())library.removeImage(item.toMap()["id"].toString());
        QVERIFY(library.importImage(QUrl::fromLocalFile(path),"reference"));QCOMPARE(library.images().size(),1);
        const auto id=library.images().front().toMap()["id"].toString();
        for(int index=0;index<55;++index)QVERIFY(library.updateImage(id,{{"opacity",double(index%10)/10.}}));
        int undos=0;while(library.undo())++undos;QCOMPARE(undos,50);QVERIFY(library.canRedo());
        QVERIFY(library.redo());ReferenceImageLibrary reopened;QCOMPARE(reopened.images().size(),1);
        QCOMPARE(reopened.images().front().toMap()["name"].toString(),QStringLiteral("reference"));
    }

    void lockedRecordsRejectTransformsButCanUnlock()
    {
        QTemporaryDir source;const QString path=source.path()+"/locked.png";QImage image(2,2,QImage::Format_ARGB32);image.fill(Qt::blue);QVERIFY(image.save(path));
        ReferenceImageLibrary library;QVERIFY(library.importImage(QUrl::fromLocalFile(path),"locked"));const auto id=library.images().back().toMap()["id"].toString();
        QVERIFY(library.updateImage(id,{{"locked",true}}));QVERIFY(!library.updateImage(id,{{"x",10}}));QVERIFY(library.updateImage(id,{{"locked",false}}));QVERIFY(library.updateImage(id,{{"blend","difference"},{"x",10}}));
        QVERIFY(library.beginGesture(id));QVERIFY(library.updateGesture({{"x",25},{"rotation",15}}));library.cancelGesture();QCOMPARE(library.images().back().toMap()["x"].toDouble(),10.);
        QVERIFY(library.beginGesture(id));QVERIFY(library.updateGesture({{"x",25},{"rotation",15}}));QVERIFY(library.commitGesture());QCOMPARE(library.images().back().toMap()["x"].toDouble(),25.);QVERIFY(library.undo());QCOMPARE(library.images().back().toMap()["x"].toDouble(),10.);
    }

    void stableModelPreservesImageIdsThroughDataHistoryAndOrdering()
    {
        QTemporaryDir source;const QString path=source.path()+"/stable.png";
        QImage image(8,6,QImage::Format_ARGB32);image.fill(Qt::green);QVERIFY(image.save(path));
        ReferenceImageLibrary library;
        for(const auto &item:library.images()) {
            const auto id=item.toMap()["id"].toString();
            QVERIFY(library.updateImage(id,{{"locked",false}}));QVERIFY(library.removeImage(id));
        }
        auto* model=qvariant_cast<QAbstractItemModel*>(library.property("imageModel"));
        QVERIFY2(model,"ReferenceImageLibrary must expose an identity-preserving imageModel");
        QAbstractItemModelTester tester(model,QAbstractItemModelTester::FailureReportingMode::QtTest);
        const int role=model->roleNames().key("modelData",-1);QVERIFY(role>=Qt::UserRole);
        QSignalSpy reset(model,&QAbstractItemModel::modelReset);
        QSignalSpy inserted(model,&QAbstractItemModel::rowsInserted);
        QSignalSpy removed(model,&QAbstractItemModel::rowsRemoved);
        QSignalSpy moved(model,&QAbstractItemModel::rowsMoved);
        QSignalSpy changed(model,&QAbstractItemModel::dataChanged);
        const auto rows=[&] {QVariantList result;for(int row=0;row<model->rowCount();++row)result.append(model->data(model->index(row,0),role));return result;};
        for(const auto& name:{"first","second","third"})QVERIFY(library.importImage(QUrl::fromLocalFile(path),name));
        QCOMPARE(inserted.count(),3);QCOMPARE(rows(),library.images());
        const auto id=library.images()[1].toMap()["id"].toString();
        QPersistentModelIndex first=model->index(0,0),second=model->index(1,0),third=model->index(2,0);
        const auto original=library.images();
        QVERIFY(library.beginGesture(id));
        for(int i=1;i<=3;++i) {
            QVERIFY(library.updateGesture({{"x",i*2.5},{"y",i*1.5},{"width",8.+i},{"rotation",i*5.}}));
            QVERIFY(second.isValid());QCOMPARE(second.data(role).toMap()["id"].toString(),id);
            QCOMPARE(rows(),library.images());QCOMPARE(reset.count(),0);QCOMPARE(inserted.count(),3);QCOMPARE(removed.count(),0);QCOMPARE(moved.count(),0);
        }
        QCOMPARE(changed.count(),3);const auto preview=library.images();QVERIFY(library.commitGesture());
        QCOMPARE(rows(),preview);QVERIFY(library.undo());QCOMPARE(rows(),original);
        QVERIFY(library.redo());QCOMPARE(rows(),preview);
        QVERIFY(library.beginGesture(id));QVERIFY(library.updateGesture({{"x",93.}}));library.cancelGesture();QCOMPARE(rows(),preview);
        QVERIFY(library.reload());QCOMPARE(rows(),preview);QVERIFY(second.isValid());
        QVERIFY(library.moveImage(id,0));QCOMPARE(second.row(),0);QCOMPARE(first.row(),1);QCOMPARE(third.row(),2);
        QCOMPARE(rows(),library.images());QVERIFY(moved.count()>0);
        QVERIFY(library.moveImage(id,2));QCOMPARE(second.row(),2);QCOMPARE(first.row(),0);QCOMPARE(third.row(),1);
        QCOMPARE(rows(),library.images());
        QVERIFY(library.removeImage(id));QVERIFY(!second.isValid());QVERIFY(first.isValid());QVERIFY(third.isValid());
        QCOMPARE(removed.count(),1);QCOMPARE(rows(),library.images());
        QVERIFY(library.undo());QCOMPARE(model->rowCount(),3);QCOMPARE(rows(),library.images());
        QCOMPARE(first.row(),0);QCOMPARE(third.row(),1);QCOMPARE(reset.count(),0);
        QVERIFY(!model->data(QModelIndex(),role).isValid());QCOMPARE(model->rowCount(model->index(0,0)),0);
    }

    void liveWireAndGradientRefinerFollowImageEdge()
    {
        QImage image(32,24,QImage::Format_RGB32);image.fill(Qt::white);
        for(int y=0;y<image.height();++y)for(int x=0;x<16;++x)image.setPixel(x,y,qRgb(0,0,0));
        const auto path=referenceLiveWire(image,{15,2},{15,21});QVERIFY(path.size()>=20);
        for(const auto& point:path)QVERIFY(std::abs(point.x()-15)<=1);
        const QVector<QPointF> draft{{12,3},{12,10},{12,20}};const auto refined=refineReferenceLine(image,draft,5);QCOMPARE(refined.size(),draft.size());
        for(const auto& point:refined)QVERIFY(point.x()>=14&&point.x()<=16);
    }
};
QTEST_MAIN(ReferenceImageTests)
#include "reference_image_tests.moc"
