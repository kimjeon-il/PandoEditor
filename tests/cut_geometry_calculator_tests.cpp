#include "cutgeometrycalculator.h"
#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <future>
using namespace pandoeditor;
namespace {
QJsonArray corpus() {
    QFile file(QStringLiteral(M973_CUT_FIXTURE));
    if(!file.open(QIODevice::ReadOnly))return {};
    return QJsonDocument::fromJson(file.readAll()).array();
}
QJsonObject payload() {return corpus()[0].toObject()["payload"].toObject();}
}
class CutGeometryCalculatorTests : public QObject {
    Q_OBJECT
private slots:
    void orderedFullWorkerResultMatchesRecordedNodeCases_data() {
        QTest::addColumn<QJsonObject>("input");QTest::addColumn<QJsonObject>("expected");
        const auto rows=corpus();QCOMPARE(rows.size(),6);
        for(const auto& value:rows){const auto row=value.toObject();QTest::newRow(qPrintable(row["id"].toString()))<<row["payload"].toObject()<<row["result"].toObject();}
    }
    void orderedFullWorkerResultMatchesRecordedNodeCases() {
        QFETCH(QJsonObject,input);QFETCH(QJsonObject,expected);const auto before=input;
        const auto output=prepareCutGeometry(input);
        QVERIFY2(output.succeeded(),qPrintable(output.detail));
        // Existing corpus is a Node regression oracle, not browser evidence.
        // Compare every field and ordered coordinate without normalization.
        QCOMPARE(output.result,expected);
        QCOMPARE(QJsonDocument::fromJson(output.json).object(),output.result);
        QVERIFY(output.inputUnchanged);QCOMPARE(input,before);
    }
    void assessmentWithoutPreviewRetainsSnapsButNoCandidates() {
        auto input=payload();input["buildPreview"]=false;
        auto expected=corpus()[0].toObject()["result"].toObject();expected.remove("split");
        const auto result=prepareCutGeometry(input);QVERIFY2(result.succeeded(),qPrintable(result.detail));
        QCOMPARE(result.result,expected);QVERIFY(result.inputUnchanged);
    }
    void pendingLinePreservesExactResult() {
        auto input=payload();input["coords"]=QJsonArray{QJsonArray{1,2}};
        const auto result=prepareCutGeometry(input);QVERIFY2(result.succeeded(),qPrintable(result.detail));
        QCOMPARE(result.result,QJsonObject({{"line",input["coords"]},{"snaps",QJsonObject{{"start",QJsonValue::Null},{"end",QJsonValue::Null}}},
            {"status","pending"},{"valid",false},{"message",""},{"issues",QJsonArray{}}}));
    }
    void cancellationDiscardsPartialValuesAtEveryBoundary() {
        int checks=0;const auto completed=prepareCutGeometry(payload(),[&]{++checks;return false;});QVERIFY(completed.succeeded());QVERIFY(checks>3);
        for(int stop=1;stop<=checks;++stop){int current=0;const auto result=prepareCutGeometry(payload(),[&]{return ++current>=stop;});
            QCOMPARE(result.status,CutGeometryStatus::Cancelled);QVERIFY(result.result.isEmpty());QVERIFY(result.json.isEmpty());QVERIFY(result.detail.isEmpty());QVERIFY(!result.inputUnchanged);}
    }
    void malformedRequestFailsAndCancellationWins() {
        const auto result=prepareCutGeometry({});QCOMPARE(result.status,CutGeometryStatus::Failed);
        QVERIFY(!result.detail.isEmpty());QVERIFY(result.result.isEmpty());QVERIFY(result.json.isEmpty());
        QCOMPARE(prepareCutGeometry({},[]{return true;}).status,CutGeometryStatus::Cancelled);
    }
    void freshWorkerEnginesProduceOwnedResults() {
        const auto input=payload();
        auto a=std::async(std::launch::async,[input]{return prepareCutGeometry(input);});
        auto b=std::async(std::launch::async,[input]{return prepareCutGeometry(input);});
        const auto first=a.get(),second=b.get();QVERIFY2(first.succeeded(),qPrintable(first.detail));QVERIFY2(second.succeeded(),qPrintable(second.detail));
        QCOMPARE(first.result,second.result);QVERIFY(first.inputUnchanged);QVERIFY(second.inputUnchanged);
    }
};
QTEST_GUILESS_MAIN(CutGeometryCalculatorTests)
#include "cut_geometry_calculator_tests.moc"
