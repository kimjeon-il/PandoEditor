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

class ReferenceImageTests : public QObject
{
    Q_OBJECT
private slots:
    void init(){QTest::failOnWarning();}
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

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
