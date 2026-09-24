#include "editorcontroller.h"
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
using namespace pandoeditor;
namespace {
QVariantMap ref(const QString& id,const QString& type="country") {
    return {{"domain","territorial"},{"type",type},{"id",id}};
}
QStringList ids(const QVariantList& values) {
    QStringList result;for(const auto& value:values)result.append(value.toMap()["id"].toString());return result;
}
void modes() {QTest::addColumn<bool>("mobile");QTest::newRow("desktop")<<false;QTest::newRow("mobile-common")<<true;}
EditorControllerConfig config(bool mobile,const QString& path) {EditorControllerConfig c;c.mobileMode=mobile;c.privateProjectPath=path;return c;}
ProjectDocument fixture() {
    ProjectDocument d({
        {"A","알파",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x123456},
        {"B","베타",{{{{20,0},{30,0},{30,10},{20,10},{20,0}}}},0xabcdef}
    },{{"countries","국가"},{"other","영역"},{"hidden","숨김",false,false,1}});
    d.documentId="selection-fixture-v1";
    auto add=[&](std::string id,UnitKind kind,const std::string& layer,double left,double right,bool locked){
        Geometry g;g.type="Polygon";g.polygons={{{{left,2},{right,2},{right,4},{left,4},{left,2}}}};
        GeometryRef gr{"selection-"+id,1};d.geometries.insert(gr,std::move(g));
        d.units.push_back({id,id,"note",kind,gr,locked});
        d.presentation.membership[territorialRef(id)]=layer;
        d.presentation.objectStyles[territorialRef(id)]={0xabcdef,0.75};
    };
    add("S",UnitKind::Subunit,"other",1,4,true);
    add("R",UnitKind::Region,"other",6,8,false);
    add("H",UnitKind::Region,"hidden",40,42,false);
    d.relations.push_back({"base-s",territorialRef("S"),territorialRef("A"),territorialRef("A")});
    validateDocument(d);return d;
}
QUrl writeFixture(const QString& directory) {
    Project project;project.replace(fixture());
    QFile file(directory+"/selection.pando.json");
    if(!file.open(QIODevice::WriteOnly))throw std::runtime_error("fixture write failed");
    file.write(projectcodec::encode(project));file.close();return QUrl::fromLocalFile(file.fileName());
}
QPointF center(const EditorController& editor,const QString& id) {
    for(const auto& value:editor.paths()) {
        const auto path=value.toMap();if(path["countryId"].toString()!=id)continue;
        return {path["left"].toDouble()+path["width"].toDouble()/2,path["top"].toDouble()+path["height"].toDouble()/2};
    }
    throw std::runtime_error("missing projected object");
}
}
class SelectionEditorTests : public QObject {
    Q_OBJECT
private slots:
    void chooserNamesIgnoreObjectOrder() {
        QTemporaryDir dir;
        auto document=fixture();
        auto other=document.units.at(2);other.id="T";other.name="가나다";
        document.units.at(2).name="하하";
        document.units.push_back(other);
        document.relations.push_back({"base-t",territorialRef("T"),territorialRef("A"),territorialRef("A")});
        document.presentation.membership[territorialRef("T")]="other";
        document.presentation.objectStyles[territorialRef("T")]=ObjectStyle{};
        // S is topmost by object order, but T must be first by display name.
        document.presentation.webPresentation.objectOrder={"territorial:subunit:T","territorial:subunit:S"};
        Project project;project.replace(std::move(document));
        QFile file(dir.filePath("chooser.json"));QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(projectcodec::encode(project));file.close();
        EditorController editor;QVERIFY(editor.openFile(QUrl::fromLocalFile(file.fileName())));
        const auto point=center(editor,"S");editor.beginMapSelection(point.x(),point.y(),false,0);
        QCOMPARE(editor.pickObject(point.x(),point.y())["id"].toString(),QString("S"));
        const auto candidates=ids(editor.objectChooserCandidates());
        QVERIFY(candidates.contains("S"));QVERIFY(candidates.contains("T"));
        QVERIFY(candidates.indexOf("T")<candidates.indexOf("S"));
    }
    void selectingDoesNotCommitOrDiscardDrafts() {
        EditorController editor;
        const auto rows=editor.countryRows();QVERIFY(rows.size()>=2);
        const auto a=rows[0].toMap()["id"].toString(),b=rows[1].toMap()["id"].toString();
        editor.selectCountry(a);const auto revision=editor.revision();
        const bool undo=editor.canUndo(),redo=editor.canRedo();
        editor.setNameDraft(QStringLiteral("uncommitted selection regression"));const auto dirty=editor.dirty();
        editor.selectCountry(b);
        QCOMPARE(editor.revision(),revision);QCOMPARE(editor.canUndo(),undo);QCOMPARE(editor.canRedo(),redo);QCOMPARE(editor.dirty(),dirty);
        editor.selectCountry(a);QCOMPARE(editor.nameDraft(),QStringLiteral("uncommitted selection regression"));
    }
    void sessionActionsPreserveHistoryAndBytes_data(){modes();}
    void sessionActionsPreserveHistoryAndBytes(){
        QFETCH(bool,mobile);QTemporaryDir dir;QVERIFY(dir.isValid());
        EditorController editor(config(mobile,dir.path()+"/private.json"));QVERIFY(editor.openFile(writeFixture(dir.path())));
        editor.selectCountry("A");editor.setNameDraft("Changed A");QVERIFY(editor.commitCountryField("name"));
        editor.setMemoDraft("Redo memo");QVERIFY(editor.commitCountryField("notes"));editor.undo();
        QVERIFY(editor.canUndo());QVERIFY(editor.canRedo());
        const auto bytes=editor.documentBytes(),instance=editor.projectInstanceId().toUtf8();const auto revision=editor.revision();
        const bool dirty=editor.dirty();QSignalSpy dirtySignals(&editor,&EditorController::dirtyChanged);
        QVERIFY(editor.selectObject(ref("B"),"toggle","objects"));
        QVERIFY(editor.selectObject(ref("S","subunit"),"range","objects",{ref("A"),ref("B"),ref("S","subunit")}));
        editor.setSearchQuery("하위단위");QCOMPARE(ids(editor.searchResults()),QStringList{"S"});
        QVERIFY(editor.setHoverObject(ref("R","region"),"map"));
        QSignalSpy focused(&editor,&EditorController::focusRequested);QVERIFY(editor.focusObject(ref("H","region")));QCOMPARE(focused.count(),1);
        QCOMPARE(focused.front().at(4).toDouble(),mobile?12.0:10.0);
        QVERIFY(editor.setHoverObject({},"map"));editor.clearSelection();editor.selectLayer("other");
        QCOMPARE(editor.documentBytes(),bytes);QCOMPARE(editor.revision(),revision);QCOMPARE(editor.projectInstanceId().toUtf8(),instance);
        QCOMPARE(editor.dirty(),dirty);QVERIFY(editor.canUndo());QVERIFY(editor.canRedo());QCOMPARE(dirtySignals.count(),0);
        editor.redo();editor.selectCountry("A");QCOMPARE(editor.memoDraft(),QString("Redo memo"));
    }
    void scopesPrimaryAndInvalidReferences(){
        QTemporaryDir dir;EditorController editor;QVERIFY(editor.openFile(writeFixture(dir.path())));
        const QVariantList ordered={ref("A"),ref("B"),ref("S","subunit"),ref("R","region")};
        QVERIFY(editor.selectObject(ref("A"),"replace","left"));
        QVERIFY(editor.selectObject(ref("B"),"toggle","left"));
        QVERIFY(editor.selectObject(ref("S","subunit"),"toggle","left"));
        QVERIFY(editor.selectObject(ref("S","subunit"),"toggle","left"));QCOMPARE(editor.primaryObject()["id"].toString(),QString("B"));
        QVERIFY(editor.selectObject(ref("R","region"),"range","right",ordered));QCOMPARE(ids(editor.selectionItems()),QStringList{"R"});
        QVERIFY(editor.selectObject(ref("B"),"range","right",ordered));QCOMPARE(ids(editor.selectionItems()),QStringList({"B","S","R"}));
        QCOMPARE(editor.rangeAnchor("right")["id"].toString(),QString("B"));QCOMPARE(editor.rangeAnchor("left")["id"].toString(),QString("S"));
        QVERIFY(editor.selectObject(ref("A"),"range","right",ordered));QCOMPARE(ids(editor.selectionItems()),QStringList({"A","B"}));
        const auto before=editor.selectionItems();const auto revision=editor.selectionRevision();
        QVERIFY(!editor.selectObject(ref("missing")));QVERIFY(!editor.selectObject(ref("S","country")));
        QVERIFY(editor.selectObject(ref("R","region"),"range","right",{ref("A"),ref("B")}));
        QCOMPARE(editor.selectionItems(),before);QCOMPARE(editor.selectionRevision(),revision);
        QVERIFY(editor.setSelection({ref("A"),ref("B"),ref("A")}));QCOMPARE(ids(editor.selectionItems()),QStringList({"A","B"}));QCOMPARE(editor.selectedId(),QString("A"));
    }
    void hiddenListAndLockedMapSelection(){
        QTemporaryDir dir;EditorController editor;QVERIFY(editor.openFile(writeFixture(dir.path())));
        QCOMPARE(editor.objectRows().size(),5);QCOMPARE(editor.paths().size(),5);
        const auto bytes=editor.documentBytes();
        editor.setSearchQuery("H");QCOMPARE(ids(editor.searchResults()),QStringList{"H"});
        QVERIFY(editor.selectObject(ref("H","region")));QCOMPARE(editor.selectedId(),QString("H"));
        const auto hidden=center(editor,"H");QVERIFY(editor.pickObject(hidden.x(),hidden.y()).isEmpty());
        const auto locked=center(editor,"S");QCOMPARE(editor.pickObject(locked.x(),locked.y())["id"].toString(),QString("S"));
        QVERIFY(editor.selectObject(ref("S","subunit")));QVERIFY(!editor.selectedEditable());
        QVERIFY(!editor.selectObject({{"domain","label"},{"type","city"},{"id","A"}}));
        QCOMPARE(editor.documentBytes(),bytes);QCOMPARE(editor.revision(),qulonglong(0));QVERIFY(!editor.dirty());
    }
    void parkedDraftsRemainBoundAndExplicitFailureIsAtomic_data(){modes();}
    void parkedDraftsRemainBoundAndExplicitFailureIsAtomic(){
        QFETCH(bool,mobile);QTemporaryDir dir;EditorController editor(config(mobile,dir.path()+"/private.json"));QVERIFY(editor.openFile(writeFixture(dir.path())));
        editor.selectCountry("A");editor.setNameDraft("Draft A");editor.setColorDraft("invalid");
        const auto original=editor.documentBytes();editor.selectCountry("B");
        editor.setNameDraft("Committed B");QVERIFY(editor.commitCountryField("name"));QVERIFY(editor.hasPendingEdits());
        const auto before=editor.documentBytes();const auto revision=editor.revision();
        QVERIFY(!editor.commitPendingEdits());QCOMPARE(editor.documentBytes(),before);QCOMPARE(editor.revision(),revision);
        editor.selectCountry("A");QCOMPARE(editor.nameDraft(),QString("Draft A"));QCOMPARE(editor.colorDraft(),QString("invalid"));
        editor.setColorDraft("#123456");QVERIFY(editor.commitPendingEdits());QVERIFY(!editor.hasPendingEdits());
        editor.selectCountry("B");QCOMPARE(editor.nameDraft(),QString("Committed B"));
        editor.undo();editor.selectCountry("A");QVERIFY(editor.nameDraft()!=QString("Draft A"));QVERIFY(editor.canRedo());
        editor.setMemoDraft("discard parked");editor.selectCountry("B");editor.discardPendingEdits();QVERIFY(!editor.hasPendingEdits());QVERIFY(editor.canRedo());
        QVERIFY(editor.documentBytes()!=original);
    }
    void preparedPreviewSurvivesSelectionOnly(){
        QTemporaryDir dir;EditorController editor;QVERIFY(editor.openFile(writeFixture(dir.path())));
        editor.selectCountry("A");editor.setNameDraft("Prepared A");QVERIFY(editor.preparePendingEdits());QVERIFY(editor.hasPreparedPreview());
        const auto before=editor.documentBytes();const auto revision=editor.revision();
        editor.selectCountry("B");editor.setSearchQuery("A");QVERIFY(editor.focusObject(ref("A")));
        QVERIFY(editor.hasPreparedPreview());QCOMPARE(editor.documentBytes(),before);QCOMPARE(editor.revision(),revision);
        QVERIFY(editor.confirmPreview());QCOMPARE(editor.selectedId(),QString("B"));QVERIFY(!editor.hasPendingEdits());
        editor.selectCountry("A");QCOMPARE(editor.nameDraft(),QString("Prepared A"));
    }
    void staleHoverExitCannotClearNewHover(){
        QTemporaryDir dir;EditorController editor;QVERIFY(editor.openFile(writeFixture(dir.path())));
        QVERIFY(editor.setHoverObject(ref("A"),"map"));const auto oldKey=editor.hoverObject()["key"].toString();
        QVERIFY(editor.setHoverObject(ref("B"),"search"));const auto revision=editor.hoverRevision();
        QVERIFY(!editor.setHoverObject({},"map",oldKey));QCOMPARE(editor.hoverRevision(),revision);QCOMPARE(editor.hoverObject()["id"].toString(),QString("B"));
        QVERIFY(!editor.setHoverObject(ref("missing"),"map"));QCOMPARE(editor.hoverRevision(),revision);
        QVERIFY(editor.setHoverObject({},"search"));QVERIFY(editor.hoverObject().isEmpty());
    }
    void sameFileReopenResetsOnlySessionAfterSuccess(){
        QTemporaryDir dir;EditorController editor;const auto file=writeFixture(dir.path());QVERIFY(editor.openFile(file));
        const auto bytes=editor.documentBytes();const auto instance=editor.projectInstanceId();
        editor.selectObject(ref("A"),"replace","left");editor.setNameDraft("Uncommitted");editor.setSearchQuery("R");editor.setHoverObject(ref("R","region"),"search");
        QVERIFY(!editor.openFile(QUrl::fromLocalFile(dir.path()+"/missing.json")));QCOMPARE(editor.selectedId(),QString("A"));QVERIFY(editor.hasPendingEdits());
        QVERIFY(editor.openFile(file));QVERIFY(editor.projectInstanceId()!=instance);QVERIFY(editor.selectionItems().isEmpty());QVERIFY(editor.rangeAnchor("left").isEmpty());
        QVERIFY(editor.hoverObject().isEmpty());QVERIFY(editor.searchQuery().isEmpty());QVERIFY(!editor.hasPendingEdits());QCOMPARE(editor.selectionRevision(),qulonglong(0));
        QCOMPARE(editor.documentBytes(),bytes);QVERIFY(!editor.canUndo());QVERIFY(!editor.canRedo());
    }
    void webImportPreviewUnaffectedBySelection(){
        QTemporaryDir dir;EditorController editor;
        QVERIFY(editor.prepareWebImport(QUrl::fromLocalFile(QString(WEB_IMPORT_FIXTURES)+"/v5.input.json")));
        QTRY_VERIFY_WITH_TIMEOUT(editor.hasWebImportPreview(),10000);
        const auto hash=editor.webImportHash();editor.selectCountry(editor.countryRows().front().toMap()["id"].toString());
        editor.setSearchQuery("국가");QVERIFY(editor.hasWebImportPreview());QCOMPARE(editor.webImportHash(),hash);
        QVERIFY(editor.confirmWebImport(hash,"discard"));QVERIFY(editor.selectionItems().isEmpty());QVERIFY(editor.searchQuery().isEmpty());
    }
};
QTEST_GUILESS_MAIN(SelectionEditorTests)
#include "selection_editor_tests.moc"
