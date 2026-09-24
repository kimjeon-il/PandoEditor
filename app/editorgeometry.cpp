#include "editorcontroller.h"
#include "retainedreferencerewriter.h"
#include "territorialgeometry.h"
#include "geometrycalculator.h"
#include <pandoeditor/commands.h>
#include <pandoeditor/geometrypredicates.h>
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
double distanceSquared(Point a,Point b) { const auto x=a.x-b.x,y=a.y-b.y;return x*x+y*y; }
double segmentDistanceSquared(Point point,Point a,Point b,double& t) {
    const auto dx=b.x-a.x,dy=b.y-a.y,denominator=dx*dx+dy*dy;
    t=denominator?std::clamp(((point.x-a.x)*dx+(point.y-a.y)*dy)/denominator,0.,1.):0.;
    return distanceSquared(point,{a.x+dx*t,a.y+dy*t});
}
Point snappedPoint(const ProjectDocument& document,const MapProjection& projection,Point input,double tolerance,std::optional<Point>& marker) {
    marker.reset();if(tolerance<=0)return input;double best=tolerance*tolerance;Point result=input;
    // Vertices have priority over edges, matching the web editor's stable snap.
    for(const auto& unit:document.units)if(const auto geometry=document.geometries.get(unit.geometry))for(const auto& polygon:geometry->polygons)for(const auto& ring:polygon)for(const auto& candidate:ring){const auto view=projection.project(candidate);const auto inputView=projection.project(input);const auto distance=distanceSquared(view,inputView);if(distance<=best){best=distance;result=candidate;marker=candidate;}}
    if(marker)return result;
    const auto inputView=projection.project(input);for(const auto& unit:document.units)if(const auto geometry=document.geometries.get(unit.geometry))for(const auto& polygon:geometry->polygons)for(const auto& ring:polygon)for(std::size_t i=1;i<ring.size();++i){double t=0;const auto distance=segmentDistanceSquared(inputView,projection.project(ring[i-1]),projection.project(ring[i]),t);if(distance<=best){best=distance;result={ring[i-1].x+(ring[i].x-ring[i-1].x)*t,ring[i-1].y+(ring[i].y-ring[i-1].y)*t};marker=result;}}
    return result;
}
double orientation(Point a,Point b,Point c) { return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x); }
bool properIntersection(Point a,Point b,Point c,Point d) {
    const auto ab1=orientation(a,b,c),ab2=orientation(a,b,d),cd1=orientation(c,d,a),cd2=orientation(c,d,b);
    return (ab1>0&&ab2<0||ab1<0&&ab2>0)&&(cd1>0&&cd2<0||cd1<0&&cd2>0);
}
bool selfIntersects(const Ring& ring) {
    if(ring.size()<4)return false;const auto edgeCount=ring.size()-1;
    for(std::size_t a=0;a<edgeCount;++a)for(std::size_t b=a+1;b<edgeCount;++b) {
        if(b==a+1||(a==0&&b+1==edgeCount))continue;
        if(properIntersection(ring[a],ring[a+1],ring[b],ring[b+1]))return true;
    }
    return false;
}
bool validGeometry(const Geometry& geometry,std::string* detail=nullptr) {
    try {
        GeometryStore check;check.insert({"geometry-edit",1},geometry);
        for(const auto& polygon:geometry.polygons) {
            if(polygon.empty()||selfIntersects(polygon.front()))throw std::invalid_argument("INVALID_GEOMETRY: self intersection");
            Geometry outer{"Polygon",{}, {}, {{polygon.front()}}};
            for(std::size_t hole=1;hole<polygon.size();++hole) {
                if(selfIntersects(polygon[hole]))throw std::invalid_argument("INVALID_GEOMETRY: self intersection");
                Geometry inner{"Polygon",{}, {}, {{polygon[hole]}}};
                if(!geometryContains(outer,inner))throw std::invalid_argument("INVALID_GEOMETRY: hole outside exterior");
            }
        }
        return true;
    }
    catch(const std::exception& error) { if(detail)*detail=error.what();return false; }
}
}

