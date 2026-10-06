#include "editorcontroller.h"
#include "geometrysnapprovider.h"
#include "retainedreferencerewriter.h"
#include "territorialgeometry.h"
#include "geometrycalculator.h"
#include <pandoeditor/commands.h>
#include <pandoeditor/map/editcoordinates.h>
#include <pandoeditor/map/editgeometry.h>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace pandoeditor;
namespace {
bool isArea(const Geometry& g) { return g.type=="Polygon"||g.type=="MultiPolygon"; }
Ring* openPath(Geometry& g,int path) {
    if(g.type=="Point"||g.type=="MultiPoint") return path==0?&g.points:nullptr;
    if(path<0||std::size_t(path)>=g.lines.size())return nullptr;
    return &g.lines[path];
}
bool samePoint(Point a,Point b) { return a.x==b.x&&a.y==b.y; }
void closeRing(Ring& ring) { if(ring.size()>1&&!samePoint(ring.front(),ring.back())) ring.push_back(ring.front()); }
void openRing(Ring& ring) { if(ring.size()>1&&samePoint(ring.front(),ring.back())) ring.pop_back(); }
}

QVariantMap EditorController::geometryEditState() const
{
    if(!geometryEdit_) return {{"active",false}};
    if(geometryEdit_->territorySelection)return territorySelectionState();
    const auto& edit=*geometryEdit_;
    auto describe=[&](const ObjectRef& ref) {
        auto row=objectRefValue(ref);QString name;
        for(const auto& unit:project_.document().units)if(territorialRef(unit.id)==ref){name=QString::fromStdString(objectDisplayName(unit));break;}
        if(name.isEmpty()&&ref==edit.target) { if(edit.createIntent)name=QString::fromStdString(edit.createIntent->name);else if(edit.content && contentSession_){name=contentEditState().value("name").toString();if(name.isEmpty()&&!contentSession_->edit.create)name=objectProperties().value("displayName").toString();}else name=objectProperties().value("displayName").toString(); }
        row["name"]=name.isEmpty()?QStringLiteral("새 객체"):name;return row;
    };
    QVariantList providers,targets,boundaryImpacts;
    if(edit.tool=="boundary"&&edit.preview)if(const auto mutation=std::get_if<ApplyTerritorialMutation>(&edit.preview->change().request().args.action))for(const auto& impact:mutation->plan.impacts) {
        if(impact.kind!="clip-child"&&impact.kind!="remove-child"&&impact.kind!="ownership-change")continue;
        QString description=impact.kind=="clip-child"?QStringLiteral("일부 영역 절단"):impact.kind=="remove-child"?QStringLiteral("객체와 참조 삭제"):QStringLiteral("상위 단위 변경");
        if(impact.kind=="ownership-change"){const auto separator=impact.messageKey.rfind(':');if(separator!=std::string::npos)description=QStringLiteral("상위 단위: ")+describe(territorialRef(impact.messageKey.substr(separator+1))).value("name").toString();}
        boundaryImpacts.append(QVariantMap{{"kind",QString::fromStdString(impact.kind)},{"id",QString::fromStdString(impact.target.id)},{"name",describe(impact.target).value("name")},{"description",description},{"messageKey",QString::fromStdString(impact.messageKey)}});
    }
    targets.append(describe(edit.target));
    for(const auto& ref:edit.boundaryOwners)if(!(ref==edit.target))targets.append(describe(ref));
    if(edit.mergeIntent)for(const auto& ref:edit.mergeIntent->donors)providers.append(describe(ref));
    if(edit.annexIntent)for(const auto& ref:edit.annexIntent->donors)providers.append(describe(ref));
    return {{"active",true},{"creating",bool(edit.createIntent)||(edit.content&&contentSession_&&contentSession_->edit.create)},{"tool",edit.tool},{"stage",edit.preview?QStringLiteral("review"):edit.stage},{"choosingProviders",edit.choosingProviders},{"providers",providers},
        {"phase",edit.preview?"preview":edit.job?"calculating":"editing"},{"previewReady",bool(edit.preview)},
        {"calculating",bool(edit.job)||edit.boundaryStatus=="preparing"},
        {"snapIndicator",edit.snapIndicator},{"boundaryStatus",edit.tool=="boundary"&&edit.boundaryStatus=="ready"&&!boundaryGeometryReady()?QStringLiteral("error"):edit.boundaryStatus},{"boundaryImpacts",boundaryImpacts},{"boundaryImpactConfirmation",edit.boundaryImpactConfirmation},{"boundaryCanPreview",boundaryGeometryReady()&&!edit.boundarySession->dragging()&&!edit.boundarySession->changedDrafts().empty()},
        {"selectedVertex",edit.vertex},{"error",edit.error},{"canUndo",edit.tool=="boundary"?boundaryGeometryReady()&&edit.boundarySession->canUndo():edit.tool=="split"?!edit.lineDraft.empty():!edit.undo.empty()},{"canRedo",edit.tool=="boundary"?boundaryGeometryReady()&&edit.boundarySession->canRedo():!edit.redo.empty()},
        {"target",objectRefValue(edit.target)},{"targets",targets},
        {"snapX",edit.snapPoint?projection_.project(*edit.snapPoint).x:std::numeric_limits<double>::quiet_NaN()},
        {"snapY",edit.snapPoint?projection_.project(*edit.snapPoint).y:std::numeric_limits<double>::quiet_NaN()}};
}

