#include "editorcontroller.h"
#include "projectcodec.h"
#include "territorial_fixture.h"
#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

using namespace pandoeditor;
namespace {
Geometry polygon(Ring ring) {
    Geometry result; result.type = "Polygon"; result.polygons = {{std::move(ring)}}; return result;
}
Geometry box(double x, double y, double width, double height) {
    return polygon({{x,y},{x+width,y},{x+width,y+height},{x,y+height},{x,y}});
}
ProjectDocument fixture(double latitude = 0) {
    ProjectDocument document({{"A","A",box(0,latitude,10,10).polygons,0xabcdef}},{{"countries","Countries"}});
    document.documentId = "edit-screen-controller-equivalence";
    Geometry line; line.type = "LineString"; line.lines = {{{1,latitude+1},{5,latitude+1},{9,latitude+1}}};
    document.geometries.insert({"river",1},line);
    HydroFeature river; river.id = "river"; river.name = "River"; river.geometry = {"river",1}; river.source.kind = "user";
    document.hydro.push_back(river);
    Geometry point; point.type = "Point"; point.points = {{2,latitude+2}};
    document.geometries.insert({"label",1},point);
    PlaceLabel label; label.id = "label"; label.name = "Label"; label.geometry = {"label",1}; label.source.kind = "user";
    document.labels.push_back(label);
    return document;
}
ProjectDocument boundaryFixture() {
    const auto a = polygon({{0,0},{1,0},{1,.5},{1,1},{0,1},{0,0}});
    const auto b = polygon({{1,0},{2,0},{2,1},{1,1},{1,.5},{1,0}});
    const auto c = polygon({{0,1},{1,1},{2,1},{2,2},{0,2},{0,1}});
    ProjectDocument document({{"A","A",a.polygons,0xabcdef},{"B","B",b.polygons,0x123456},{"C","C",c.polygons,0x654321}},{{"countries","Countries"}});
    document.documentId = "edit-screen-boundary-equivalence";
    return document;
}

// QVariant considers NaN unequal to itself. Preserve every field and every
// finite bit pattern, while giving independently absent snap points one token.
QVariant comparable(const QVariant& value) {
    if (value.metaType().id() == QMetaType::QVariantMap) {
        auto map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it) it.value() = comparable(it.value());
        return map;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        auto list = value.toList();
        for (auto& item : list) item = comparable(item);
        return list;
    }
    if (value.metaType().id() == QMetaType::Double) {
        const double number = value.toDouble();
        if (std::isnan(number)) return QStringLiteral("<NaN>");
        // String bit patterns also distinguish signed zero; no epsilon hides
        // a changed inverse or screen-reconstruction operation order.
        quint64 bits; static_assert(sizeof(bits) == sizeof(number));
        std::memcpy(&bits,&number,sizeof(bits));
        return QStringLiteral("double:%1").arg(bits,16,16,QLatin1Char('0'));
    }
    return value;
}
QString firstDifference(const QVariant& actual, const QVariant& expected, const QString& path = {}) {
    if (actual == expected) return {};
    if (actual.metaType().id() == QMetaType::QVariantMap && expected.metaType().id() == QMetaType::QVariantMap) {
        const auto a = actual.toMap(), e = expected.toMap();
        if (a.keys() != e.keys()) return path+": different map keys";
        for (auto it = a.begin(); it != a.end(); ++it) {
            const auto difference = firstDifference(it.value(),e[it.key()],path+"."+it.key());
            if (!difference.isEmpty()) return difference;
        }
    } else if (actual.metaType().id() == QMetaType::QVariantList && expected.metaType().id() == QMetaType::QVariantList) {
        const auto a = actual.toList(), e = expected.toList();
        if (a.size() != e.size()) return path+": different list sizes";
        for (qsizetype i = 0; i < a.size(); ++i) {
            const auto difference = firstDifference(a[i],e[i],path+QString("[%1]").arg(i));
            if (!difference.isEmpty()) return difference;
        }
    }
    return QString("%1: actual=%2 expected=%3").arg(path,actual.toString(),expected.toString());
}
QVariantMap observation(EditorController& controller) {
    return {{"edit",controller.geometryEditState()}, {"paths",controller.geometryDraftPaths()},
            {"snap",controller.geometrySnapState()}, {"content",controller.contentEditState()},
            {"selection",controller.selectionItems()}, {"primary",controller.selectedId()},
            {"selectionRevision",controller.selectionRevision()}, {"revision",controller.revision()},
            {"canUndo",controller.canUndo()}, {"canRedo",controller.canRedo()},
            {"camera",controller.mapViewState()}, {"canonical",controller.documentBytes()}};
}
struct Pair {
    QTemporaryDir directory;
    QStringList legacySignals, screenSignals;
    EditorController legacy, screen;
    MapProjection projection;
    explicit Pair(bool mobile = false)
        : legacy({mobile,directory.filePath("legacy-private.json")}),
          screen({mobile,directory.filePath("screen-private.json")}) {
        const auto trace = [](EditorController& c, QStringList& values) {
            QObject::connect(&c,&EditorController::geometryEditChanged,&c,[&values]{values.append("edit");});
            QObject::connect(&c,&EditorController::geometryChanged,&c,[&values]{values.append("geometry");});
            QObject::connect(&c,&EditorController::contentEditChanged,&c,[&values]{values.append("content");});
            QObject::connect(&c,&EditorController::viewStateChanged,&c,[&values]{values.append("view");});
            QObject::connect(&c,&EditorController::selectionChanged,&c,[&values]{values.append("selection");});
            QObject::connect(&c,&EditorController::stateChanged,&c,[&values]{values.append("state");});
            QObject::connect(&c,&EditorController::errorOccurred,&c,[&values](const QString& message){values.append("error:"+message);});
        };
        trace(legacy,legacySignals); trace(screen,screenSignals);
    }
    bool open(const ProjectDocument& document = fixture()) {
        Project project; project.replace(document);
        QFile file(directory.filePath("input.json"));
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.write(projectcodec::encode(project)); file.close();
        projection.rebuild(document);
        const auto url = QUrl::fromLocalFile(file.fileName());
        return legacy.openFile(url) && screen.openFile(url) &&
               legacy.setProjectionMode("flat") && screen.setProjectionMode("flat") &&
               legacy.resizeMapCamera(1100,760,2) && screen.resizeMapCamera(1100,760,2);
    }
    template<class Action> bool both(Action action) { return action(legacy) && action(screen); }
    void clearSignals() { legacySignals.clear(); screenSignals.clear(); }
    Point at(Point geographic) const {
        const auto map = projection.project(geographic);
        const auto camera = legacy.mapViewState();
        return {camera["originX"].toDouble()+map.x*camera["mapScale"].toDouble(),
                camera["originY"].toDouble()+map.y*camera["mapScale"].toDouble()};
    }
    bool edit(const QString& domain = "territorial") {
        return both([&](EditorController& c) {
            const auto id = domain == "territorial" ? "A" : domain == "hydro" ? "river" : "label";
            if (!c.selectObject({{"domain",domain},{"id",id}},"replace")) return false;
            return domain == "territorial" ? c.beginGeometryEdit() : c.beginContentEdit(domain) && c.beginContentGeometry();
        });
    }
    bool draw() {
        return both([](EditorController& c){c.selectCountry("A");return c.beginGeometryDraw();});
    }
};
bool equivalent(Pair& pair, bool includeSignals = false) {
    const auto screen = comparable(observation(pair.screen));
    const auto legacy = comparable(observation(pair.legacy));
    if (!QTest::qCompare(screen,legacy,"screen observable state","legacy observable state",__FILE__,__LINE__)) return false;
    return !includeSignals || QTest::qCompare(pair.screenSignals,pair.legacySignals,"screen signal order","legacy signal order",__FILE__,__LINE__);
}

