#include "labelplacementmodel.h"
#include <QAbstractItemModelTester>
#include <QGuiApplication>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QtTest>

// QVariant's real equality dispatch provides a deterministic work counter. No
// clocks, instrumented production branches, or private model access are needed.
struct CountedLabelRef {
    int id=0;
    static inline qsizetype comparisons=0;
    friend bool operator==(const CountedLabelRef& a,const CountedLabelRef& b) {
        ++comparisons;return a.id==b.id;
    }
    friend bool operator!=(const CountedLabelRef& a,const CountedLabelRef& b) {return !(a==b);}
};
Q_DECLARE_METATYPE(CountedLabelRef)

namespace {
QVariantMap ref(const QString& id,const QString& domain="label") {
    return {{"domain",domain},{"id",id},{"key",domain+":"+id}};
}
QVariantMap row(const QVariant& identity,double x=10,double y=20) {
    return {{"ref",identity},{"x",x},{"y",y},{"name","Label"},{"flagSource","flag.svg"}};
}
QVariantList values(const LabelPlacementModel& model) {
    QVariantList rows;
    for(int i=0;i<model.rowCount();++i)rows.append(model.data(model.index(i),LabelPlacementModel::Content));
    return rows;
}
}

class LabelPlacementModelTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {qRegisterMetaType<CountedLabelRef>();}
    void coordinateUpdatesHaveLinearIdentityWork_data() {
        QTest::addColumn<int>("count");
        QTest::newRow("64")<<64;QTest::newRow("256")<<256;QTest::newRow("2048")<<2048;
    }
    void coordinateUpdatesHaveLinearIdentityWork() {
        QFETCH(int,count);
        LabelPlacementModel model;
        QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVariantList before,after;
        for(int i=0;i<count;++i) {
            before.append(row(QVariant::fromValue(CountedLabelRef{i})));
            after.append(row(QVariant::fromValue(CountedLabelRef{i}),12,23));
        }
        model.setRows(before);
        QSignalSpy resets(&model,&QAbstractItemModel::modelReset);
        QSignalSpy inserts(&model,&QAbstractItemModel::rowsInserted);
        QSignalSpy removes(&model,&QAbstractItemModel::rowsRemoved);
        QSignalSpy moves(&model,&QAbstractItemModel::rowsMoved);
        CountedLabelRef::comparisons=0;
        model.setRows(after);
        const auto comparisons=CountedLabelRef::comparisons;
        QVERIFY(comparisons>=count); // Verify the counter actually observes QVariant equality.
        QVERIFY2(comparisons<=count*8,qPrintable(QString("%1 ref comparisons for %2 rows; expected linear work <= %3")
                    .arg(comparisons).arg(count).arg(count*8)));
        QCOMPARE(values(model),after);
        QCOMPARE(resets.count()+inserts.count()+removes.count()+moves.count(),0);
    }
    void preciseRolesAndUnchangedRows() {
        LabelPlacementModel model;
        QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto original=row(ref("A"));model.setRows({original});
        QPersistentModelIndex retained(model.index(0));
        QSignalSpy changed(&model,&QAbstractItemModel::dataChanged);
        model.setRows({original});QCOMPARE(changed.count(),0);
        const auto update=[&](const QVariantMap& next,const QList<int>& roles) {
            changed.clear();model.setRows({next});
            QCOMPARE(changed.count(),1);
            QCOMPARE(changed.front()[0].value<QModelIndex>(),model.index(0));
            QCOMPARE(changed.front()[1].value<QModelIndex>(),model.index(0));
            QCOMPARE(changed.front()[2].value<QList<int>>(),roles);
            QCOMPARE(model.data(retained,LabelPlacementModel::Content),QVariant(next));
        };
        auto next=original;next["x"]=11;update(next,{LabelPlacementModel::LabelX});
        next["y"]=22;update(next,{LabelPlacementModel::LabelY});
        next["x"]=12;next["y"]=23;update(next,{LabelPlacementModel::LabelX,LabelPlacementModel::LabelY});
        next["name"]="Changed";next["flagSource"]="other.svg";update(next,{LabelPlacementModel::Content});
        next["x"]=13;next["pinned"]=true;update(next,{LabelPlacementModel::LabelX,LabelPlacementModel::Content});
        QVERIFY(retained.isValid());
    }
    void structuralChangesPreserveIdentityAndPersistentIndexes() {
        LabelPlacementModel model;
        QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto a=row(ref("A")),b=row(ref("B")),c=row(ref("C")),d=row(ref("D"));
        model.setRows({a,b,c});
        QPersistentModelIndex keptA(model.index(0)),removedB(model.index(1)),keptC(model.index(2));
        QSignalSpy resets(&model,&QAbstractItemModel::modelReset);
        QSignalSpy inserts(&model,&QAbstractItemModel::rowsInserted);
        QSignalSpy removes(&model,&QAbstractItemModel::rowsRemoved);
        QSignalSpy moves(&model,&QAbstractItemModel::rowsMoved);
        const QVariantList target{c,d,a};model.setRows(target);
        QCOMPARE(values(model),target);QVERIFY(!removedB.isValid());
        QCOMPARE(keptC.row(),0);QCOMPARE(keptA.row(),2);
        QCOMPARE(model.data(keptA,LabelPlacementModel::Content),QVariant(a));
        QCOMPARE(resets.count(),0);QCOMPARE(inserts.count(),1);QCOMPARE(removes.count(),1);QCOMPARE(moves.count(),1);
        QCOMPARE(removes.front()[1].toInt(),1);QCOMPARE(removes.front()[2].toInt(),1);
        QCOMPARE(moves.front()[1].toInt(),1);QCOMPARE(moves.front()[4].toInt(),0);
        QCOMPARE(inserts.front()[1].toInt(),1);QCOMPARE(inserts.front()[2].toInt(),1);
        model.setRows({});QCOMPARE(model.rowCount(),0);QVERIFY(!keptA.isValid());QVERIFY(!keptC.isValid());
    }
    void fullReferenceEqualityNotJustKeyOrId() {
        LabelPlacementModel model;
        QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto first=ref("same");auto second=first;second["domain"]="territorial";
        auto third=first;third["extra"]="metadata";
        const QVariantList initial{row(first),row(second),row(third)};model.setRows(initial);
        QPersistentModelIndex a(model.index(0)),b(model.index(1)),c(model.index(2));
        const QVariantList reordered{initial[2],initial[0],initial[1]};model.setRows(reordered);
        QCOMPARE(values(model),reordered);QCOMPARE(a.row(),1);QCOMPARE(b.row(),2);QCOMPARE(c.row(),0);
        auto fourth=first;fourth["extra"]="different";
        model.setRows({row(fourth),initial[0],initial[1]});
        QVERIFY(!c.isValid());QVERIFY(a.isValid());QVERIFY(b.isValid());
    }
    void duplicateAndInvalidReferencesKeepLegacyMatching() {
        LabelPlacementModel model;
        QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);
        const auto a=row(ref("A")),invalid=row(QVariant{});
        model.setRows({a,a,invalid});
        QPersistentModelIndex first(model.index(0)),duplicate(model.index(1)),missing(model.index(2));
        auto moved=a;moved["x"]=17;
        model.setRows({moved,a,invalid});QCOMPARE(values(model),QVariantList({moved,a,invalid}));
        // Existing matching is membership-based, not a multiset: reducing a
        // duplicate's multiplicity retains the unmatched duplicate at the end.
        model.setRows({invalid,moved});
        QCOMPARE(values(model),QVariantList({invalid,moved,a}));
        QCOMPARE(missing.row(),0);QCOMPARE(first.row(),1);QCOMPARE(duplicate.row(),2);
        model.setRows({});QCOMPARE(model.rowCount(),0);
    }
};
QTEST_MAIN(LabelPlacementModelTests)
#include "label_placement_model_tests.moc"