QVariantList EditorController::geometryDraftPaths() const
{
    QVariantList paths;if(!geometryEdit_)return paths;
    if(geometryEdit_->territorySelection)return territorySelectionPaths();
    if(geometryEdit_->preview) {
        const auto& document=geometryEdit_->preview->change().after();
        std::vector<ObjectRef> refs{geometryEdit_->target};
        const auto& before=geometryEdit_->preview->change().before();
        if(geometryEdit_->tool=="boundary") {
            refs.clear();const auto beforeIndex=validateDocument(before),afterIndex=validateDocument(document);
            for(const auto& ref:geometryEdit_->preview->change().impact().changedObjects)if(ref.domain=="territorial") {
                const auto previous=objectGeometry(before,beforeIndex,ref),next=objectGeometry(document,afterIndex,ref);
                if(!(previous==next))refs.push_back(ref);
            }
            if(const auto mutation=std::get_if<ApplyTerritorialMutation>(&geometryEdit_->preview->change().request().args.action))for(const auto& impact:mutation->plan.impacts)
                if((impact.kind=="clip-child"||impact.kind=="remove-child"||impact.kind=="ownership-change")&&std::find(refs.begin(),refs.end(),impact.target)==refs.end())refs.push_back(impact.target);
        }
        if(geometryEdit_->splitIntent)refs.push_back(territorialRef(geometryEdit_->splitIntent->createdId));
        const auto index=validateDocument(document);
        for(const auto& ref:refs) {
            const auto geometryRef=objectGeometry(document,index,ref);
            const bool removed=!geometryRef;
            const auto previous=removed?objectGeometry(before,validateDocument(before),ref):std::optional<GeometryRef>{};
            const auto geometry=geometryRef?document.geometries.get(*geometryRef):previous?before.geometries.get(*previous):nullptr;if(!geometry)continue;
            for(const auto& polygon:geometry->polygons) {
                QString path;
                for(const auto& ring:polygon) {
                    for(std::size_t i=0;i<ring.size();++i) {const auto point=projection_.project(ring[i]);path+=QString("%1%2 %3 ").arg(i?"L":"M").arg(point.x,0,'g',17).arg(point.y,0,'g',17);}
                    path+="Z ";
                }
                paths.append(QVariantMap{{"path",path},{"vertices",QVariantList{}},{"hole",false},{"preview",true},{"ownerId",QString::fromStdString(ref.id)},{"removed",removed},{"created",geometryEdit_->tool!="boundary"&&!(ref==geometryEdit_->target)}});
            }
        }
        return paths;
    }
    if(geometryEdit_->tool=="boundary") {
        if(!boundaryGeometryReady())return paths;
        const auto& session=*geometryEdit_->boundarySession;QVariantList vertices;
        for(std::size_t i=0;i<session.nodes().size();++i){const auto& node=session.nodes()[i];const auto point=projection_.project(node.coordinate);QStringList owners;for(const auto& owner:node.owners)owners.append(QString::fromStdString(owner));
            vertices.append(QVariantMap{{"x",point.x},{"y",point.y},{"polygon",0},{"ring",0},{"vertex",int(i)},{"ownerIds",owners},{"nodeKey",QString::fromStdString(sharedboundary::nodeKeyText(node.coordinate))},{"fixed",node.fixed},{"locked",!node.fixed&&!session.canMove(i)}});
        }
        bool first=true;for(const auto& segment:session.segments()){const auto a=projection_.project(session.nodes()[segment.start].coordinate),b=projection_.project(session.nodes()[segment.end].coordinate);paths.append(QVariantMap{{"path",QString("M%1 %2 L%3 %4").arg(a.x,0,'g',17).arg(a.y,0,'g',17).arg(b.x,0,'g',17).arg(b.y,0,'g',17)},{"vertices",first?vertices:QVariantList{}},{"hole",false},{"line",true},{"boundary",true}});first=false;}
        for(const auto& segment:session.activeSegments()){const auto a=projection_.project(segment.first),b=projection_.project(segment.second);paths.append(QVariantMap{{"path",QString("M%1 %2 L%3 %4").arg(a.x,0,'g',17).arg(a.y,0,'g',17).arg(b.x,0,'g',17).arg(b.y,0,'g',17)},{"vertices",QVariantList{}},{"hole",false},{"line",true},{"boundary",true},{"activeBoundary",true}});}
        return paths;
    }
    if(!isArea(geometryEdit_->draft)&&geometryEdit_->tool!="split") {
        const auto& g=geometryEdit_->draft;
        const auto sources=(g.type=="Point"||g.type=="MultiPoint")?std::vector<Ring>{g.points}:g.lines;
        for(std::size_t p=0;p<sources.size();++p){QString path;QVariantList vertices;
            for(std::size_t v=0;v<sources[p].size();++v){const auto point=projection_.project(sources[p][v]);path+=QString("%1%2 %3 ").arg(v?"L":"M").arg(point.x,0,'g',17).arg(point.y,0,'g',17);vertices.append(QVariantMap{{"x",point.x},{"y",point.y},{"polygon",int(p)},{"ring",0},{"vertex",int(v)}});}
            paths.append(QVariantMap{{"path",g.type=="MultiPoint"?QString{}:path},{"vertices",vertices},{"hole",false},{"line",true}});
        }return paths;
    }
    if(geometryEdit_->tool=="split") {QString path;QVariantList vertices;for(std::size_t i=0;i<geometryEdit_->lineDraft.size();++i){const auto point=projection_.project(geometryEdit_->lineDraft[i]);path+=QString("%1%2 %3 ").arg(i?"L":"M").arg(point.x,0,'g',17).arg(point.y,0,'g',17);vertices.append(QVariantMap{{"x",point.x},{"y",point.y},{"polygon",0},{"ring",0},{"vertex",int(i)}});}paths.append(QVariantMap{{"path",path},{"vertices",vertices},{"hole",false},{"line",true}});return paths;}
    for(std::size_t polygon=0;polygon<geometryEdit_->draft.polygons.size();++polygon)
        for(std::size_t ring=0;ring<geometryEdit_->draft.polygons[polygon].size();++ring) {
            const auto& source=geometryEdit_->draft.polygons[polygon][ring];QString path;QVariantList vertices;
            for(std::size_t vertex=0;vertex<source.size();++vertex) {
                const auto point=projection_.project(source[vertex]);
                path+=QString("%1%2 %3 ").arg(vertex?"L":"M").arg(point.x,0,'g',17).arg(point.y,0,'g',17);
                vertices.append(QVariantMap{{"x",point.x},{"y",point.y},{"polygon",int(polygon)},{"ring",int(ring)},{"vertex",int(vertex)}});
            }
            if(source.size()>2)path+="Z";
            paths.append(QVariantMap{{"path",path},{"vertices",vertices},{"hole",ring>0}});
        }
    return paths;
}