enum class Operation { Add, Select, Insert, Move, Hover, Pick, Translate };
const char* methodName(Operation operation) {
    switch (operation) {
    case Operation::Add: return "geometryAddPointScreen";
    case Operation::Select: return "geometrySelectNearestScreen";
    case Operation::Insert: return "geometryInsertNearestScreen";
    case Operation::Move: return "geometryMoveSelectedVertexScreen";
    case Operation::Hover: return "geometryHoverSnapScreen";
    case Operation::Pick: return "geometryPickTerritorySelectionScreen";
    case Operation::Translate: return "geometryTranslateObjectScreen";
    }
    return "";
}
struct CallResult { bool invoked = false, legacy = false, screen = false; };
CallResult apply(Pair& pair, Operation operation, Point input, double radius = 0, const QString& pointer = {}) {
    // This is intentionally the old QML arithmetic, not the extracted helper.
    // Each input event uses a fresh display, including events within one drag.
    const auto camera = pair.legacy.mapViewState();
    const double scale = camera["mapScale"].toDouble();
    const double x = operation == Operation::Translate ? input.x/scale : (input.x-camera["originX"].toDouble())/scale;
    const double y = operation == Operation::Translate ? input.y/scale : (input.y-camera["originY"].toDouble())/scale;
    const double tolerance = radius/scale;
    pair.clearSignals();
    CallResult result;
    switch (operation) {
    case Operation::Add: result.legacy = pair.legacy.geometryAddPoint(x,y,tolerance,pointer); break;
    case Operation::Select: result.legacy = pair.legacy.geometrySelectNearest(x,y,tolerance); break;
    case Operation::Insert: result.legacy = pair.legacy.geometryInsertNearest(x,y,tolerance); break;
    case Operation::Move: result.legacy = pair.legacy.geometryMoveSelectedVertex(x,y,tolerance,pointer); break;
    case Operation::Hover: result.legacy = pair.legacy.geometryHoverSnap(x,y,pointer); break;
    case Operation::Pick: result.legacy = pair.legacy.geometryPickTerritorySelection(x,y); break;
    case Operation::Translate: result.legacy = pair.legacy.geometryTranslateObject(x,y); break;
    }
    const auto* name = methodName(operation);
    if (operation == Operation::Add || operation == Operation::Move)
        result.invoked = QMetaObject::invokeMethod(&pair.screen,name,Qt::DirectConnection,Q_RETURN_ARG(bool,result.screen),Q_ARG(double,input.x),Q_ARG(double,input.y),Q_ARG(double,radius),Q_ARG(QString,pointer));
    else if (operation == Operation::Select || operation == Operation::Insert)
        result.invoked = QMetaObject::invokeMethod(&pair.screen,name,Qt::DirectConnection,Q_RETURN_ARG(bool,result.screen),Q_ARG(double,input.x),Q_ARG(double,input.y),Q_ARG(double,radius));
    else if (operation == Operation::Hover)
        result.invoked = QMetaObject::invokeMethod(&pair.screen,name,Qt::DirectConnection,Q_RETURN_ARG(bool,result.screen),Q_ARG(double,input.x),Q_ARG(double,input.y),Q_ARG(QString,pointer));
    else
        result.invoked = QMetaObject::invokeMethod(&pair.screen,name,Qt::DirectConnection,Q_RETURN_ARG(bool,result.screen),Q_ARG(double,input.x),Q_ARG(double,input.y));
    return result;
}
#define VERIFY_CALL(pair, operation, input, radius, pointer, expected) do { \
    const auto result = apply(pair, operation, input, radius, pointer); \
    QVERIFY2(result.invoked,methodName(operation)); \
    QCOMPARE(result.screen,result.legacy); QCOMPARE(result.legacy,expected); \
    QVERIFY(equivalent(pair,true)); \
} while (false)

