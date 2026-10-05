#include "editorcontroller.h"
#include "geometrysnapprovider.h"
#include <cmath>
#include <algorithm>
using namespace pandoeditor;
void EditorController::resetGeometrySnap() {if(snapProvider_)snapProvider_->reset();}
Point EditorController::snappedGeometryPoint(double x,double y,double tolerance,const QString& pointerType) {
    const auto raw=projection_.unproject(x,y);
    if(!geometryEdit_)return raw;
    auto& edit=*geometryEdit_;edit.snapPoint.reset();edit.snapIndicator.clear();
    if(tolerance<=0||!std::isfinite(tolerance))return raw;
    if(!snapProvider_)snapProvider_=new geometrysnap::Provider(*jobs_,this);
    const auto display=camera_.display();const auto view=camera_.view();
    geometrysnap::Request request;request.coordinate=raw;
    const double margin=geometrysnap::marginForScale(view.scale);request.margin=margin*2;
    if(!edit.boundaryOwners.empty())for(const auto& owner:edit.boundaryOwners)request.activeOwnerIds.push_back(owner.id);
    else if(edit.coastIntent)request.activeOwnerIds.push_back(edit.coastIntent->target.id);
    else if(selection_.primary())request.activeOwnerIds.push_back(selection_.primary()->id);
    if(edit.territorySelection&&edit.stage=="selection") {
        const auto& selection=edit.territorySelection->state();
        if(selection.activePhase==TerritorySelectionPhase::Drawing&&selection.activeMethod==TerritorySelectionMethod::Line&&selection.workingSourceGeometry) {
            const auto identity=QString::fromStdString(edit.base.instanceId())+":"+QString::number(edit.generation)+":"+
                QString::number(selection.derivedRevision.value_or(selection.revision));
            request.sourceGeometry=snapProvider_->retainSource(*selection.workingSourceGeometry,identity);
            request.sourceKey=snapProvider_->sourceKey();
        }
    }
    const auto& candidates=snapProvider_->candidates(project_.snapshot(),request,edit.tool,margin,edit.generation);
    const Point screen{display.originX+x*display.mapScale,display.originY+y*display.mapScale};
    // The active edit overlay uses MapProjection coordinates and the camera's
    // display transform. Rank candidates in that same visible pixel space;
    // mixing it with the geographic renderer's view gives latitude-dependent
    // distances even when the pointer is directly over a visible edit vertex.
    const auto project=[this,display](Point point)->std::optional<Point>{
        if(!std::isfinite(point.x)||!std::isfinite(point.y)||point.y < -90||point.y > 90)return {};
        const auto local=projection_.project(point);
        const Point projected{display.originX+local.x*display.mapScale,display.originY+local.y*display.mapScale};
        if(!std::isfinite(projected.x)||!std::isfinite(projected.y))return {};
        return projected;
    };
    const auto type=pointerType.isEmpty()?(mobileMode_?std::string("touch"):std::string("mouse")):pointerType.toStdString();
    const auto result=geometrysnap::resolveSnap(raw,screen,candidates,project,type,edit.snapExcludedNodeKey.toStdString());
    if(!result)return raw;
    edit.snapPoint=result->coordinate;
    QVariantList owners;for(const auto& owner:result->candidate.ownerIds)owners.append(QString::fromStdString(owner));
    QVariant endpoints;if(result->segmentEndpoints)endpoints=QVariantList{QVariantList{(*result->segmentEndpoints)[0].x,(*result->segmentEndpoints)[0].y},QVariantList{(*result->segmentEndpoints)[1].x,(*result->segmentEndpoints)[1].y}};
    edit.snapIndicator={{"kind",QString::fromStdString(result->candidate.kind)},
        {"coordinate",QVariantList{result->coordinate.x,result->coordinate.y}}, {"segmentEndpoints",endpoints},
        {"ownerIds",owners},{"nodeKey",result->candidate.nodeKey.empty()?QVariant{}:QVariant(QString::fromStdString(result->candidate.nodeKey))},
        {"segmentKey",result->candidate.segmentKey.empty()?QVariant{}:QVariant(QString::fromStdString(result->candidate.segmentKey))}};
    return result->coordinate;
}

QVariantMap EditorController::geometrySnapState() const {
    QVariantMap state{{"status",snapProvider_?snapProvider_->status():QStringLiteral("empty")},
        {"indicator",geometryEdit_?QVariant(geometryEdit_->snapIndicator):QVariant{}},
        {"submitted",qulonglong(snapProvider_?snapProvider_->submittedCount():0)}};
    if(snapProvider_){const auto& d=snapProvider_->diagnostics();state["nearbyObjects"]=qulonglong(d.nearbyObjects);state["visitedSegments"]=qulonglong(d.visitedSegments);state["intersectionTests"]=qulonglong(d.intersectionTests);state["geometryIndexBuilds"]=qulonglong(d.geometryIndexBuilds);}
    return state;
}
bool EditorController::geometryHoverSnap(double x,double y,const QString& pointerType) {
    if(!geometryEdit_||geometryEdit_->preview||geometryEdit_->choosingProviders||geometryEdit_->stage=="setup"||!std::isfinite(x)||!std::isfinite(y))return false;
    if(geometryEdit_->territorySelection&&(geometryEdit_->stage!="selection"||geometryEdit_->territorySelection->state().activePhase!=TerritorySelectionPhase::Drawing))return false;
    if(!geometryEdit_->territorySelection&&geometryEdit_->tool!="draw"&&geometryEdit_->tool!="annex"&&geometryEdit_->tool!="split")return false;
    const auto& edit=*geometryEdit_;
    const auto nonempty=[](const Ring& ring){return !ring.empty();};
    const bool hasDraftPoint=!edit.lineDraft.empty()||!edit.draft.points.empty()||
        std::any_of(edit.draft.lines.begin(),edit.draft.lines.end(),nonempty)||
        std::any_of(edit.draft.polygons.begin(),edit.draft.polygons.end(),[&](const Polygon& polygon){return std::any_of(polygon.begin(),polygon.end(),nonempty);});
    // editing-domain.setDraftHover ignores an empty draft. Hover must not make
    // the first click warm when the web's first coordinate is still cold/raw.
    if(!hasDraftPoint||edit.dragBefore)return false;
    const auto before=geometryEdit_->snapIndicator;
    snappedGeometryPoint(x,y,1,pointerType);
    if(before!=geometryEdit_->snapIndicator)emit geometryEditChanged();
    return true;
}

void EditorController::clearGeometrySnapIndicator() {
    if(!geometryEdit_||(!geometryEdit_->snapPoint&&geometryEdit_->snapIndicator.isEmpty()))return;
    geometryEdit_->snapPoint.reset();geometryEdit_->snapIndicator.clear();emit geometryEditChanged();
}