bool EditorController::beginGeometryEdit(const QString& tool)
{
    if(geometryEdit_||structureDialogOpen()||hasPendingEdits())return false;
    const auto target=selection_.primary();if(!target||!selectedEditable())return false;
    const auto unit=selectedUnit();if(!unit)return false;
    const auto geometry=project_.document().geometries.get(pandoeditor::staticGeometryBinding(project_.document(),unit->id).geometryRef);if(!geometry)return false;
    geometryEdit_=GeometryEditSession{project_.snapshot(),*target,*geometry,{},{},0,0,-1,tool,{}};
    emit geometryEditChanged();return true;
}

bool EditorController::beginGeometryDraw()
{
    if(geometryEdit_||(structureDialogOpen()&&!createDraft_)||hasPendingEdits())return false;
    if(createDraft_) {
        if(!createDraft_->base.matches(project_)) { cancelStructureMutation();emit errorOccurred("STALE_STRUCTURE_SESSION");return false; }
        geometryEdit_=GeometryEditSession{createDraft_->base,territorialRef(createDraft_->intent.id),{"Polygon",{}, {}, {}},{},{},0,0,-1,QStringLiteral("draw"),createDraft_->intent};
    } else {
        if(!beginGeometryEdit(QStringLiteral("draw")))return false;
        geometryEdit_->draft={"Polygon",{}, {}, {}};
    }
    emit geometryEditChanged();return true;
}

bool EditorController::geometryAddPointScreen(double x,double y,double radiusPixels,const QString& pointerType)
{
    const auto display=camera_.display();
    const auto point=map::editScreenToMap({x,y},display);
    return geometryAddPoint(point.x,point.y,map::editPixelRadiusToMap(radiusPixels,display),pointerType);
}

bool EditorController::geometryAddPoint(double x,double y,double tolerance,const QString& pointerType)
{
    if(!geometryEdit_||geometryEdit_->preview||geometryEdit_->stage=="setup"||geometryEdit_->choosingProviders||!std::isfinite(x)||!std::isfinite(y))return false;
    auto& edit=*geometryEdit_;
    if(edit.territorySelection) {
        if(edit.stage!="selection"||edit.territorySelection->state().activePhase!=TerritorySelectionPhase::Drawing||edit.applying||edit.selectionPending||edit.sourceChange||edit.territorySelection->state().methodChangeConfirmation)return false;
        if(edit.territorySelection->state().activeMethod==TerritorySelectionMethod::Line) {edit.lineDraft.push_back(snappedGeometryPoint(x,y,tolerance,pointerType));++edit.request;emit geometryEditChanged();return true;}
    }
    if(edit.tool=="split"&&!edit.territorySelection){edit.lineDraft.push_back(snappedGeometryPoint(x,y,tolerance,pointerType));++edit.request;emit geometryEditChanged();return true;}if(edit.tool!="draw"&&edit.tool!="annex"&&!edit.territorySelection)return false;
    if(!isArea(edit.draft)) {
        edit.undo.push_back(edit.draft);edit.redo.clear();
        const auto point=snappedGeometryPoint(x,y,tolerance,pointerType);
        if(edit.draft.type=="Point")edit.draft.points={point};
        else if(edit.draft.type=="MultiPoint")edit.draft.points.push_back(point);
        else {if(edit.draft.lines.empty())edit.draft.lines.emplace_back();edit.draft.lines.front().push_back(point);}
        edit.polygon=0;edit.ring=0;edit.vertex=int(openPath(edit.draft,0)->size())-1;++edit.request;emit geometryEditChanged();return true;
    }
    if(edit.draft.polygons.empty())edit.draft.polygons.push_back({Ring{}});
    auto& ring=edit.draft.polygons.front().front();edit.undo.push_back(edit.draft);edit.redo.clear();openRing(ring);ring.push_back(snappedGeometryPoint(x,y,tolerance,pointerType));
    if(ring.size()>=3)closeRing(ring);edit.vertex=int(ring.size())-2;++edit.request;emit geometryEditChanged();return true;
}

bool EditorController::geometrySelectNearestScreen(double x,double y,double radiusPixels)
{
    const auto display=camera_.display();
    const auto point=map::editScreenToMap({x,y},display);
    return geometrySelectNearest(point.x,point.y,map::editPixelRadiusToMap(radiusPixels,display));
}

bool EditorController::geometrySelectNearest(double x,double y,double tolerance)
{
    if(geometryEdit_&&geometryEdit_->territorySelection)return false;
    if(!geometryEdit_||geometryEdit_->preview||!std::isfinite(x)||!std::isfinite(y)||tolerance<0)return false;
    if(geometryEdit_->tool=="boundary") {
        if(!boundaryGeometryReady()||geometryEdit_->job)return false;
        auto& edit=*geometryEdit_;
        const auto found=map::nearestMovableBoundaryNode(*edit.boundarySession,{x,y},tolerance,mapCameraMetrics());
        edit.polygon=edit.ring=0;edit.vertex=found;emit geometryEditChanged();return found>=0;
    }
    auto& edit=*geometryEdit_;
    const auto hit=map::nearestEditVertex(edit.draft,{x,y},tolerance,mapCameraMetrics());
    edit.polygon=hit.polygon;edit.ring=hit.ring;edit.vertex=hit.vertex;emit geometryEditChanged();return hit.vertex>=0;
}