QVariantMap EditorController::geometryEditState() const
{
    if(!geometryEdit_) return {{"active",false}};
    const auto& edit=*geometryEdit_;
    return {{"active",true},{"tool",edit.tool},{"phase",edit.preview?"preview":"editing"},{"previewReady",bool(edit.preview)},
        {"selectedVertex",edit.vertex},{"error",edit.error},{"canUndo",edit.tool=="split"?!edit.lineDraft.empty():!edit.undo.empty()},{"canRedo",!edit.redo.empty()},
        {"target",objectRefValue(edit.target)},
        {"snapX",edit.snapPoint?projection_.project(*edit.snapPoint).x:std::numeric_limits<double>::quiet_NaN()},
        {"snapY",edit.snapPoint?projection_.project(*edit.snapPoint).y:std::numeric_limits<double>::quiet_NaN()}};
}

QVariantList EditorController::geometryDraftPaths() const
{
    QVariantList paths;if(!geometryEdit_)return paths;
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
    const auto geometry=project_.document().geometries.get(unit->geometry);if(!geometry)return false;
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

bool EditorController::geometryAddPoint(double x,double y,double tolerance)
{
    if(!geometryEdit_||geometryEdit_->preview||!std::isfinite(x)||!std::isfinite(y))return false;
    auto& edit=*geometryEdit_;if(edit.tool=="split"){edit.lineDraft.push_back(snappedPoint(project_.document(),projection_,projection_.unproject(x,y),tolerance,edit.snapPoint));++edit.request;emit geometryEditChanged();return true;}if(edit.tool!="draw"&&edit.tool!="annex")return false;
    if(!isArea(edit.draft)) {
        edit.undo.push_back(edit.draft);edit.redo.clear();
        const auto point=snappedPoint(project_.document(),projection_,projection_.unproject(x,y),tolerance,edit.snapPoint);
        if(edit.draft.type=="Point")edit.draft.points={point};
        else if(edit.draft.type=="MultiPoint")edit.draft.points.push_back(point);
        else {if(edit.draft.lines.empty())edit.draft.lines.emplace_back();edit.draft.lines.front().push_back(point);}
        edit.polygon=0;edit.ring=0;edit.vertex=int(openPath(edit.draft,0)->size())-1;++edit.request;emit geometryEditChanged();return true;
    }
    if(edit.draft.polygons.empty())edit.draft.polygons.push_back({Ring{}});
    auto& ring=edit.draft.polygons.front().front();edit.undo.push_back(edit.draft);edit.redo.clear();openRing(ring);ring.push_back(snappedPoint(project_.document(),projection_,projection_.unproject(x,y),tolerance,edit.snapPoint));
    if(ring.size()>=3)closeRing(ring);edit.vertex=int(ring.size())-2;++edit.request;emit geometryEditChanged();return true;
}

bool EditorController::geometrySelectNearest(double x,double y,double tolerance)
{
    if(!geometryEdit_||geometryEdit_->preview||!std::isfinite(x)||!std::isfinite(y)||tolerance<0)return false;
    const auto point=projection_.unproject(x,y);auto& edit=*geometryEdit_;double best=tolerance*tolerance;int bestPolygon=-1,bestRing=-1,bestVertex=-1;
    if(!isArea(edit.draft)) {
        for(int p=0;auto path=openPath(edit.draft,p);++p)for(std::size_t v=0;v<path->size();++v){const auto d=distanceSquared(projection_.project((*path)[v]),{x,y});if(d<=best){best=d;bestPolygon=p;bestVertex=int(v);}}
        edit.polygon=bestPolygon;edit.ring=0;edit.vertex=bestVertex;emit geometryEditChanged();return bestVertex>=0;
    }
    for(std::size_t p=0;p<edit.draft.polygons.size();++p)for(std::size_t r=0;r<edit.draft.polygons[p].size();++r) {
        const auto& ring=edit.draft.polygons[p][r];const auto limit=ring.size()>1&&samePoint(ring.front(),ring.back())?ring.size()-1:ring.size();
        for(std::size_t v=0;v<limit;++v){const auto candidate=distanceSquared(point,ring[v]);if(candidate<=best){best=candidate;bestPolygon=int(p);bestRing=int(r);bestVertex=int(v);}}
    }
    edit.polygon=bestPolygon;edit.ring=bestRing;edit.vertex=bestVertex;emit geometryEditChanged();return bestVertex>=0;
}

bool EditorController::geometryMoveSelectedVertex(double x,double y,double tolerance)
{
    if(!geometryEdit_||geometryEdit_->preview||geometryEdit_->vertex<0||!std::isfinite(x)||!std::isfinite(y))return false;
    auto& edit=*geometryEdit_;
    if(!isArea(edit.draft)) {auto path=openPath(edit.draft,edit.polygon);if(!path||std::size_t(edit.vertex)>=path->size())return false;if(!edit.dragBefore)edit.undo.push_back(edit.draft);edit.redo.clear();(*path)[edit.vertex]=snappedPoint(project_.document(),projection_,projection_.unproject(x,y),tolerance,edit.snapPoint);++edit.request;emit geometryEditChanged();return true;}
    if(edit.polygon<0||edit.ring<0||std::size_t(edit.polygon)>=edit.draft.polygons.size()||std::size_t(edit.ring)>=edit.draft.polygons[edit.polygon].size())return false;
    auto& ring=edit.draft.polygons[edit.polygon][edit.ring];const auto limit=ring.size()>1&&samePoint(ring.front(),ring.back())?ring.size()-1:ring.size();if(std::size_t(edit.vertex)>=limit)return false;
    const bool wasClosed=ring.size()>1&&samePoint(ring.front(),ring.back());
    if(!edit.dragBefore)edit.undo.push_back(edit.draft);edit.redo.clear();ring[edit.vertex]=snappedPoint(project_.document(),projection_,projection_.unproject(x,y),tolerance,edit.snapPoint);if(wasClosed&&edit.vertex==0)ring.back()=ring.front();++edit.request;emit geometryEditChanged();return true;
}
bool EditorController::geometryBeginVertexDrag(){if(!geometryEdit_||geometryEdit_->preview||geometryEdit_->vertex<0||geometryEdit_->dragBefore)return false;geometryEdit_->dragBefore=geometryEdit_->draft;return true;}
void EditorController::geometryEndVertexDrag(bool cancel){if(!geometryEdit_||!geometryEdit_->dragBefore)return;if(cancel)geometryEdit_->draft=*geometryEdit_->dragBefore;else geometryEdit_->undo.push_back(*geometryEdit_->dragBefore);geometryEdit_->dragBefore.reset();geometryEdit_->snapPoint.reset();++geometryEdit_->request;emit geometryEditChanged();}

bool EditorController::geometryInsertNearest(double x,double y,double tolerance)
{
    if(!geometryEdit_||geometryEdit_->preview||!std::isfinite(x)||!std::isfinite(y)||tolerance<0)return false;
    auto& edit=*geometryEdit_;const auto point=projection_.unproject(x,y);double best=tolerance*tolerance;int foundP=-1,foundR=-1,foundV=-1;Point inserted;
    if(!isArea(edit.draft)) {
        if(edit.draft.type=="Point"||edit.draft.type=="MultiPoint")return false;
        for(std::size_t p=0;p<edit.draft.lines.size();++p){const auto& path=edit.draft.lines[p];for(std::size_t v=1;v<path.size();++v){double t=0;const auto d=segmentDistanceSquared({x,y},projection_.project(path[v-1]),projection_.project(path[v]),t);if(d<=best){best=d;foundP=int(p);foundV=int(v);inserted={path[v-1].x+(path[v].x-path[v-1].x)*t,path[v-1].y+(path[v].y-path[v-1].y)*t};}}}
        if(foundV<0)return false;edit.undo.push_back(edit.draft);edit.redo.clear();auto& path=edit.draft.lines[foundP];path.insert(path.begin()+foundV,inserted);edit.polygon=foundP;edit.ring=0;edit.vertex=foundV;++edit.request;emit geometryEditChanged();return true;
    }
    for(std::size_t p=0;p<edit.draft.polygons.size();++p)for(std::size_t r=0;r<edit.draft.polygons[p].size();++r){const auto& ring=edit.draft.polygons[p][r];for(std::size_t v=1;v<ring.size();++v){double t=0;const auto d=segmentDistanceSquared(point,ring[v-1],ring[v],t);if(d<=best){best=d;foundP=int(p);foundR=int(r);foundV=int(v);inserted={ring[v-1].x+(ring[v].x-ring[v-1].x)*t,ring[v-1].y+(ring[v].y-ring[v-1].y)*t};}}}
    if(foundV<0)return false;edit.undo.push_back(edit.draft);edit.redo.clear();auto& ring=edit.draft.polygons[foundP][foundR];ring.insert(ring.begin()+foundV,inserted);edit.polygon=foundP;edit.ring=foundR;edit.vertex=foundV;++edit.request;emit geometryEditChanged();return true;
}

bool EditorController::geometryDeleteSelectedVertex()
{
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
    if(!geometryEdit_||geometryEdit_->preview)return false;if(geometryEdit_->tool=="split"){if(geometryEdit_->lineDraft.empty())return false;geometryEdit_->lineDraft.pop_back();++geometryEdit_->request;emit geometryEditChanged();return true;}if(geometryEdit_->undo.empty())return false;geometryEdit_->redo.push_back(geometryEdit_->draft);geometryEdit_->draft=std::move(geometryEdit_->undo.back());geometryEdit_->undo.pop_back();geometryEdit_->vertex=-1;++geometryEdit_->request;emit geometryEditChanged();return true;
}

bool EditorController::geometryRedoDraft(){if(!geometryEdit_||geometryEdit_->preview||geometryEdit_->redo.empty()||geometryEdit_->tool=="split")return false;geometryEdit_->undo.push_back(geometryEdit_->draft);geometryEdit_->draft=std::move(geometryEdit_->redo.back());geometryEdit_->redo.pop_back();geometryEdit_->vertex=-1;++geometryEdit_->request;emit geometryEditChanged();return true;}

bool EditorController::requestGeometryPreview()
{
    std::string validationDetail;
    if(!geometryEdit_||geometryEdit_->preview||!geometryEdit_->base.matches(project_))return false;
    if(geometryEdit_->tool!="split"&&!validGeometry(geometryEdit_->draft,&validationDetail)){geometryEdit_->error=validationDetail.empty()?QStringLiteral("닫힌 유효 폴리곤이 필요합니다."):QStringLiteral("닫힌 유효 폴리곤이 필요합니다: %1").arg(QString::fromStdString(validationDetail));emit geometryEditChanged();return false;}
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
    else if(edit.annexIntent){auto annex=*edit.annexIntent;annex.selection=edit.draft;intent=std::move(annex);}
    else if(edit.splitIntent){auto split=*edit.splitIntent;split.cutLine=edit.lineDraft;intent=std::move(split);}
    else if(!edit.boundaryOwners.empty()) {
        if(edit.boundaryOwners.size()!=2){edit.error=QStringLiteral("공유 국경은 두 객체를 선택해야 합니다.");emit geometryEditChanged();return false;}
        GeometryOperationRequest unionRequest;unionRequest.operation=GeometryOperation::Union;for(const auto& owner:edit.boundaryOwners){const auto unit=edit.base.document().units.at(edit.base.index().objects.at(owner));unionRequest.operands.push_back(*edit.base.document().geometries.get(unit.geometry));}
        auto outer=calculateGeometry(unionRequest,[] {return false;});if(!outer.succeeded()){edit.error=QString::fromStdString(outer.detail);emit geometryEditChanged();return false;}const auto other=*std::find_if(edit.boundaryOwners.begin(),edit.boundaryOwners.end(),[&](const auto& owner){return !(owner==edit.target);});auto remainder=calculateGeometry({GeometryOperation::Difference,outer.geometry,edit.draft},[] {return false;});if(!remainder.succeeded()||remainder.status==GeometryOperationStatus::Empty){edit.error=QStringLiteral("반대쪽 영역이 사라집니다.");emit geometryEditChanged();return false;}intent=SharedBoundaryIntent{{{edit.target,edit.draft},{other,std::move(remainder.geometry)}}};
    }
    else if(edit.coastIntent){auto coast=*edit.coastIntent;coast.draft=edit.draft;intent=std::move(coast);}
    auto planned=CommandProcessor::planTerritorial(edit.base,intent);if(!planned.ok()||!planned.plan){commandError(planned.error,QString::fromStdString(planned.detail));return false;}
    if(edit.annexIntent||edit.splitIntent||!edit.boundaryOwners.empty()||edit.coastIntent){JobScheduler scheduler;auto ticket=scheduler.enqueue(edit.base,"geometry-edit");scheduler.takeNext();auto prepared=prepareTerritorialGeometry(edit.base,*planned.plan,ticket.token());if(!prepared.ok()||!prepared.preview){edit.error=QString::fromStdString(prepared.detail);emit geometryEditChanged();return false;}edit.preview=std::move(prepared.preview);edit.error.clear();emit geometryEditChanged();return true;}
    CommandArguments args;
    if(edit.createIntent) args.action=ApplyTerritorialMutation{*planned.plan,std::nullopt};
    else args.action=ApplyTerritorialMutation{*planned.plan,GeometryPatch{edit.base.revision(),{{edit.target,edit.draft}},{},{}}};
    CommandRequest request{command.toStdString(),edit.base.instanceId(),edit.base.document().documentId,edit.base.revision(),{edit.target},std::move(args)};
    auto prepared=CommandProcessor::prepare(edit.base,request,[](const ProjectDocument& before,const TerritorialMutationPlan& plan,std::vector<PreservedExtension>& extensions){const auto result=retainedrefs::rewrite(before,plan,extensions);return ExtensionRewriteResult{result.ok,result.detail,result.handledExtensionIds};});
    if(!prepared.ok()||!prepared.preview){edit.error=QString::fromStdString(prepared.detail);commandError(prepared.error,edit.error);emit geometryEditChanged();return false;}edit.preview=std::move(prepared.preview);edit.error.clear();emit geometryEditChanged();return true;
}

bool EditorController::confirmGeometryEdit()
{
    if(!geometryEdit_||!geometryEdit_->preview)return false;MapProjection next;try{next.rebuild(geometryEdit_->preview->change().after());}catch(...){return false;}
    auto applied=CommandProcessor::confirm(project_,*geometryEdit_->preview);if(!applied.ok()){commandError(applied.error,QString::fromStdString(applied.detail));geometryEdit_.reset();emit geometryEditChanged();return false;}
    projection_=std::move(next);const bool created=bool(geometryEdit_->createIntent);const bool content=geometryEdit_->content;geometryEdit_.reset();if(content){contentSession_.reset();emit contentEditChanged();}if(created)createDraft_.reset();hover_.reset();++hoverRevision_;closeObjectChooser();publish(false);emit geometryChanged();emit structureChanged();emit geometryEditChanged();return true;
}

void EditorController::cancelGeometryEdit()
{
    if(!geometryEdit_)return;if(geometryEdit_->preview)CommandProcessor::cancel(*geometryEdit_->preview);geometryEdit_.reset();emit geometryEditChanged();emit contentEditChanged();emit draftsChanged();emit dirtyChanged();
}