bool changeCamera(Pair& pair, int event) {
    return pair.both([&](EditorController& c) {
        if (!c.resizeMapCamera(event%2 ? 360 : 1100,640+event*7,event%2 ? 3 : 1)) return false;
        if (!c.zoomMapCameraAt(1.17+event*.03,137.25,213.75)) return false;
        c.beginMapCameraPan();
        const bool changed = c.updateMapCameraPan(31.125-event*7,-22.875+event*3);
        c.endMapCameraPan(); return changed;
    });
}
}

class EditScreenControllerTests : public QObject {
    Q_OBJECT
private slots:
    void displayAdaptersAreInvokable() {
        const auto& meta=EditorController::staticMetaObject;
        for(const char* signature:{"editMapPointToScreen(double,double,QVariantMap)",
                                   "editPixelLengthToMap(double,QVariantMap)",
                                   "editLabelDragToMap(double,double,double,double,QVariantMap)",
                                   "editMapRectToScreen(QRectF,QVariantMap)",
                                   "editMapDragPosition(double,double,double,double,QVariantMap)",
                                   "editMapRectGeographicBounds(QRectF,QVariantMap)"})
            QVERIFY2(meta.indexOfMethod(signature)>=0,signature);
    }
    void displayAdaptersUseOnlySuppliedSnapshots() {
        EditorController editor({false,QString()});
        const QVariantMap camera{{"originX",123.4},{"originY",-98.125},{"mapScale",3.7},{"devicePixelRatio",3.}};
        const QVariantMap projection{{"cosLatitude",.17364817766693041},{"minX",21.31415926535898},{"maxLatitude",88.}};
        const QRectF rect(.1,18,12.25,9.5);
        const auto before=editor.documentBytes();const auto liveCamera=editor.mapViewState();
        const auto preparation=editor.renderQuality()["scenePreparationCount"];
        QSignalSpy geometry(&editor,&EditorController::geometryChanged),edit(&editor,&EditorController::geometryEditChanged);
        const auto check=[&] {
            const auto point=editor.editMapPointToScreen(rect.x(),rect.y(),camera);
            if(!QTest::qCompare(comparable(point.x()),comparable(123.4+rect.x()*3.7),"screen x","old screen x",__FILE__,__LINE__))return false;
            if(!QTest::qCompare(comparable(point.y()),comparable(-98.125+rect.y()*3.7),"screen y","old screen y",__FILE__,__LINE__))return false;
            const auto screen=editor.editMapRectToScreen(rect,camera);
            if(!QTest::qCompare(screen,QRectF(point,QSizeF(rect.width()*3.7,rect.height()*3.7)),"screen rect","old screen rect",__FILE__,__LINE__))return false;
            const auto label=editor.editLabelDragToMap(937.5638261969304,18,-495.95395331482916,9.5,camera);
            if(!QTest::qCompare(comparable(label.x()),comparable((937.5638261969304-495.95395331482916-123.4)/3.7),"label x","old label x",__FILE__,__LINE__))return false;
            const auto drag=editor.editMapDragPosition(rect.x(),rect.y(),-7.25,9.5,camera);
            if(!QTest::qCompare(drag,QPointF(rect.x()-7.25/3.7,rect.y()+9.5/3.7),"drag","old drag",__FILE__,__LINE__))return false;
            const auto bounds=editor.editMapRectGeographicBounds(rect,projection);
            const QVariantMap expected{{"west",(rect.x()+21.31415926535898)/.17364817766693041},
                {"east",(rect.x()+rect.width()+21.31415926535898)/.17364817766693041},
                {"north",88.-rect.y()},{"south",88.-rect.y()-rect.height()}};
            return QTest::qCompare(comparable(bounds),comparable(expected),"bounds","old bounds",__FILE__,__LINE__);
        };
        QVERIFY(check());QCOMPARE(editor.editPixelLengthToMap(3,camera),3./3.7);
        QCOMPARE(editor.mapViewState(),liveCamera);QCOMPARE(editor.renderQuality()["scenePreparationCount"],preparation);
        QVERIFY(editor.setProjectionMode("flat"));QVERIFY(editor.resizeMapCamera(913,587));
        QVERIFY(editor.zoomMapCameraAt(1.7,211,139));
        editor.beginMapCameraPan();QVERIFY(editor.updateMapCameraPan(-19,37));editor.endMapCameraPan();
        QVERIFY(check());QCOMPARE(editor.documentBytes(),before);QCOMPARE(geometry.count(),0);QCOMPARE(edit.count(),0);
        // The old QML view properties use finite fallbacks for display state.
        const auto nan=std::numeric_limits<double>::quiet_NaN();
        QCOMPARE(editor.editMapPointToScreen(5,7,{{"originX",nan},{"originY",nan},{"mapScale",nan}}),QPointF(5,7));
    }
    void screenEntrypointsAreInvokable() {
        const auto& meta = EditorController::staticMetaObject;
        for (const auto* signature : {
                 "geometryAddPointScreen(double,double,double,QString)",
                 "geometrySelectNearestScreen(double,double,double)",
                 "geometryInsertNearestScreen(double,double,double)",
                 "geometryMoveSelectedVertexScreen(double,double,double,QString)",
                 "geometryHoverSnapScreen(double,double,QString)",
                 "geometryPickTerritorySelectionScreen(double,double)",
                 "geometryTranslateObjectScreen(double,double)"})
            QVERIFY2(meta.indexOfMethod(signature) >= 0, signature);
    }
    void inactiveEntrypointsPreserveGuardsAndEmitNothing() {
        Pair pair; QVERIFY(pair.open());
        for (const auto operation : {Operation::Add,Operation::Select,Operation::Insert,Operation::Move,Operation::Hover,Operation::Pick,Operation::Translate}) {
            VERIFY_CALL(pair,operation,(Point{-17.25,123.75}),18,"touch",false);
            QVERIFY(pair.screenSignals.isEmpty());
        }
    }
    void nativeGlobeEntrypointsKeepLegacyEditingAndCameraMode() {
        Pair pair; QVERIFY(pair.open());
        QVERIFY(pair.both([](EditorController& c){return c.setProjectionMode("globe");}));
        const auto camera = pair.screen.mapViewState();
        QVERIFY(pair.draw());
        VERIFY_CALL(pair,Operation::Hover,pair.at({2,2}),0,"mouse",false);
        VERIFY_CALL(pair,Operation::Add,pair.at({2,2}),0,"mouse",true);
        VERIFY_CALL(pair,Operation::Add,pair.at({6,2}),0,"touch",true);
        QCOMPARE(pair.screen.mapViewState(),camera);
        QCOMPARE(pair.screen.projectionMode(),QString("globe"));
        pair.legacy.cancelGeometryEdit(); pair.screen.cancelGeometryEdit();
        QVERIFY(pair.edit());
        VERIFY_CALL(pair,Operation::Select,pair.at({0,0}),10,"",true);
        VERIFY_CALL(pair,Operation::Move,pair.at({-.25,.25}),0,"mouse",true);
        VERIFY_CALL(pair,Operation::Insert,pair.at({10,5}),10,"",true);
        QCOMPARE(pair.screen.mapViewState(),camera);
        QCOMPARE(pair.screen.projectionMode(),QString("globe"));
    }
    void drawingUsesEachEventCameraAndPreservesExactPaths_data() {
        QTest::addColumn<bool>("mobile"); QTest::addColumn<double>("latitude");
        for (const bool mobile : {false,true}) for (const double latitude : {0.,59.})
            QTest::newRow(qPrintable(QString("%1-lat%2").arg(mobile?"360px":"desktop").arg(latitude))) << mobile << latitude;
    }
    void drawingUsesEachEventCameraAndPreservesExactPaths() {
        QFETCH(bool,mobile); QFETCH(double,latitude);
        Pair pair(mobile); QVERIFY(pair.open(fixture(latitude))); QVERIFY(pair.draw());
        const auto canonical = pair.legacy.documentBytes();
        int event = 0;
        for (const auto geographic : {Point{1,latitude+1},Point{7,latitude+1},Point{7,latitude+7},Point{1,latitude+7}}) {
            const auto previous = pair.legacy.geometryDraftPaths();
            QVERIFY(changeCamera(pair,++event));
            QCOMPARE(pair.legacy.geometryDraftPaths(),previous);
            VERIFY_CALL(pair,Operation::Add,pair.at(geographic),0,"mouse",true);
            QCOMPARE(pair.screen.documentBytes(),canonical);
        }
        const auto drawn = pair.screen.geometryDraftPaths();
        QVERIFY(pair.both([](EditorController& c){return c.geometryUndoDraft();})); QVERIFY(equivalent(pair));
        QVERIFY(pair.screen.geometryDraftPaths() != drawn);
        QVERIFY(pair.both([](EditorController& c){return c.geometryRedoDraft();})); QVERIFY(equivalent(pair));
        QCOMPARE(pair.screen.geometryDraftPaths(),drawn);
        pair.legacy.cancelGeometryEdit(); pair.screen.cancelGeometryEdit(); QVERIFY(equivalent(pair));
        QCOMPARE(pair.screen.documentBytes(),canonical); QVERIFY(!pair.screen.canUndo());
        QVERIFY(pair.edit());
        VERIFY_CALL(pair,Operation::Add,pair.at({3,latitude+3}),0,"mouse",false);
    }
    void selectionInsertionVertexGesturesAndCanonicalHistory_data() {
        QTest::addColumn<QString>("domain");
        QTest::newRow("polygon") << QString("territorial");
        QTest::newRow("line") << QString("hydro");
        QTest::newRow("point") << QString("label");
    }
    void selectionInsertionVertexGesturesAndCanonicalHistory() {
        QFETCH(QString,domain); Pair pair; QVERIFY(pair.open()); QVERIFY(pair.edit(domain));
        const auto canonical = pair.screen.documentBytes();
        const Point vertex = domain=="territorial" ? Point{0,0} : domain=="hydro" ? Point{1,1} : Point{2,2};
        const Point midpoint = domain=="territorial" ? Point{5,0} : Point{3,1};
        VERIFY_CALL(pair,Operation::Insert,pair.at(midpoint),10,"",domain!="label");
        if (domain!="label") {
            QVERIFY(pair.both([](EditorController& c){return c.geometryUndoDraft();}));
            QVERIFY(pair.both([](EditorController& c){return c.geometryRedoDraft();}));
            QVERIFY(pair.both([](EditorController& c){return c.geometryUndoDraft();}));
        }
        VERIFY_CALL(pair,Operation::Select,pair.at(vertex),10,"",true);
        const auto originalPaths = pair.screen.geometryDraftPaths();
        QVERIFY(pair.both([](EditorController& c){return c.geometryBeginVertexDrag();}));
        pair.legacy.geometryEndVertexDrag(false); pair.screen.geometryEndVertexDrag(false);
        QVERIFY(equivalent(pair));
        // Ordinary vertex release records a no-op, unlike object/boundary drag.
        QVERIFY(pair.screen.geometryEditState()["canUndo"].toBool());
        QVERIFY(pair.both([](EditorController& c){return c.geometryUndoDraft();}));
        QCOMPARE(pair.screen.geometryDraftPaths(),originalPaths);
        VERIFY_CALL(pair,Operation::Select,pair.at(vertex),10,"",true);
        QVERIFY(pair.both([](EditorController& c){return c.geometryBeginVertexDrag();}));
        QVERIFY(changeCamera(pair,1));
        VERIFY_CALL(pair,Operation::Move,pair.at({vertex.x-.25,vertex.y+.25}),0,"touch",true);
        QVERIFY(changeCamera(pair,2));
        VERIFY_CALL(pair,Operation::Move,pair.at({vertex.x-.5,vertex.y+.5}),0,"mouse",true);
        pair.legacy.geometryEndVertexDrag(true); pair.screen.geometryEndVertexDrag(true);
        QVERIFY(equivalent(pair)); QCOMPARE(pair.screen.geometryDraftPaths(),originalPaths);
        VERIFY_CALL(pair,Operation::Select,pair.at(vertex),10,"",true);
        QVERIFY(pair.both([](EditorController& c){return c.geometryBeginVertexDrag();}));
        VERIFY_CALL(pair,Operation::Move,pair.at({vertex.x-.25,vertex.y+.25}),0,"mouse",true);
        pair.legacy.geometryEndVertexDrag(false); pair.screen.geometryEndVertexDrag(false);
        QVERIFY(equivalent(pair)); QCOMPARE(pair.screen.documentBytes(),canonical);
        QVERIFY(pair.both([](EditorController& c){return c.requestGeometryPreview();}));
        QTRY_VERIFY_WITH_TIMEOUT(pair.legacy.geometryEditState()["previewReady"].toBool() && pair.screen.geometryEditState()["previewReady"].toBool(),10000);
        QVERIFY(equivalent(pair));
        VERIFY_CALL(pair,Operation::Select,pair.at(vertex),10,"",false);
        VERIFY_CALL(pair,Operation::Move,pair.at(vertex),0,"",false);
        QVERIFY(pair.both([](EditorController& c){return c.confirmGeometryEdit();}));
        QVERIFY(equivalent(pair)); const auto committed = pair.screen.documentBytes(); QVERIFY(committed != canonical);
        pair.legacy.undo(); pair.screen.undo(); QVERIFY(equivalent(pair)); QCOMPARE(pair.screen.documentBytes(),canonical);
        pair.legacy.redo(); pair.screen.redo(); QVERIFY(equivalent(pair)); QCOMPARE(pair.screen.documentBytes(),committed);
    }
    void radiusPolicyIsLegacySpecific_data() {
        QTest::addColumn<int>("operationValue"); QTest::addColumn<double>("radius");
        const std::array<std::pair<const char*,double>,5> radii {{{"zero",0},{"negative",-18},{"NaN",std::numeric_limits<double>::quiet_NaN()},{"infinity",std::numeric_limits<double>::infinity()},{"positive",10}}};
        for (const auto operation : {Operation::Add,Operation::Select,Operation::Insert,Operation::Move})
            for (const auto& radius : radii)
                QTest::newRow(qPrintable(QString("%1-%2").arg(methodName(operation),radius.first))) << int(operation) << radius.second;
    }
    void radiusPolicyIsLegacySpecific() {
        QFETCH(int,operationValue); QFETCH(double,radius); const auto operation = Operation(operationValue);
        Pair pair; QVERIFY(pair.open());
        QVERIFY(operation==Operation::Add ? pair.draw() : pair.edit());
        if (operation==Operation::Select || operation==Operation::Move)
            VERIFY_CALL(pair,Operation::Select,pair.at({0,0}),10,"",true);
        const auto before = pair.screen.geometryDraftPaths();
        const int selected = pair.screen.geometryEditState()["selectedVertex"].toInt();
        const Point input = pair.at(operation==Operation::Insert ? Point{5,0} : Point{0,0});
        // NaN is not a universal rejection: add/move accept raw coordinates,
        // select clears its previous hit, and insert is inert.
        const bool expected = operation==Operation::Add || operation==Operation::Move || (radius>=0 && !std::isnan(radius));
        VERIFY_CALL(pair,operation,input,radius,"mouse",expected);
        if (operation==Operation::Select && radius<0) {
            QCOMPARE(pair.screen.geometryEditState()["selectedVertex"].toInt(),selected);
            QVERIFY(pair.screenSignals.isEmpty());
        }
        if (operation==Operation::Select && std::isnan(radius)) {
            QCOMPARE(pair.screen.geometryEditState()["selectedVertex"].toInt(),-1);
            QCOMPARE(pair.screenSignals,QStringList{"edit"});
        }
        if (operation==Operation::Insert && !expected) {
            QCOMPARE(pair.screen.geometryDraftPaths(),before); QVERIFY(pair.screenSignals.isEmpty());
        }
    }
    void invalidCoordinatesPreserveActiveSessionAndSignals_data() {
        QTest::addColumn<double>("x"); QTest::addColumn<double>("y");
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        const auto inf = std::numeric_limits<double>::infinity();
        QTest::newRow("nan-x") << nan << 20.; QTest::newRow("nan-y") << 20. << nan;
        QTest::newRow("positive-infinity") << inf << 20.; QTest::newRow("negative-infinity") << 20. << -inf;
    }
    void invalidCoordinatesPreserveActiveSessionAndSignals() {
        QFETCH(double,x); QFETCH(double,y); Pair pair; QVERIFY(pair.open()); QVERIFY(pair.edit());
        VERIFY_CALL(pair,Operation::Select,pair.at({0,0}),10,"",true);
        for (const auto operation : {Operation::Select,Operation::Insert,Operation::Move,Operation::Pick}) {
            const auto before = comparable(observation(pair.screen));
            VERIFY_CALL(pair,operation,(Point{x,y}),18,"touch",false);
            QCOMPARE(comparable(observation(pair.screen)),before); QVERIFY(pair.screenSignals.isEmpty());
        }
        pair.legacy.cancelGeometryEdit(); pair.screen.cancelGeometryEdit(); QVERIFY(pair.draw());
        VERIFY_CALL(pair,Operation::Add,pair.at({1,1}),0,"mouse",true);
        for (const auto operation : {Operation::Add,Operation::Hover}) {
            VERIFY_CALL(pair,operation,(Point{x,y}),18,"touch",false); QVERIFY(pair.screenSignals.isEmpty());
        }
        pair.legacy.cancelGeometryEdit(); pair.screen.cancelGeometryEdit(); QVERIFY(pair.edit("hydro"));
        QVERIFY(pair.both([](EditorController& c){return c.geometrySetMoveMode(true) && c.geometryBeginObjectDrag();}));
        VERIFY_CALL(pair,Operation::Translate,(Point{x,y}),0,"",false); QVERIFY(pair.screenSignals.isEmpty());
    }
    void negativeScreenCoordinatesRemainRawEditableInput() {
        Pair pair; QVERIFY(pair.open()); QVERIFY(pair.draw());
        VERIFY_CALL(pair,Operation::Add,(Point{-17.25,-33.75}),-1,"mouse",true);
        QVERIFY(!pair.screen.geometryDraftPaths().isEmpty());
        pair.legacy.cancelGeometryEdit(); pair.screen.cancelGeometryEdit(); QVERIFY(pair.edit());
        VERIFY_CALL(pair,Operation::Select,pair.at({0,0}),10,"",true);
        VERIFY_CALL(pair,Operation::Move,(Point{-42.25,-73.125}),0,"mouse",true);
        QVERIFY(equivalent(pair)); QVERIFY(!pair.screen.canUndo());
    }
    void independentColdWarmProvidersKeepPointerRadius_data() {
        QTest::addColumn<bool>("mobile"); QTest::addColumn<QString>("pointer"); QTest::addColumn<double>("latitude");
        for (const bool mobile : {false,true}) for (const auto* pointer : {"mouse","touch",""}) for (const double latitude : {0.,59.})
            QTest::newRow(qPrintable(QString("%1-%2-lat%3").arg(mobile?"360px":"desktop",*pointer?pointer:"default").arg(latitude))) << mobile << QString(pointer) << latitude;
    }
    void independentColdWarmProvidersKeepPointerRadius() {
        QFETCH(bool,mobile); QFETCH(QString,pointer); QFETCH(double,latitude);
        Pair pair(mobile); QVERIFY(pair.open(fixture(latitude))); QVERIFY(pair.draw());
        QVERIFY(pair.both([&](EditorController& c){
            return c.resizeMapCamera(mobile?360:1100,760,3) && c.zoomMapCameraAt(100/c.mapViewState()["mapScale"].toDouble(),150,150);
        }));
        auto input = pair.at({0,latitude+5}); input.x -= 14;
        VERIFY_CALL(pair,Operation::Hover,input,0,pointer,false);
        QCOMPARE(pair.screen.geometrySnapState()["submitted"].toULongLong(),qulonglong(0));
        const auto canonical = pair.screen.documentBytes();
        VERIFY_CALL(pair,Operation::Add,input,18,pointer,true);
        QVERIFY(!std::isfinite(pair.screen.geometryEditState()["snapX"].toDouble()));
        const auto coldPaths = pair.screen.geometryDraftPaths();
        QTRY_VERIFY_WITH_TIMEOUT(pair.legacy.geometrySnapState()["status"].toString()=="ready" && pair.screen.geometrySnapState()["status"].toString()=="ready",5000);
        QVERIFY(equivalent(pair)); QCOMPARE(pair.screen.geometryDraftPaths(),coldPaths);
        const bool touch = pointer=="touch" || (pointer.isEmpty() && mobile);
        VERIFY_CALL(pair,Operation::Hover,input,0,pointer,true);
        QCOMPARE(!pair.screen.geometrySnapState()["indicator"].toMap().isEmpty(),touch);
        VERIFY_CALL(pair,Operation::Add,input,18,pointer,true);
        QCOMPARE(!pair.screen.geometrySnapState()["indicator"].toMap().isEmpty(),touch);
        if (touch) QCOMPARE(pair.screen.geometrySnapState()["indicator"].toMap()["kind"].toString(),QString("edge"));
        const auto paths = pair.screen.geometryDraftPaths();
        QVERIFY(changeCamera(pair,1)); QCOMPARE(pair.screen.geometryDraftPaths(),paths);
        input = pair.at({0,latitude+5}); input.x -= 14;
        VERIFY_CALL(pair,Operation::Hover,input,0,"touch",true);
        QTRY_VERIFY_WITH_TIMEOUT(pair.legacy.geometrySnapState()["status"].toString()=="ready" && pair.screen.geometrySnapState()["status"].toString()=="ready",5000);
        VERIFY_CALL(pair,Operation::Hover,input,0,"touch",true);
        QVERIFY(!pair.screen.geometrySnapState()["indicator"].toMap().isEmpty());
        VERIFY_CALL(pair,Operation::Hover,input,0,"mouse",true);
        QVERIFY(pair.screen.geometrySnapState()["indicator"].toMap().isEmpty());
        QCOMPARE(pair.screen.documentBytes(),canonical);
        pair.legacy.cancelGeometryEdit(); pair.screen.cancelGeometryEdit(); QVERIFY(equivalent(pair));
        QCOMPARE(pair.screen.geometrySnapState()["status"].toString(),QString("empty"));
        const auto submitted = pair.screen.geometrySnapState()["submitted"].toULongLong();
        QVERIFY(pair.draw());
        VERIFY_CALL(pair,Operation::Hover,input,0,"touch",false);
        QCOMPARE(pair.screen.geometrySnapState()["submitted"].toULongLong(),submitted);
    }
    void objectTranslationRejectsAtomicallyAndPreservesDifferentHistoryPolicy() {
        Pair pair; QVERIFY(pair.open()); QVERIFY(pair.edit("hydro"));
        const auto canonical = pair.screen.documentBytes(); const auto original = pair.screen.geometryDraftPaths();
        QVERIFY(pair.both([](EditorController& c){return c.geometrySetMoveMode(true) && c.geometryBeginObjectDrag();}));
        VERIFY_CALL(pair,Operation::Translate,(Point{17.125,9.875}),0,"",true);
        const auto translated = pair.screen.geometryDraftPaths(); QVERIFY(translated != original);
        const auto beforeInvalid = comparable(observation(pair.screen));
        const auto scale = pair.screen.mapViewState()["mapScale"].toDouble();
        VERIFY_CALL(pair,Operation::Translate,(Point{0,-1000*scale}),0,"",false);
        QCOMPARE(comparable(observation(pair.screen)),beforeInvalid); QVERIFY(pair.screenSignals.isEmpty());
        QVERIFY(changeCamera(pair,2));
        VERIFY_CALL(pair,Operation::Translate,(Point{0,0}),0,"",true);
        QCOMPARE(pair.screen.geometryDraftPaths(),original);
        pair.legacy.geometryEndObjectDrag(false); pair.screen.geometryEndObjectDrag(false);
        QVERIFY(equivalent(pair)); QVERIFY(!pair.screen.geometryEditState()["canUndo"].toBool());
        QVERIFY(pair.both([](EditorController& c){return c.geometryBeginObjectDrag();}));
        VERIFY_CALL(pair,Operation::Translate,(Point{25.125,-12.375}),0,"",true);
        pair.legacy.geometryEndObjectDrag(true); pair.screen.geometryEndObjectDrag(true);
        QVERIFY(equivalent(pair)); QCOMPARE(pair.screen.geometryDraftPaths(),original);
        QVERIFY(!pair.screen.geometryEditState()["canUndo"].toBool());
        QVERIFY(pair.both([](EditorController& c){return c.geometryBeginObjectDrag();}));
        VERIFY_CALL(pair,Operation::Translate,(Point{25.125,-12.375}),0,"",true);
        const auto finalPaths = pair.screen.geometryDraftPaths();
        pair.legacy.geometryEndObjectDrag(false); pair.screen.geometryEndObjectDrag(false);
        QVERIFY(pair.both([](EditorController& c){return c.geometryUndoDraft();})); QVERIFY(equivalent(pair));
        QCOMPARE(pair.screen.geometryDraftPaths(),original);
        QVERIFY(pair.both([](EditorController& c){return c.geometryRedoDraft();})); QVERIFY(equivalent(pair));
        QCOMPARE(pair.screen.geometryDraftPaths(),finalPaths);
        QVERIFY(pair.both([](EditorController& c){return c.requestGeometryPreview() && c.confirmGeometryEdit();}));
        QVERIFY(equivalent(pair)); const auto committed = pair.screen.documentBytes(); QVERIFY(committed != canonical);
        pair.legacy.undo(); pair.screen.undo(); QVERIFY(equivalent(pair)); QCOMPARE(pair.screen.documentBytes(),canonical);
        pair.legacy.redo(); pair.screen.redo(); QVERIFY(equivalent(pair)); QCOMPARE(pair.screen.documentBytes(),committed);
    }
    void boundaryInvalidLatitudeIsAcceptedUntilReleaseAndRetry_data() {
        QTest::addColumn<bool>("cancel"); QTest::newRow("release-rejects") << false; QTest::newRow("cancel-restores") << true;
    }
    void boundaryInvalidLatitudeIsAcceptedUntilReleaseAndRetry() {
        QFETCH(bool,cancel); Pair pair; QVERIFY(pair.open(boundaryFixture()));
        const auto canonical = pair.screen.documentBytes();
        QVERIFY(pair.both([](EditorController& c){
            bool first = true;
            for (const auto* id : {"A","B","C"}) { if (!c.selectObject({{"domain","territorial"},{"id",id}},first?"replace":"toggle")) return false; first = false; }
            return c.beginSharedBoundaryGeometry();
        }));
        QTRY_VERIFY_WITH_TIMEOUT(pair.legacy.geometryEditState()["boundaryStatus"].toString()=="ready" && pair.screen.geometryEditState()["boundaryStatus"].toString()=="ready",5000);
        QVERIFY(equivalent(pair)); const auto original = pair.screen.geometryDraftPaths();
        VERIFY_CALL(pair,Operation::Select,pair.at({1,1}),1,"",true);
        QVERIFY(pair.both([](EditorController& c){return c.geometryBeginVertexDrag();}));
        QVERIFY(changeCamera(pair,1));
        VERIFY_CALL(pair,Operation::Move,pair.at({1,100}),0,"touch",true);
        QCOMPARE(pair.screen.documentBytes(),canonical);
        QCOMPARE(pair.screen.geometryEditState()["boundaryStatus"].toString(),QString("ready"));
        QVERIFY(!pair.screen.geometryEditState()["previewReady"].toBool());
        pair.legacy.geometryEndVertexDrag(cancel); pair.screen.geometryEndVertexDrag(cancel); QVERIFY(equivalent(pair));
        if (cancel) {
            QCOMPARE(pair.screen.geometryDraftPaths(),original);
            QCOMPARE(pair.screen.geometryEditState()["boundaryStatus"].toString(),QString("ready"));
        } else {
            QCOMPARE(pair.screen.geometryEditState()["boundaryStatus"].toString(),QString("error"));
            QVERIFY(!pair.screen.geometryEditState()["previewReady"].toBool());
            QVERIFY(pair.both([](EditorController& c){return c.geometryRetryBoundaryPreparation();}));
            QTRY_VERIFY_WITH_TIMEOUT(pair.legacy.geometryEditState()["boundaryStatus"].toString()=="ready" && pair.screen.geometryEditState()["boundaryStatus"].toString()=="ready",5000);
        }
        QCOMPARE(pair.screen.documentBytes(),canonical); QVERIFY(!pair.screen.canUndo());
        VERIFY_CALL(pair,Operation::Select,pair.at({1,1}),1,"",true);
        QVERIFY(pair.both([](EditorController& c){return c.geometryBeginVertexDrag();}));
        pair.legacy.geometryEndVertexDrag(false); pair.screen.geometryEndVertexDrag(false); QVERIFY(equivalent(pair));
        QVERIFY(!pair.screen.geometryEditState()["canUndo"].toBool());
        VERIFY_CALL(pair,Operation::Select,pair.at({1,1}),1,"",true);
        QVERIFY(pair.both([](EditorController& c){return c.geometryBeginVertexDrag();}));
        VERIFY_CALL(pair,Operation::Move,pair.at({1,1.1}),0,"mouse",true);
        pair.legacy.geometryEndVertexDrag(true); pair.screen.geometryEndVertexDrag(true);
        QVERIFY(equivalent(pair)); QCOMPARE(pair.screen.geometryDraftPaths(),original);
    }
    void territoryPickUsesCandidateAndComponentState_data() {
        QTest::addColumn<QString>("method");
        QTest::newRow("polygon-candidates") << QString("polygon");
        QTest::newRow("source-components") << QString("components");
    }
    void territoryPickUsesCandidateAndComponentState() {
        QFETCH(QString,method); Pair pair; auto document = fixture();
        if (method=="components") {
            // Annex uses fixed existing IDs. A split receipt deliberately owns
            // a fresh UUID per session, so it is not an identical-pair fixture.
            document.geometries.insert({"donor",1},box(20,0,10,10));
            appendTerritory(document,{"donor","Donor","",UnitKind::General,false},{"donor",1});
            document.presentation.objectStyles[territorialRef("donor")] = {};
        }
        QVERIFY(pair.open(document)); const auto canonical = pair.screen.documentBytes();
        const Point pick = method=="components" ? Point{22,2} : Point{2,2};
        QVERIFY(pair.both([&](EditorController& c){
            c.selectCountry("A");
            const bool started = method=="components" ? c.beginAnnexGeometry() && c.geometryToggleProvider({{"domain","territorial"},{"id","donor"}}) : c.beginSplitGeometry();
            return started && c.geometryAdvanceStage() && c.geometrySelectTerritoryMethod(method);
        }));
        // Immediate input remains subject to legacy pending-calculation guards.
        if (method=="polygon") {
            QVERIFY(pair.screen.geometryEditState()["selectionPending"].toBool());
            VERIFY_CALL(pair,Operation::Add,pair.at({2,2}),0,"mouse",false);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!pair.legacy.geometryEditState()["calculating"].toBool() && !pair.screen.geometryEditState()["calculating"].toBool(),10000);
        if (method=="polygon") {
            for (const auto point : {Point{1,1},Point{4,1},Point{4,4},Point{1,4}})
                VERIFY_CALL(pair,Operation::Add,pair.at(point),0,"mouse",true);
            QVERIFY(pair.both([](EditorController& c){return c.geometryFinishTerritoryDraft();}));
            QTRY_VERIFY_WITH_TIMEOUT(!pair.legacy.geometryEditState()["calculating"].toBool() && !pair.screen.geometryEditState()["calculating"].toBool(),15000);
            QCOMPARE(pair.screen.geometryEditState()["candidates"].toList().size(),1);
        } else QCOMPARE(pair.screen.geometryEditState()["components"].toList().size(),1);
        QVERIFY(equivalent(pair));
        const QString selectedKey = method=="polygon" ? "selectedCandidateIds" : "selectedComponentKeys";
        const auto before = pair.screen.geometryEditState()[selectedKey].toList();
        QVERIFY(changeCamera(pair,1));
        VERIFY_CALL(pair,Operation::Pick,pair.at(pick),0,"",true);
        QTRY_VERIFY_WITH_TIMEOUT(!pair.legacy.geometryEditState()["calculating"].toBool() && !pair.screen.geometryEditState()["calculating"].toBool(),15000);
        QVERIFY(equivalent(pair)); QVERIFY(pair.screen.geometryEditState()[selectedKey].toList()!=before);
        const auto actualSelection = comparable(pair.screen.riverSelectionObservation());
        const auto expectedSelection = comparable(pair.legacy.riverSelectionObservation());
        QVERIFY2(actualSelection == expectedSelection,qPrintable(firstDifference(actualSelection,expectedSelection)));
        QVERIFY(changeCamera(pair,2));
        VERIFY_CALL(pair,Operation::Pick,pair.at(pick),0,"",true);
        QTRY_VERIFY_WITH_TIMEOUT(!pair.legacy.geometryEditState()["calculating"].toBool() && !pair.screen.geometryEditState()["calculating"].toBool(),15000);
        QVERIFY(equivalent(pair)); QCOMPARE(pair.screen.geometryEditState()[selectedKey].toList(),before);
        VERIFY_CALL(pair,Operation::Pick,pair.at({-50,-50}),0,"",false);
        VERIFY_CALL(pair,Operation::Pick,(Point{std::numeric_limits<double>::quiet_NaN(),0}),0,"",false);
        QCOMPARE(pair.screen.documentBytes(),canonical); QVERIFY(!pair.screen.canUndo());
        pair.legacy.cancelGeometryEdit(); pair.screen.cancelGeometryEdit(); QVERIFY(equivalent(pair));
    }
};
QTEST_MAIN(EditScreenControllerTests)
#include "edit_screen_controller_tests.moc"