bool EditorController::geometryMoveSelectedVertexScreen(double x,double y,double radiusPixels,const QString& pointerType)
{
    const auto display=camera_.display();
    const auto point=map::editScreenToMap({x,y},display);
    return geometryMoveSelectedVertex(point.x,point.y,map::editPixelRadiusToMap(radiusPixels,display),pointerType);
}

bool EditorController::geometryMoveSelectedVertex(double x,double y,double tolerance,const QString& pointerType)
{
    if(geometryEdit_&&geometryEdit_->territorySelection)return false;
    if(!geometryEdit_||geometryEdit_->preview||geometryEdit_->vertex<0||!std::isfinite(x)||!std::isfinite(y))return false;
    auto& edit=*geometryEdit_;
    if(edit.tool=="boundary") {
        if(!boundaryGeometryReady()||edit.job)return false;
        // Resolve the pointer once, then fan the same snapped node out to all owners.
        const auto point=snappedGeometryPoint(x,y,tolerance,pointerType);
        if(!edit.boundarySession->move(std::size_t(edit.vertex),point))return false;
        if(!edit.boundarySession->dragging())edit.draft=edit.boundarySession->drafts().at(edit.target.id);
        edit.error.clear();++edit.request;emit geometryEditChanged();return true;
    }
    if(!isArea(edit.draft)) {auto path=openPath(edit.draft,edit.polygon);if(!path||std::size_t(edit.vertex)>=path->size())return false;if(!edit.dragBefore)edit.undo.push_back(edit.draft);edit.redo.clear();(*path)[edit.vertex]=snappedGeometryPoint(x,y,tolerance,pointerType);++edit.request;emit geometryEditChanged();return true;}
    if(edit.polygon<0||edit.ring<0||std::size_t(edit.polygon)>=edit.draft.polygons.size()||std::size_t(edit.ring)>=edit.draft.polygons[edit.polygon].size())return false;
    auto& ring=edit.draft.polygons[edit.polygon][edit.ring];const auto limit=ring.size()>1&&samePoint(ring.front(),ring.back())?ring.size()-1:ring.size();if(std::size_t(edit.vertex)>=limit)return false;
    const bool wasClosed=ring.size()>1&&samePoint(ring.front(),ring.back());
    if(!edit.dragBefore)edit.undo.push_back(edit.draft);edit.redo.clear();ring[edit.vertex]=snappedGeometryPoint(x,y,tolerance,pointerType);if(wasClosed&&edit.vertex==0)ring.back()=ring.front();++edit.request;emit geometryEditChanged();return true;
}
bool EditorController::geometryBeginVertexDrag(){
    if(geometryEdit_&&geometryEdit_->tool=="boundary") {
        if(!boundaryGeometryReady()||geometryEdit_->preview||geometryEdit_->job||geometryEdit_->vertex<0||!geometryEdit_->boundarySession->beginDrag(std::size_t(geometryEdit_->vertex)))return false;
        geometryEdit_->snapExcludedNodeKey=QString::fromStdString(sharedboundary::nodeKeyText(geometryEdit_->boundarySession->nodes()[geometryEdit_->vertex].coordinate));return true;
    }
    if(!geometryEdit_||geometryEdit_->tool=="move"||geometryEdit_->preview||geometryEdit_->vertex<0||geometryEdit_->dragBefore)return false;geometryEdit_->dragBefore=geometryEdit_->draft;return true;}

void EditorController::geometryEndVertexDrag(bool cancel){
    if(geometryEdit_&&geometryEdit_->tool=="boundary") {
        auto& edit=*geometryEdit_;if(!edit.boundarySession||!edit.boundarySession->dragging())return;
        const bool changed=edit.boundarySession->endDrag(cancel||!boundaryGeometryReady());edit.draft=edit.boundarySession->drafts().at(edit.target.id);
        if(!edit.boundarySession->valid()){edit.boundaryStatus="error";edit.error=QString::fromStdString(edit.boundarySession->error());}
        edit.snapPoint.reset();edit.snapIndicator.clear();edit.snapExcludedNodeKey.clear();++edit.request;emit geometryEditChanged();
        if(changed&&!requestGeometryPreview()){edit.boundarySession->resetDraft();edit.draft=edit.boundarySession->drafts().at(edit.target.id);edit.vertex=-1;emit geometryEditChanged();}return;
    }
    if(!geometryEdit_||geometryEdit_->tool=="move"||!geometryEdit_->dragBefore)return;if(cancel)geometryEdit_->draft=*geometryEdit_->dragBefore;else geometryEdit_->undo.push_back(*geometryEdit_->dragBefore);geometryEdit_->dragBefore.reset();geometryEdit_->snapPoint.reset();geometryEdit_->snapIndicator.clear();++geometryEdit_->request;emit geometryEditChanged();}
