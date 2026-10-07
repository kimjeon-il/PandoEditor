#include "editorcontroller.h"
#include <pandoeditor/maprenderorder.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <QCollator>
#include <QLocale>

using namespace pandoeditor;

MapPickContext EditorController::mapPickContext(double zoom) const {
    MapPickContext context;
    context.mobile=mobileMode_;
    context.zoom=zoom;
    context.primary=selection_.primary();

    context.placedLabels=&labelEngine_.placedRefs();

    if(const auto frame=hydroRuntime_.frame()) {
        context.externalHydro.reserve(frame->features.size());
        for(const auto& feature:frame->features) {
            const auto record=hydroRuntime_.recordByFid(feature.fid);
            if(!record)continue;
            MapExternalHydroPickFeature pick;
            pick.ref={"hydroBuiltin",record->awId.toStdString()};
            pick.category=record->category.toStdString();
            if(record->bounds.size()==4) {
                pick.bounds={record->bounds[0],record->bounds[1],
                             record->bounds[2],record->bounds[3]};
            } else {
                pick.bounds={feature.bounds[0]*1e-6,feature.bounds[1]*1e-6,
                             feature.bounds[2]*1e-6,feature.bounds[3]*1e-6};
            }
            pick.feature=&feature;
            context.externalHydro.push_back(std::move(pick));
        }
    }
    return context;
}

std::vector<ObjectRef> EditorController::sortMapCandidates(std::vector<ObjectRef> found) const {
    QCollator names{QLocale{QLocale::Korean}};
    const auto snapshot=project_.snapshot();
    std::stable_sort(found.begin(),found.end(),[&](const ObjectRef& a,const ObjectRef& b) {
        const int leftRank=a.domain=="placeBuiltin"?mapPickOrder(project_.document(),{"label",a.id}):mapPicker_.candidateRank(snapshot,a);
        const int rightRank=b.domain=="placeBuiltin"?mapPickOrder(project_.document(),{"label",b.id}):mapPicker_.candidateRank(snapshot,b);
        if(leftRank!=rightRank)return leftRank>rightRank;
        const auto title=[&](const ObjectRef& ref) {
            if(ref.domain=="placeBuiltin")if(const auto record=placeRuntime_.recordById(QString::fromStdString(ref.id)))return record->name;
            if(ref.domain=="hydroBuiltin") {
                const auto record=hydroRuntime_.recordById(QString::fromStdString(ref.id));
                return record?record->name:QString::fromStdString(ref.id);
            }
            const auto property=project_.propertyView(ref);
            return property?QString::fromStdString(property->displayName):
                QString::fromStdString(ref.id);
        };
        return names.compare(title(a),title(b))<0;
    });
    return found;
}

std::vector<ObjectRef> EditorController::mapCandidates(
    double x,double y,double pixelsPerUnit,double zoom) const {
    auto found=mapPicker_.pickMap(project_.snapshot(),mapCameraMetrics(),{x,y,pixelsPerUnit},mapPickContext(zoom));
    if(pixelsPerUnit>0)for(const auto& label:labelEngine_.placements())if(label.ref.domain=="placeBuiltin"&&objectVisible(label.ref)){
        const auto point=projection_.project(label.geographic);
        if(std::abs(x-point.x)*pixelsPerUnit<=label.width/2&&std::abs(y-point.y)*pixelsPerUnit<=label.height/2)found.push_back(label.ref);
    }
    return sortMapCandidates(std::move(found));
}

std::vector<ObjectRef> EditorController::mapCandidatesScreen(
    double x,double y,double zoom) const {
    const auto view=sceneBridge_.viewState();
    const auto metrics=mapCameraMetrics();
    auto found=mapPicker_.pickScreen(project_.snapshot(),view,metrics,{x,y},mapPickContext(zoom));
    for(const auto& label:labelEngine_.placements())if(label.ref.domain=="placeBuiltin"&&objectVisible(label.ref)&&
        std::abs(x-label.x)<=label.width/2&&std::abs(y-label.y)<=label.height/2)found.push_back(label.ref);
    return sortMapCandidates(std::move(found));
}
QVariantList EditorController::objectChooserCandidates() const {
    QVariantList rows;
    if(!objectChooserOpen())return rows;
    const auto all=objectRows();
    for(const auto& ref:chooserRefs_){
        auto row=objectRefValue(ref);
        for(const auto& value:all)if(value.toMap()["id"]==row["id"] && value.toMap()["domain"]==row["domain"]){row=value.toMap();break;}
        rows.append(row);
    }
    return rows;
}
void EditorController::closeObjectChooser(){
    if(!chooserBase_&&chooserRefs_.empty())return;
    chooserRefs_.clear();chooserBase_.reset();chooserToggle_=false;emit objectChooserChanged();
}
void EditorController::beginMapSelection(double x,double y,bool additive,double pixelsPerUnit,double zoom){
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(pixelsPerUnit)||pixelsPerUnit<0)return;
    beginMapSelectionCandidates(mapCandidates(x,y,pixelsPerUnit,zoom),additive);
}
void EditorController::beginMapSelectionScreen(double x,double y,bool additive,double zoom){
    if(!std::isfinite(x)||!std::isfinite(y))return;
    beginMapSelectionCandidates(mapCandidatesScreen(x,y,zoom),additive);
}
void EditorController::beginMapSelectionCandidates(std::vector<ObjectRef> refs,bool additive){
    refs=mapPicker_.normalizeSelectionCandidates(project_.snapshot(),std::move(refs));
    closeObjectChooser();
    if(refs.empty()){clearSelection();return;} // web background clears even with Ctrl
    if(refs.size()==1){selectObject(objectRefValue(refs.front()),additive?"toggle":"replace","map");return;}
    chooserBase_=project_.snapshot();chooserRefs_=std::move(refs);chooserToggle_=additive;
    emit objectChooserChanged();
}
bool EditorController::chooseMapCandidate(int index,bool toggle){
    if(!objectChooserOpen()||index<0||static_cast<std::size_t>(index)>=chooserRefs_.size())return false;
    if(!chooserBase_->matches(project_)){closeObjectChooser();return false;}
    const auto ref=chooserRefs_.at(static_cast<std::size_t>(index));
    if(!objectVisible(ref)){closeObjectChooser();return false;}
    const bool additive=toggle||chooserToggle_;
    const auto value=objectRefValue(ref);
    closeObjectChooser();
    return selectObject(value,additive?"toggle":"replace","map");
}
