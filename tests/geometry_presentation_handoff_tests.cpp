#include "editorcontroller.h"
#include "projectcodec.h"
#include "gpumapitem.h"
#include "maprenderitem.h"
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

using namespace pandoeditor;
namespace {
struct Fixture {
    QTemporaryDir directory;
    EditorController editor{[&]{EditorControllerConfig c;c.appearancePath=directory.filePath("appearance.json");return c;}()};
    bool open() {
        ProjectDocument d({{"A","Alpha",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0xabcdef}},{{"countries","Countries"}});
        Geometry line;line.type="LineString";line.lines={{{1,1},{5,1},{9,1}}};
        d.geometries.insert({"river",1},line);HydroFeature h;h.id="river";h.name="River";h.geometry={"river",1};h.source.kind="user";d.hydro.push_back(h);
        Project p;p.replace(std::move(d));QFile f(directory.filePath("project.json"));if(!f.open(QIODevice::WriteOnly))return false;
        f.write(projectcodec::encode(p));f.close();return editor.openFile(QUrl::fromLocalFile(f.fileName()))&&editor.setProjectionMode("flat")&&editor.resizeMapCamera(800,600,1);
    }
    MapSceneBridge* bridge(){return qobject_cast<MapSceneBridge*>(editor.mapSceneBridge());}
    bool commit(const QString& domain="territorial") {
        if(!editor.selectObject({{"domain",domain},{"id",domain=="hydro"?"river":"A"}},"replace"))return false;
        if(!(domain=="territorial"?editor.beginGeometryEdit():editor.beginContentEdit(domain)&&editor.beginContentGeometry()))return false;
        const auto paths=editor.geometryDraftPaths();if(paths.isEmpty())return false;
        const auto vertices=paths.front().toMap().value("vertices").toList();if(vertices.isEmpty())return false;
        const auto v=vertices.front().toMap();const auto x=v.value("x").toDouble(),y=v.value("y").toDouble();
        return editor.geometrySelectNearest(x,y,.001)&&editor.geometryMoveSelectedVertex(x+.01,y+.01,0)&&editor.requestGeometryPreview()&&editor.confirmGeometryEdit();
    }
};
QVariantList held(EditorController& e) {
    const auto value=e.property("geometryPresentationPaths");return value.isValid()?value.toList():e.geometryDraftPaths();
}
QVariantList inventory(const std::shared_ptr<const MapFrame>& frame) {
    QVariantList rows;for(const auto& p:frame->scene->strokes)rows.append(QVariantMap{{"domain",QString::fromStdString(p.object.domain)},{"id",QString::fromStdString(p.object.id)},{"geometryId",QString::fromStdString(p.geometry.id)},{"geometryVersion",QVariant::fromValue(qulonglong(p.geometry.version))}});return rows;
}
bool registerSource(EditorController& e,QObject* item) {return QMetaObject::invokeMethod(&e,"recordMapPresentationSource",Q_ARG(QObject*,item));}
}
// These invoke real renderer signals to test controller policy. Actual render
// barriers and GPU/CPU delivery are covered by the separate renderer device tests.
class GeometryPresentationHandoffTests:public QObject {
    Q_OBJECT
private slots:
    void finalStrokeSurvivesConfirm_data(){QTest::addColumn<QString>("domain");QTest::newRow("country")<<QString("territorial");QTest::newRow("hydro")<<QString("hydro");}
    void finalStrokeSurvivesConfirm(){QFETCH(QString,domain);Fixture f;QVERIFY(f.open());QVERIFY(f.commit(domain));QVERIFY(!f.editor.geometryEditState().value("active").toBool());QVERIFY2(!held(f.editor).isEmpty(),"Successful Apply must retain the final geographic stroke until presentation");}
    void receiptNeedsCurrentViewAndExactSuccessorAndNeverMutatesDocument(){
        Fixture f;QVERIFY(f.open());QVERIFY(f.commit());QVERIFY(!held(f.editor).isEmpty());
        MapRenderItem item;item.setSceneBridge(f.bridge());QVERIFY(registerSource(f.editor,&item));
        auto frame=f.bridge()->frameSnapshot();const auto bytes=f.editor.documentBytes();const auto revision=f.editor.revision();const auto undo=f.editor.canUndo();
        auto wrong=inventory(frame);for(auto& row:wrong){auto m=row.toMap();m["geometryVersion"]=m.value("geometryVersion").toULongLong()+1;row=m;}
        emit item.framePresented(frame,wrong);QVERIFY(!held(f.editor).isEmpty());
        QVERIFY(f.editor.publishMapView({{"translateX",frame->view.translateX+20}}));
        emit item.framePresented(frame,inventory(frame));QVERIFY(!held(f.editor).isEmpty());
        frame=f.bridge()->frameSnapshot();emit item.framePresented(frame,inventory(frame));QVERIFY(held(f.editor).isEmpty());
        QCOMPARE(f.editor.documentBytes(),bytes);QCOMPARE(f.editor.revision(),revision);QCOMPARE(f.editor.canUndo(),undo);
    }
    void cancelNewEditKeepsHeldStrokeAndUndoInvalidates(){
        Fixture f;QVERIFY(f.open());QVERIFY(f.commit());QVERIFY(!held(f.editor).isEmpty());
        const auto paths=held(f.editor);QVERIFY(f.editor.beginGeometryEdit());f.editor.cancelGeometryEdit();QCOMPARE(held(f.editor),paths);
        f.editor.undo();QVERIFY(held(f.editor).isEmpty());
    }
    void panReprojectsHeldStrokeAndOldAckCannotClearNewCommit(){
        Fixture f;QVERIFY(f.open());QVERIFY(f.commit());QVERIFY(!held(f.editor).isEmpty());
        GpuMapItem item;item.setSceneBridge(f.bridge());QVERIFY(registerSource(f.editor,&item));auto old=f.bridge()->frameSnapshot();const auto before=held(f.editor);
        QVERIFY(f.editor.publishMapView({{"translateX",old->view.translateX+30}}));QVERIFY(held(f.editor)!=before);
        QVERIFY(f.commit());emit item.framePresented(old,inventory(old));QVERIFY(!held(f.editor).isEmpty());
        const auto now=f.bridge()->frameSnapshot();emit item.framePresented(now,inventory(now));QVERIFY(held(f.editor).isEmpty());
    }
    void globeHorizonDoesNotCloseAnUnseenBoundary(){
        Fixture f;QVERIFY(f.open());QVERIFY(f.commit());
        QVERIFY(f.editor.setProjectionMode("globe"));
        QVERIFY(f.editor.publishMapView({{"centerLongitude",0.0},{"rotationLongitude",-85.0},{"rotationLatitude",0.0},{"rotationRoll",0.0}}));
        const auto paths=held(f.editor);QVERIFY(!paths.isEmpty());
        for(const auto& value:paths)QVERIFY2(!value.toMap().value("path").toString().contains('Z'),"A clipped stroke must not invent a closing edge behind the horizon");
    }
};
QTEST_MAIN(GeometryPresentationHandoffTests)
#include "geometry_presentation_handoff_tests.moc"