bool EditorController::geometrySetMoveMode(bool enabled){
    if(!geometryEdit_||!geometryEdit_->content||geometryEdit_->preview||geometryEdit_->dragBefore||
       (geometryEdit_->tool!="edit"&&geometryEdit_->tool!="move"))return false;
    geometryEdit_->tool=enabled?QStringLiteral("move"):QStringLiteral("edit");
    geometryEdit_->vertex=-1;emit geometryEditChanged();return true;
}
bool EditorController::geometryBeginObjectDrag(){
    if(!geometryEdit_||!geometryEdit_->content||geometryEdit_->tool!="move"||
       geometryEdit_->preview||geometryEdit_->dragBefore)return false;
    geometryEdit_->dragBefore=geometryEdit_->draft;
    geometryEdit_->objectDragMoved=false;return true;
}
bool EditorController::geometryTranslateObjectScreen(double deltaX,double deltaY){
    const auto display=camera_.display();
    const auto delta=map::editScreenDeltaToMap({deltaX,deltaY},display);
    return geometryTranslateObject(delta.x,delta.y);
}
bool EditorController::geometryTranslateObject(double dx,double dy){
    if(!geometryEdit_||geometryEdit_->tool!="move"||!geometryEdit_->dragBefore||
       !std::isfinite(dx)||!std::isfinite(dy))return false;
    auto translated=map::translateEditGeometry(*geometryEdit_->dragBefore,{dx,dy},mapCameraMetrics());
    if(!translated)return false;
    geometryEdit_->draft=std::move(translated->geometry);
    geometryEdit_->objectDragMoved=translated->moved;
    ++geometryEdit_->request;emit geometryEditChanged();return true;
}
void EditorController::geometryEndObjectDrag(bool cancel){
    if(!geometryEdit_||geometryEdit_->tool!="move"||!geometryEdit_->dragBefore)return;
    if(cancel)geometryEdit_->draft=*geometryEdit_->dragBefore;
    else if(geometryEdit_->objectDragMoved){geometryEdit_->undo.push_back(*geometryEdit_->dragBefore);geometryEdit_->redo.clear();}
    geometryEdit_->dragBefore.reset();geometryEdit_->objectDragMoved=false;
    ++geometryEdit_->request;emit geometryEditChanged();
}

bool EditorController::geometryInsertNearestScreen(double x,double y,double radiusPixels)
{
    const auto display=camera_.display();
    const auto point=map::editScreenToMap({x,y},display);
    return geometryInsertNearest(point.x,point.y,map::editPixelRadiusToMap(radiusPixels,display));
}

bool EditorController::geometryInsertNearest(double x,double y,double tolerance)
{
    if(geometryEdit_&&geometryEdit_->tool=="boundary")return false;
    if(geometryEdit_&&geometryEdit_->territorySelection)return false;
    if(!geometryEdit_||geometryEdit_->preview||!std::isfinite(x)||!std::isfinite(y)||tolerance<0)return false;
    auto& edit=*geometryEdit_;
    const auto hit=map::nearestEditSegment(edit.draft,{x,y},tolerance,mapCameraMetrics());
    if(!hit)return false;
    edit.undo.push_back(edit.draft);edit.redo.clear();
    auto& path=isArea(edit.draft)?edit.draft.polygons[hit->index.polygon][hit->index.ring]:edit.draft.lines[hit->index.polygon];
    path.insert(path.begin()+hit->index.vertex,hit->coordinate);
    edit.polygon=hit->index.polygon;edit.ring=hit->index.ring;edit.vertex=hit->index.vertex;++edit.request;emit geometryEditChanged();return true;
}

bool EditorController::geometryDeleteSelectedVertex()
{
    if(geometryEdit_&&geometryEdit_->tool=="boundary")return false;
    if(geometryEdit_&&geometryEdit_->territorySelection)return false;
    if(geometryEdit_&&!geometryEdit_->preview&&geometryEdit_->vertex>=0&&!isArea(geometryEdit_->draft)) {
        auto& edit=*geometryEdit_;auto path=openPath(edit.draft,edit.polygon);const std::size_t minimum=(edit.draft.type=="Point"||edit.draft.type=="MultiPoint")?1:2;
        if(!path||path->size()<=minimum||std::size_t(edit.vertex)>=path->size())return false;
        edit.undo.push_back(edit.draft);edit.redo.clear();path->erase(path->begin()+edit.vertex);edit.vertex=-1;++edit.request;emit geometryEditChanged();return true;
    }
    if(!geometryEdit_||geometryEdit_->preview||geometryEdit_->vertex<0)return false;auto& edit=*geometryEdit_;auto& ring=edit.draft.polygons[edit.polygon][edit.ring];const auto wasClosed=ring.size()>1&&samePoint(ring.front(),ring.back());const auto limit=wasClosed?ring.size()-1:ring.size();if(limit<=3||std::size_t(edit.vertex)>=limit)return false;
    edit.undo.push_back(edit.draft);edit.redo.clear();ring.erase(ring.begin()+edit.vertex);if(wasClosed)ring.back()=ring.front();edit.vertex=-1;++edit.request;emit geometryEditChanged();return true;
}

bool EditorController::geometryUndoDraft()
{
    if(geometryEdit_&&geometryEdit_->tool=="boundary") {auto& edit=*geometryEdit_;if(!boundaryGeometryReady()||edit.preview||edit.job||!edit.boundarySession->undo())return false;edit.draft=edit.boundarySession->drafts().at(edit.target.id);edit.vertex=-1;++edit.request;emit geometryEditChanged();return true;}
    if(geometryEdit_&&geometryEdit_->territorySelection) {
        if(geometryEdit_->applying||geometryEdit_->sourceChange||geometryEdit_->territorySelection->state().methodChangeConfirmation)return false;
        if(geometryEdit_->territorySelection->state().activePhase!=TerritorySelectionPhase::Drawing)return geometryUndoTerritoryPart();
        if(geometryEdit_->territorySelection->state().activeMethod==TerritorySelectionMethod::Line) {if(geometryEdit_->lineDraft.empty())return false;geometryEdit_->lineDraft.pop_back();++geometryEdit_->request;emit geometryEditChanged();return true;}
    }
    if(!geometryEdit_||geometryEdit_->preview)return false;if(geometryEdit_->tool=="split"&&!geometryEdit_->territorySelection){if(geometryEdit_->lineDraft.empty())return false;geometryEdit_->lineDraft.pop_back();++geometryEdit_->request;emit geometryEditChanged();return true;}if(geometryEdit_->undo.empty())return false;geometryEdit_->redo.push_back(geometryEdit_->draft);geometryEdit_->draft=std::move(geometryEdit_->undo.back());geometryEdit_->undo.pop_back();geometryEdit_->vertex=-1;++geometryEdit_->request;emit geometryEditChanged();return true;
}

