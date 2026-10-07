#include "editorcontroller.h"
#include "projectcodec.h"
#include "territorial_fixture.h"
#include <pandoeditor/project.h>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

// Independent literals/actions: Web a1555722813fbb7f2ae397f1999e21dd9fa0ed66,
// territorial-info-period.test.mjs and browser/info-tab-v9.spec.mjs. The
// subsequent fixed ebcfae4d source retains the static TIMELINE_ACTIVATION guard.
namespace {
QVariantMap ref(const QString& id) {return {{"domain","territorial"},{"id",id}};}
pandoeditor::ProjectDocument fixture() {
    using namespace pandoeditor;
    ProjectDocument document({{"A","Alpha",{{{{0,0},{12,0},{12,12},{0,12},{0,0}}}},0x336699}},{{"countries","Countries"}});
    const auto add=[&](const std::string& id,const std::string& name,UnitKind kind,double x,double y,double size,const std::string& parent) {
        Geometry shape{"Polygon",{},{},{{{{x,y},{x+size,y},{x+size,y+size},{x,y+size},{x,y}}}}};
        GeometryRef geometry{"info-shape:"+id,1};document.geometries.insert(geometry,shape);
        appendTerritory(document,{id,name,"",kind,false},geometry,parent);
        document.presentation.membership[territorialRef(id)]="countries";
        document.presentation.objectStyles[territorialRef(id)]={0,1,false};
    };
    add("B","Beta",UnitKind::General,1,1,10,"A");
    add("C","Child",UnitKind::General,2,2,3,"B");
    add("D","A very long child name that must preserve its exact source spelling",UnitKind::General,7,7,3,"B");
    add("R","Independent region",UnitKind::Regional,20,20,3,"");
    document.symbols[territorialRef("C")]={FlagPolicy::Embedded,"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciLz4="};
    return document;
}
bool openFixture(EditorController& editor,const QString& directory) {
    pandoeditor::Project project;project.replace(fixture());const auto bytes=projectcodec::encode(project);
    QFile file(directory+"/info.pando.json");if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())return false;
    file.close();return editor.openFile(QUrl::fromLocalFile(file.fileName()));
}
QVariantMap parse(EditorController& editor,const QString& input) {
    // Meta-object invocation keeps RED compilable against the actual old
    // controller without changing production or defining a test-only parser.
    QVariantMap result;
    QMetaObject::invokeMethod(&editor,"parseTerritorialPeriodInput",Qt::DirectConnection,Q_RETURN_ARG(QVariantMap,result),Q_ARG(QString,input));
    return result;
}
bool commit(EditorController& editor,const QString& token,const QString& input) {
    bool accepted=false;
    QMetaObject::invokeMethod(&editor,"commitTerritorialPeriod",Qt::DirectConnection,Q_RETURN_ARG(bool,accepted),Q_ARG(QString,token),Q_ARG(QString,input));
    return accepted;
}
QStringList rowIds(const QVariant& rows) {
    QStringList ids;for(const auto& value:rows.toList())ids.push_back(value.toMap().value("id").toString());return ids;
}
}
class TerritorialInfoControllerTests : public QObject {
    Q_OBJECT
private slots:
    void periodPrecisionAndOpenBounds_data() {
        QTest::addColumn<QString>("input");QTest::addColumn<QString>("from");QTest::addColumn<QString>("to");
        QTest::newRow("day")<<QString("1871-01-18 ~ 1918-11-09")<<QString("1871-01-18")<<QString("1918-11-09");
        QTest::newRow("open-end")<<QString(" 1871-01-18 ~ ")<<QString("1871-01-18")<<QString();
        QTest::newRow("open-start")<<QString("~ 1918-11-09")<<QString()<<QString("1918-11-09");
        QTest::newRow("empty")<<QString()<<QString()<<QString();
        QTest::newRow("both-open")<<QString("~")<<QString()<<QString();
        QTest::newRow("bce-leap")<<QString("-0004-02-29 ~ 0001")<<QString("-0004-02-29")<<QString("0001");
        QTest::newRow("coarse")<<QString("1900 ~ 1900-02")<<QString("1900")<<QString("1900-02");
        QTest::newRow("extended")<<QString("+012345 ~ +012346-03")<<QString("+012345")<<QString("+012346-03");
    }
    void periodPrecisionAndOpenBounds() {
        QFETCH(QString,input);QFETCH(QString,from);QFETCH(QString,to);EditorController editor;
        const auto result=parse(editor,input);QVERIFY2(result.value("ok").toBool(),"The production whole-period parser must preserve both endpoint values atomically");
        if(from.isEmpty())QVERIFY(result.value("validFrom").isNull());else QCOMPARE(result.value("validFrom").toString(),from);
        if(to.isEmpty())QVERIFY(result.value("validTo").isNull());else QCOMPARE(result.value("validTo").toString(),to);
        const auto formatted=from.isEmpty()&&to.isEmpty()?QString():(from+" ~ "+to).trimmed();
        QCOMPARE(result.value("formatted").toString(),formatted);
        QCOMPARE(parse(editor,formatted),result);
    }
    void invalidPeriodNeverChangesDocument_data() {
        QTest::addColumn<QString>("input");
        for(const auto& value:{"1900","1900 ~~ 1901","1900 ~ 1901 ~","0000 ~","1900-02-29 ~","~ 2024-13-01","1918-11-09 ~ 1871-01-18"})QTest::newRow(value)<<QString(value);
    }
    void invalidPeriodNeverChangesDocument() {
        QFETCH(QString,input);EditorController editor;editor.selectCountry("DEU");
        const auto before=editor.documentBytes();const auto revision=editor.revision();const auto dirty=editor.dirty();
        const auto result=parse(editor,input);QVERIFY(!result.value("ok").toBool());QVERIFY(!result.value("error").toString().isEmpty());
        const auto token=editor.beginPropertyEdit("validity");QVERIFY(!token.isEmpty());QVERIFY(!commit(editor,token,input));
        QCOMPARE(editor.documentBytes(),before);QCOMPARE(editor.revision(),revision);QCOMPARE(editor.dirty(),dirty);
    }
    void openPeriodIsAtomicNoOpAndKeepsRedo_data() {
        QTest::addColumn<QString>("input");QTest::newRow("empty")<<QString();QTest::newRow("separator")<<QString("~");QTest::newRow("spaces")<<QString(" ~ ");
    }
    void openPeriodIsAtomicNoOpAndKeepsRedo() {
        QFETCH(QString,input);EditorController editor;editor.selectCountry("DEU");
        editor.setMemoDraft("History marker");QVERIFY(editor.commitObjectField("notes"));editor.undo();QVERIFY(editor.canRedo());
        const auto before=editor.documentBytes();const auto revision=editor.revision();const auto dirty=editor.dirty();
        const auto token=editor.beginPropertyEdit("validity");QVERIFY(!token.isEmpty());QVERIFY(commit(editor,token,input));
        QCOMPARE(editor.objectProperties().value("periodInput").toString(),QString());
        QCOMPARE(editor.documentBytes(),before);QCOMPARE(editor.revision(),revision);QCOMPARE(editor.dirty(),dirty);QVERIFY(editor.canRedo());
    }
    void datedPeriodsKeepStaticActivationBoundary_data() {
        QTest::addColumn<QString>("input");
        for(const auto& value:{"1871-01-18 ~ 1918-11-09","1871-01-18 ~","~ 1918-11-09","-0004-02-29 ~ 0001","1900 ~ 1900-02"})QTest::newRow(value)<<QString(value);
    }
    void datedPeriodsKeepStaticActivationBoundary() {
        QFETCH(QString,input);EditorController editor;editor.selectCountry("DEU");QSignalSpy errors(&editor,&EditorController::errorOccurred);
        const auto before=editor.documentBytes();const auto revision=editor.revision();const auto dirty=editor.dirty();
        const auto token=editor.beginPropertyEdit("validity");QVERIFY(!token.isEmpty());QVERIFY(!commit(editor,token,input));
        QVERIFY(!errors.empty());QVERIFY(errors.last().first().toString().contains("TIMELINE_ACTIVATION"));
        QCOMPARE(editor.documentBytes(),before);QCOMPARE(editor.revision(),revision);QCOMPARE(editor.dirty(),dirty);QVERIFY(!editor.canUndo());
    }
    void periodSessionCannotFollowSelectionOrProjectReplacement() {
        QTemporaryDir directory;QVERIFY(directory.isValid());EditorController editor;QVERIFY(openFixture(editor,directory.path()));
        QVERIFY(editor.selectObject(ref("B")));const auto token=editor.beginPropertyEdit("validity");QVERIFY(!token.isEmpty());
        QVERIFY(editor.selectObject(ref("C")));const auto before=editor.documentBytes();QVERIFY(!commit(editor,token,"~"));QCOMPARE(editor.documentBytes(),before);
        const auto next=editor.beginPropertyEdit("validity");QVERIFY(!next.isEmpty());QVERIFY(openFixture(editor,directory.path()));
        const auto replaced=editor.documentBytes();QVERIFY(!commit(editor,next,"~"));QCOMPARE(editor.documentBytes(),replaced);
    }
    void lockedPeriodCannotCommit() {
        EditorController editor;editor.selectCountry("DEU");QVERIFY(editor.toggleObjectLock());const auto before=editor.documentBytes();
        const auto token=editor.beginPropertyEdit("validity");QVERIFY(token.isEmpty()||!commit(editor,token,"~"));QCOMPARE(editor.documentBytes(),before);
        QVERIFY(editor.objectProperties().contains("periodInput"));QVERIFY(!editor.objectProperties().value("editable").toBool());
    }
    void relationsUseOnlyLiveCanonicalParentAndChildren() {
        QTemporaryDir directory;QVERIFY(directory.isValid());EditorController editor;QVERIFY(openFixture(editor,directory.path()));
        QVERIFY(editor.selectObject(ref("B")));const auto rows=editor.objectProperties();
        QCOMPARE(rowIds(rows.value("parentRows")),QStringList{"A"});QCOMPARE(rowIds(rows.value("childRows")),QStringList({"C","D"}));
        const auto first=rows.value("childRows").toList().first().toMap();QCOMPARE(first.value("name").toString(),QString("Child"));QVERIFY(first.value("flagSource").toString().startsWith("data:image/svg+xml"));
        QVERIFY(editor.selectObject(ref("A")));QVERIFY(editor.objectProperties().value("parentRows").toList().isEmpty());QCOMPARE(rowIds(editor.objectProperties().value("childRows")),QStringList{"B"});
        QVERIFY(editor.selectObject(ref("R")));QVERIFY(editor.objectProperties().value("parentRows").toList().isEmpty());QVERIFY(editor.objectProperties().value("childRows").toList().isEmpty());
        QVERIFY(editor.objectProperties().contains("periodInput"));
    }
    void relationGpsMatchesHeaderFocusWithoutChangingSelection() {
        QTemporaryDir directory;QVERIFY(directory.isValid());EditorController editor;QVERIFY(openFixture(editor,directory.path()));
        QVERIFY(editor.selectObject(ref("C")));QSignalSpy focused(&editor,&EditorController::focusRequested);QVERIFY(editor.focusObject());QCOMPARE(focused.size(),qsizetype(1));const auto expected=focused.first();
        QVERIFY(editor.selectObject(ref("B")));QCOMPARE(rowIds(editor.objectProperties().value("childRows")),QStringList({"C","D"}));
        const auto before=editor.documentBytes();const auto selected=editor.primaryObject();const auto revision=editor.selectionRevision();
        QVERIFY(editor.focusObject(ref("C")));QCOMPARE(focused.last(),expected);QCOMPARE(editor.primaryObject(),selected);QCOMPARE(editor.selectionRevision(),revision);QCOMPARE(editor.documentBytes(),before);
    }
    void hierarchyNamesRefreshThroughOneUndo() {
        QTemporaryDir directory;QVERIFY(directory.isValid());EditorController editor;QVERIFY(openFixture(editor,directory.path()));
        QVERIFY(editor.selectObject(ref("C")));editor.setNameDraft("Changed child");QVERIFY(editor.commitObjectField("name"));
        QVERIFY(editor.selectObject(ref("B")));const auto changed=editor.objectProperties().value("childRows").toList();
        QVERIFY(!changed.isEmpty());QCOMPARE(changed.first().toMap().value("name").toString(),QString("Changed child"));
        editor.undo();const auto restored=editor.objectProperties().value("childRows").toList();
        QVERIFY(!restored.isEmpty());QCOMPARE(restored.first().toMap().value("name").toString(),QString("Child"));
    }
};
QTEST_MAIN(TerritorialInfoControllerTests)
#include "territorial_info_controller_tests.moc"
