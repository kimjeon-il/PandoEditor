#include "territorial_fixture.h"
#include "editorcontroller.h"
#include "projectcodec.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <cmath>
#include <stdexcept>
using namespace pandoeditor;
namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void configureView(EditorController& controller,const QJsonObject& row) {
    require(row["view"].isObject(),"missing explicit controller view");
    const auto view=row["view"].toObject();
    const auto number=[](const QJsonValue& value) {require(value.isDouble()&&std::isfinite(value.toDouble()),"invalid controller view number");return value.toDouble();};
    const auto vector=[&](const char* key,int length) {require(view[key].isArray(),"missing controller view vector");const auto values=view[key].toArray();require(values.size()==length,"invalid controller view vector");for(const auto value:values)number(value);return values;};
    const auto translate=vector("translate",2),rotate=vector("rotate",3),center=vector("center",2);
    require(view["size"].isObject()&&view["snapDistance"].isObject()&&view["coarsePointer"].isBool(),"incomplete controller view");
    const auto size=view["size"].toObject(),snap=view["snapDistance"].toObject();
    require(number(snap["mouse"])==10&&number(snap["touch"])==18,"unsupported controller snap policy");
    require(view["coarsePointer"].toBool()==controller.mobileMode(),"controller pointer mode differs from input");
    require(controller.setProjectionMode(view["kind"].toString()),"invalid controller projection");
    require(controller.publishMapView({{"scale",number(view["scale"])},{"translateX",number(translate[0])},{"translateY",number(translate[1])},
        {"rotationLongitude",number(rotate[0])},{"rotationLatitude",number(rotate[1])},{"rotationRoll",number(rotate[2])},
        {"centerLongitude",number(center[0])},{"centerLatitude",number(center[1])},
        {"viewportWidth",number(size["width"])},{"viewportHeight",number(size["height"])}}),"controller view rejected");
}
Geometry decodeGeometry(const QJsonObject& value) {
    Geometry geometry;geometry.type=value["type"].toString().toStdString();
    require(geometry.type=="Polygon"||geometry.type=="MultiPolygon","invalid input geometry type");
    auto polygons=value["coordinates"].toArray();if(geometry.type=="Polygon")polygons=QJsonArray{polygons};
    for(const auto& p:polygons){Polygon polygon;for(const auto& r:p.toArray()){Ring ring;for(const auto& c:r.toArray()){const auto xy=c.toArray();require(xy.size()==2,"invalid coordinate");ring.push_back({xy[0].toDouble(),xy[1].toDouble()});}polygon.push_back(ring);}geometry.polygons.push_back(polygon);}return geometry;
}
QJsonObject encodeGeometry(const Geometry& geometry) {
    QJsonArray polygons;for(const auto& polygon:geometry.polygons){QJsonArray rings;for(const auto& ring:polygon){QJsonArray points;for(const auto p:ring)points.append(QJsonArray{p.x,p.y});rings.append(points);}polygons.append(rings);}return {{"type","MultiPolygon"},{"coordinates",polygons}};
}
ProjectDocument document(const QJsonObject& row) {
    ProjectDocument d;d.documentId="m972-controller-oracle";
    for(const auto& f:row["features"].toArray()) {
        const auto feature=f.toObject();const auto id=feature["id"].toString().toStdString();
        require(!id.empty(),"missing source identity");const GeometryRef ref{id,1};d.geometries.insert(ref,decodeGeometry(feature["geometry"].toObject()));
        appendTerritory(d,{id,id,"",UnitKind::General,false},ref);d.presentation.objectStyles[territorialRef(id)]={};
    }
    const auto reference=row["reference"].toString();
    if(reference=="distribution") {
        DistributionLayer layer;layer.id="distribution";layer.name="Fixture";d.distributionLayers.push_back(layer);
        DistributionEntry entry;entry.id="donor-reference";entry.layerId=layer.id;entry.territory=territorialRef("donor");entry.value=1;d.distributionEntries.push_back(entry);
    } else if(reference=="label") {
        Geometry geometry;geometry.type="Point";geometry.points={{5,0}};d.geometries.insert({"donor-label",1},geometry);
        PlaceLabel label;label.id="donor-label";label.name="Donor label";label.geometry={"donor-label",1};label.territory=territorialRef("donor");d.labels.push_back(label);
    } else if(reference=="label-settings")d.presentation.webPresentation.labelSettings[territorialRef("donor")]={};
    validateDocument(d);return d;
}
void settle(EditorController& controller) {
    QElapsedTimer timeout;timeout.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents,10);
        if(!controller.geometryEditState().value("calculating").toBool())return;
        QThread::msleep(2);
    }while(timeout.elapsed()<10000);
    throw std::runtime_error("actual EditorController timer/job did not settle");
}
// Observe the production SVG overlays rather than accessing private selection or
// evaluating a second selection model. The fixtures are equator-symmetric so
// the real flat projection round-trips exactly; geometry is never rounded.
QJsonArray overlays(EditorController& controller,const MapProjection& projection) {
    QJsonArray result;static const QRegularExpression token("([MLZ])|(-?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?)");
    for(const auto& value:controller.geometryDraftPaths()) {
        const auto item=value.toMap();const auto kind=item.value("selectionKind").toString();
        if(kind.isEmpty()||kind=="draft")continue;
        Polygon polygon;Ring ring;auto matches=token.globalMatch(item.value("path").toString());
        while(matches.hasNext()) {
            const auto match=matches.next();const auto command=match.captured(1);
            if(command=="M"){require(ring.empty(),"overlay ring did not close");continue;}
            if(command=="L")continue;
            if(command=="Z"){require(ring.size()>=4,"overlay ring too short");polygon.push_back(ring);ring.clear();continue;}
            require(matches.hasNext(),"overlay coordinate missing y");const auto y=matches.next();require(y.captured(1).isEmpty(),"overlay coordinate malformed");
            ring.push_back(projection.unproject(match.captured(2).toDouble(),y.captured(2).toDouble()));
        }
        require(ring.empty()&&!polygon.empty(),"overlay polygon malformed");Geometry geometry;geometry.polygons.push_back(polygon);
        result.append(QJsonObject{{"kind",kind},{"id",item.value("id").toString()},{"geometry",encodeGeometry(geometry)}});
    }
    return result;
}
QJsonObject snapshot(EditorController& controller,const QByteArray& before,const MapProjection& projection) {
    const auto bytes=controller.documentBytes();const auto d=projectcodec::decode(bytes);QJsonArray features,references;
    for(const auto& unit:d.units)features.append(QJsonObject{{"id",QString::fromStdString(unit.id)},{"geometry",encodeGeometry(*d.geometries.get(staticGeometryBinding(d,unit.id).geometryRef))},{"properties",QJsonObject{{"parentId",QString::fromStdString(staticParentRelation(d,unit.id).parentId)}}}});
    for(const auto& entry:d.distributionEntries)if(entry.territory)references.append(QJsonObject{{"kind","distribution"},{"id",QString::fromStdString(entry.id)},{"target",QString::fromStdString(entry.territory->id)}});
    for(const auto& label:d.labels)if(label.territory)references.append(QJsonObject{{"kind","label"},{"id",QString::fromStdString(label.id)},{"target",QString::fromStdString(label.territory->id)}});
    for(const auto& [ref,settings]:d.presentation.webPresentation.labelSettings)references.append(QJsonObject{{"kind","label-settings"},{"id",QString::fromStdString(ref.id)},{"target",QString::fromStdString(ref.id)}});
    return {{"state",QJsonObject::fromVariantMap(controller.geometryEditState())},{"mapViewState",QJsonObject::fromVariantMap(controller.mapViewState())},{"coarsePointer",controller.mobileMode()},{"overlays",overlays(controller,projection)},{"features",features},{"references",references},{"history",QJsonObject{{"canUndo",controller.canUndo()},{"canRedo",controller.canRedo()}}},{"unchangedFromBefore",bytes==before}};
}
QJsonObject run(const QJsonObject& row) {
    QTemporaryDir dir;require(dir.isValid(),"fixture directory failed");Project p;p.replace(document(row));MapProjection projection;projection.rebuild(p.document());
    QFile file(dir.filePath("input.json"));require(file.open(QIODevice::WriteOnly),"fixture write failed");file.write(projectcodec::encode(p));file.close();
    EditorController controller({false,dir.filePath("private.json")});require(controller.openFile(QUrl::fromLocalFile(file.fileName())),"controller fixture open failed");controller.selectCountry("target");
    configureView(controller,row);
    const auto before=controller.documentBytes();QJsonArray observations,events;
    QObject::connect(&controller,&EditorController::geometryEditChanged,&controller,[&]{const auto s=controller.geometryEditState();events.append(QJsonObject{{"selectionPending",s.value("selectionPending").toBool()},{"previewPending",s.value("previewPending").toBool()},{"applying",s.value("applying").toBool()},{"active",s.value("active").toBool()}});});
    for(const auto& v:row["actions"].toArray()) {
        const auto action=v.toObject();const auto op=action["op"].toString();bool accepted=true;QJsonValue outcome=QJsonValue::Null;
        if(op=="begin") {accepted=controller.beginAnnexGeometry();for(const auto& donor:row["sourceIds"].toArray())accepted=controller.geometryToggleProvider({{"domain","territorial"},{"id",donor.toString()}})&&accepted;}
        else if(op=="advance")accepted=controller.geometryAdvanceStage();
        else if(op=="method")accepted=controller.geometrySelectTerritoryMethod(action["method"].toString());
        else if(op=="draft")for(const auto& v:action["points"].toArray()){const auto point=v.toArray();const auto xy=projection.project({point[0].toDouble(),point[1].toDouble()});accepted=controller.geometryAddPoint(xy.x,xy.y,0)&&accepted;}
        else if(op=="finish")accepted=controller.geometryFinishTerritoryDraft();
        else if(op=="candidate") {const auto candidates=controller.geometryEditState().value("candidates").toList();const int index=action["index"].toInt(-1);require(index>=0&&index<candidates.size(),"candidate index not observed");accepted=controller.geometryToggleTerritoryCandidate(candidates[index].toMap().value("id").toString());}
        else if(op=="component")accepted=controller.geometryToggleTerritoryComponent(action["key"].toString());
        else if(op=="archive")accepted=controller.geometryAddTerritoryPart();
        else if(op=="undoPart")accepted=controller.geometryUndoTerritoryPart();
        else if(op=="removePart") {const auto parts=controller.geometryEditState().value("parts").toList();const int index=action["index"].toInt(-1);require(index>=0&&index<parts.size(),"part index not observed");accepted=controller.geometryRemoveTerritoryPart(parts[index].toMap().value("id").toString());}
        else if(op=="back")accepted=controller.geometryBack();
        else if(op=="cancel")controller.cancelGeometryEdit();
        else if(op=="apply")accepted=controller.confirmGeometryEdit();
        else if(op=="undo"){accepted=controller.canUndo();controller.undo();}
        else if(op=="redo"){accepted=controller.canRedo();controller.redo();}
        else if(op!="observe")throw std::runtime_error("unknown controller action");
        if(!action["deferSettle"].toBool())settle(controller);
        if(op=="apply")outcome=accepted&&!controller.geometryEditState().value("active").toBool();
        if(action.contains("name")){auto observation=snapshot(controller,before,projection);observation["name"]=action["name"];observation["accepted"]=accepted;observation["outcome"]=outcome;observations.append(observation);}
    }
    controller.cancelGeometryEdit();return {{"case",row["case"]},{"observations",observations},{"events",events}};
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        QFile input;require(input.open(stdin,QIODevice::ReadOnly),"stdin unavailable");QJsonParseError error;const auto doc=QJsonDocument::fromJson(input.readAll(),&error);require(error.error==QJsonParseError::NoError&&doc.isArray(),"invalid controller corpus");
        QJsonArray result;for(const auto& row:doc.array())result.append(run(row.toObject()));const auto bytes=QJsonDocument(result).toJson(QJsonDocument::Compact);std::fwrite(bytes.constData(),1,static_cast<std::size_t>(bytes.size()),stdout);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