bool EditorController::geometryRedoDraft(){
    if(geometryEdit_&&geometryEdit_->tool=="boundary") {auto& edit=*geometryEdit_;if(!boundaryGeometryReady()||edit.preview||edit.job||!edit.boundarySession->redo())return false;edit.draft=edit.boundarySession->drafts().at(edit.target.id);edit.vertex=-1;++edit.request;emit geometryEditChanged();return true;}
if(!geometryEdit_||geometryEdit_->preview||geometryEdit_->redo.empty()||(geometryEdit_->tool=="split"&&!geometryEdit_->territorySelection))return false;geometryEdit_->undo.push_back(geometryEdit_->draft);geometryEdit_->draft=std::move(geometryEdit_->redo.back());geometryEdit_->redo.pop_back();geometryEdit_->vertex=-1;++geometryEdit_->request;emit geometryEditChanged();return true;}

bool EditorController::requestGeometryPreview()
{
    if(geometryEdit_&&geometryEdit_->territorySelection) {
        if(geometryEdit_->applying||geometryEdit_->sourceChange||geometryEdit_->territorySelection->state().methodChangeConfirmation)return false;
        if(geometryEdit_->territorySelection->state().activePhase==TerritorySelectionPhase::Drawing)return geometryFinishTerritoryDraft();
        scheduleTerritoryPreview();return true;
    }
    if(geometryEdit_&&geometryEdit_->tool=="boundary"&&(!boundaryGeometryReady()||geometryEdit_->job||geometryEdit_->boundarySession->dragging()||geometryEdit_->boundarySession->changedDrafts().size()<2))return false;
    std::string validationDetail;
    if(!geometryEdit_||geometryEdit_->preview||!geometryEdit_->base.matches(project_))return false;
    if(geometryEdit_->stage=="setup")return false;
    if(geometryEdit_->annexIntent&&geometryEdit_->choosingProviders)return false;
    if(geometryEdit_->tool!="split"&&geometryEdit_->tool!="merge"&&geometryEdit_->tool!="boundary"&&!map::validateEditGeometry(geometryEdit_->draft,&validationDetail)){geometryEdit_->error=validationDetail.empty()?QStringLiteral("닫힌 유효 폴리곤이 필요합니다."):QStringLiteral("닫힌 유효 폴리곤이 필요합니다: %1").arg(QString::fromStdString(validationDetail));emit geometryEditChanged();return false;}
    auto& edit=*geometryEdit_;
    if(edit.content) {
        if(!contentSession_||!contentSession_->base.matches(project_))return false;
        auto command=contentSession_->edit;
        const auto previous=objectGeometry(edit.base.document(),edit.base.index(),edit.target);
        GeometryRef ref=previous?GeometryRef{previous->id,previous->version+1}:GeometryRef{"content:"+edit.target.domain+":"+edit.target.id,1};
        if(previous&&previous->version==std::numeric_limits<std::uint32_t>::max()){edit.error="도형 버전 한도 초과";emit geometryEditChanged();return false;}
        while(edit.base.document().geometries.get(ref)){if(ref.version==std::numeric_limits<std::uint32_t>::max())return false;++ref.version;}
        command.geometry=std::make_pair(ref,edit.draft);
        std::visit([&](auto& value){using T=std::decay_t<decltype(value)>;if constexpr(std::is_same_v<T,PlaceLabel>||std::is_same_v<T,HydroFeature>||std::is_same_v<T,GenericFeature>)value.geometry=ref;else if constexpr(std::is_same_v<T,DistributionEntry>){value.geometry=ref;value.territory.reset();}},command.value);
        CommandArguments args;args.action=command;
        auto request=CommandProcessor::makeRequest(project_,"content.edit",args);
        auto prepared=CommandProcessor::prepare(project_,request);
        if(!prepared.ok()||!prepared.preview){edit.error=QString::fromStdString(prepared.detail);emit geometryEditChanged();return false;}
        edit.preview=std::move(prepared.preview);edit.error.clear();emit geometryEditChanged();return true;
    }
    TerritorialMutationIntent intent=ReplaceGeometryIntent{edit.target};
    QString command=QStringLiteral("territorial.geometry.replace");
    if(edit.createIntent) { auto create=*edit.createIntent;create.geometry=edit.draft;intent=std::move(create);command=QStringLiteral("territorial.create"); }
    else if(edit.mergeIntent){intent=*edit.mergeIntent;}
    else if(edit.annexIntent){auto annex=*edit.annexIntent;annex.selection=edit.draft;intent=std::move(annex);}
    else if(edit.splitIntent){intent=*edit.splitIntent;}
    else if(edit.coastIntent){auto coast=*edit.coastIntent;coast.draft=edit.draft;intent=std::move(coast);}
    const auto schedule=[&](CommandJobRunner::Task task){
        if(edit.job)jobs_->cancel(edit.job->id());
        const auto request=edit.request;
        edit.error.clear();edit.boundaryImpactConfirmation=false;edit.boundaryImpactsApproved=false;
        edit.job=jobs_->submit(edit.base,"territorial:geometry-edit",
            std::move(task),
            [this,request](std::uint64_t id,JobDisposition disposition,PrepareResult result){
                if(!geometryEdit_||!geometryEdit_->job||geometryEdit_->job->id()!=id)return;
                auto& current=*geometryEdit_;current.job.reset();
                if(current.request!=request||disposition!=JobDisposition::Accepted||!current.base.matches(project_)||(current.tool=="boundary"&&!boundaryGeometryReady())) {
                    current.error=QStringLiteral("초안 또는 문서가 변경되어 계산 결과를 폐기했습니다.");
                } else if(!result.ok()||!result.preview) {
                    current.error=QString::fromStdString(result.detail.empty()?"GEOMETRY_PREPARATION_FAILED":result.detail);
                } else {current.preview=std::move(result.preview);current.stage="review";current.error.clear();}
                emit geometryEditChanged();
            });
        emit geometryEditChanged();return true;
    };
    if(!edit.boundaryOwners.empty()) {
        const auto drafts=edit.boundarySession->changedDrafts();
        // Preparation stays an immutable view of the canonical source. The
        // detached owner payload alone travels into the preview calculation.
        edit.boundarySession->resetDraft();edit.draft=edit.boundarySession->drafts().at(edit.target.id);edit.vertex=-1;
        for(const auto& draft:drafts)if(!map::validateEditGeometry(draft.geometry,&validationDetail)){edit.error=QString::fromStdString(validationDetail);emit geometryEditChanged();return false;}
        return schedule([drafts](const ProjectSnapshot& snapshot,const JobToken& token){
            PrepareResult failed;failed.error=CommandError::PrepareFailed;if(token.cancelled())return failed;
            auto planned=CommandProcessor::planTerritorial(snapshot,SharedBoundaryIntent{drafts});
            if(!planned.ok()||!planned.plan){failed.error=planned.error;failed.detail=planned.detail;return failed;}
            return prepareTerritorialGeometry(snapshot,*planned.plan,token);
        });
    }
    if(edit.mergeIntent||edit.annexIntent||edit.splitIntent||edit.coastIntent) {
        return schedule([intent](const ProjectSnapshot& snapshot,const JobToken& token){
            if(const auto annex=std::get_if<AnnexTerritoryIntent>(&intent))
                return prepareDrawnTerritoryAnnex(snapshot,*annex,token);
            auto planned=CommandProcessor::planTerritorial(snapshot,intent);
            if(!planned.ok()||!planned.plan){PrepareResult failed;failed.error=planned.error;failed.detail=planned.detail;return failed;}
            return prepareTerritorialGeometry(snapshot,*planned.plan,token);
        });
    }
    auto planned=CommandProcessor::planTerritorial(edit.base,intent);if(!planned.ok()||!planned.plan){commandError(planned.error,QString::fromStdString(planned.detail));return false;}
    CommandArguments args;
    if(edit.createIntent) args.action=ApplyTerritorialMutation{*planned.plan,std::nullopt};
    else args.action=ApplyTerritorialMutation{*planned.plan,GeometryPatch{edit.base.revision(),{{edit.target,edit.draft}},{},{}}};
    CommandRequest request{command.toStdString(),edit.base.instanceId(),edit.base.document().documentId,edit.base.revision(),{edit.target},std::move(args)};
    auto prepared=CommandProcessor::prepare(edit.base,request,[](const ProjectDocument& before,const TerritorialMutationPlan& plan,std::vector<PreservedExtension>& extensions){const auto result=retainedrefs::rewrite(before,plan,extensions);return ExtensionRewriteResult{result.ok,result.detail,result.handledExtensionIds};});
    if(!prepared.ok()||!prepared.preview){edit.error=QString::fromStdString(prepared.detail);commandError(prepared.error,edit.error);emit geometryEditChanged();return false;}edit.preview=std::move(prepared.preview);edit.error.clear();emit geometryEditChanged();return true;
}

bool EditorController::confirmGeometryEdit()
{
    if(geometryEdit_&&geometryEdit_->territorySelection)return applyTerritorySelection();
    if(geometryEdit_&&geometryEdit_->tool=="boundary") {
        if(!boundaryGeometryReady()||!geometryEdit_->preview)return false;
        if(!geometryEdit_->boundaryImpactsApproved) {
            const auto mutation=std::get_if<ApplyTerritorialMutation>(&geometryEdit_->preview->change().request().args.action);
            if(mutation&&std::any_of(mutation->plan.impacts.begin(),mutation->plan.impacts.end(),[](const auto& impact){return impact.kind=="clip-child"||impact.kind=="remove-child"||impact.kind=="ownership-change";})) {
                geometryEdit_->boundaryImpactConfirmation=true;emit geometryEditChanged();return false;
            }
        }
    }
    if(!geometryEdit_||!geometryEdit_->preview)return false;MapProjection next;try{next.rebuild(geometryEdit_->preview->change().after());}catch(...){return false;}
    std::optional<ObjectRef> boundarySelectedAfter;
    if(geometryEdit_->tool=="boundary") {
        if(!staticParentRelation(geometryEdit_->base.document(),geometryEdit_->target.id).parentId.empty())boundarySelectedAfter=geometryEdit_->target;
        else if(const auto mutation=std::get_if<ApplyTerritorialMutation>(&geometryEdit_->preview->change().request().args.action))if(const auto boundary=std::get_if<SharedBoundaryIntent>(&mutation->plan.intent))if(!boundary->drafts.empty())boundarySelectedAfter=boundary->drafts.front().owner;
    }
    auto applied=CommandProcessor::confirm(project_,*geometryEdit_->preview);if(!applied.ok()){commandError(applied.error,QString::fromStdString(applied.detail));geometryEdit_->preview.reset();geometryEdit_->boundaryImpactConfirmation=false;geometryEdit_->boundaryImpactsApproved=false;geometryEdit_->stage="selection";geometryEdit_->error=QString::fromStdString(applied.detail);emit geometryEditChanged();return false;}
    noteAppliedImpact(applied.impact);
    projection_=std::move(next);const bool created=bool(geometryEdit_->createIntent);const bool content=geometryEdit_->content;geometryEdit_.reset();if(content){contentSession_.reset();emit contentEditChanged();}if(created)createDraft_.reset();hover_.reset();++hoverRevision_;closeObjectChooser();
    if(boundarySelectedAfter){auto selected=selection_;selected.replace(*boundarySelectedAfter,"map");applySelection(std::move(selected));}
    publish(false);emit geometryChanged();emit structureChanged();emit geometryEditChanged();return true;
}

void EditorController::cancelGeometryEdit()
{
    // A pending boundary request stops the shared web Worker; a READY or
    // settled error cancellation does not. Rebase is deferred until a request.
    if(geometryEdit_&&geometryEdit_->tool=="boundary"&&geometryEdit_->boundaryStatus=="preparing"&&snapProvider_)
        snapProvider_->notifyWorkerStopped();
    resetGeometrySnap();
    if(!geometryEdit_)return;
    const auto boundaryInitialSelection=geometryEdit_->boundaryInitialSelection;
    std::optional<ObjectRef> boundaryChildPrimary;
    // Child entry has no root selection snapshot. cancelActiveMode reapplies
    // its current selected child as one selection; preview discard does not.
    if(geometryEdit_->tool=="boundary"&&!boundaryInitialSelection)if(const auto primary=selection_.primary();primary&&primary->domain=="territorial") {
        const auto found=project_.index().objects.find(*primary);
        if(found!=project_.index().objects.end()&&project_.document().units.at(found->second).kind==UnitKind::General&&!staticParentRelation(project_.document(),primary->id).parentId.empty())boundaryChildPrimary=*primary;
    }
    if(geometryEdit_->boundaryCancelled)geometryEdit_->boundaryCancelled->store(true);if(geometryEdit_->territorySelection)cancelTerritoryCalculation();if(geometryEdit_->job)jobs_->cancel(geometryEdit_->job->id());if(geometryEdit_->preview)CommandProcessor::cancel(*geometryEdit_->preview);geometryEdit_.reset();
    // Pinned web snapshots omit each item's key, so restore cannot resolve
    // primaryKey and replaceMany falls back to the last surviving item.
    if(boundaryInitialSelection){auto restored=selection_;restored.setMany(boundaryInitialSelection->items(),std::nullopt,"map");restored.prune([this](const ObjectRef& ref){return project_.index().objects.count(ref)>0;});applySelection(std::move(restored));}
    else if(boundaryChildPrimary){auto restored=selection_;restored.replace(*boundaryChildPrimary,"map");applySelection(std::move(restored));}
    emit geometryEditChanged();emit contentEditChanged();emit draftsChanged();emit dirtyChanged();
}

bool EditorController::geometryToggleProvider(const QVariantMap& object) {
    if(geometryEdit_&&geometryEdit_->territorySelection) {const auto ref=existingObjectRef(object);return ref&&toggleTerritorySource(*ref);}

    if(!geometryEdit_||geometryEdit_->preview||geometryEdit_->job||!geometryEdit_->choosingProviders||geometryEdit_->stage!="selection"||!geometryEdit_->base.matches(project_))return false;
    const auto ref=existingObjectRef(object);auto& edit=*geometryEdit_;
    if(!ref||ref->domain!="territorial"||*ref==edit.target||objectLocked(project_.document(),project_.index(),*ref))return false;
    const auto& target=project_.document().units.at(project_.index().objects.at(edit.target));
    const auto& provider=project_.document().units.at(project_.index().objects.at(*ref));
    if(target.kind!=provider.kind)return false;
    auto& providers=edit.mergeIntent?edit.mergeIntent->donors:edit.annexIntent->donors;
    const auto found=std::find(providers.begin(),providers.end(),*ref);
    if(found==providers.end())providers.push_back(*ref);else providers.erase(found);
    ++edit.request;edit.error.clear();emit geometryEditChanged();emit visualChanged();return true;
}
bool EditorController::geometryAdvanceStage() {
    if(geometryEdit_&&geometryEdit_->territorySelection)return advanceTerritoryStage();
    if(!geometryEdit_||geometryEdit_->preview||geometryEdit_->job)return false;
    auto& edit=*geometryEdit_;
    if(edit.stage=="setup"){edit.stage="selection";emit geometryEditChanged();return true;}
    if(edit.annexIntent&&edit.choosingProviders&&!edit.annexIntent->donors.empty()) {
        edit.choosingProviders=false;edit.error.clear();emit geometryEditChanged();return true;
    }
    return false;
}
bool EditorController::geometryBack() {
    if(geometryEdit_&&geometryEdit_->territorySelection)return backTerritoryStage();
    if(!geometryEdit_)return false;auto& edit=*geometryEdit_;
    if(edit.job){jobs_->cancel(edit.job->id());edit.job.reset();++edit.request;edit.error.clear();emit geometryEditChanged();return true;}
    if(edit.preview){CommandProcessor::cancel(*edit.preview);edit.preview.reset();edit.boundaryImpactConfirmation=false;edit.boundaryImpactsApproved=false;
        if(edit.tool=="boundary"&&edit.boundarySession){edit.boundarySession->resetDraft();edit.draft=edit.boundarySession->drafts().at(edit.target.id);edit.vertex=-1;}
        edit.stage="selection";edit.error.clear();++edit.request;emit geometryEditChanged();return true;}
    if(edit.annexIntent&&!edit.choosingProviders){edit.choosingProviders=true;emit geometryEditChanged();return true;}
    if(edit.tool=="boundary"){cancelGeometryEdit();return true;}
    if(edit.stage=="selection"){edit.stage="setup";edit.error.clear();emit geometryEditChanged();return true;}
    cancelGeometryEdit();return true;
}
