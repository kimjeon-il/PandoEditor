#include "editorcontroller.h"
#include <pandoeditor/picking.h>
#include <pandoeditor/maprenderorder.h>
#include <QCollator>
#include <QLocale>
#include <algorithm>
#include <cmath>
#include <set>

using namespace pandoeditor;
namespace {
double segmentDistance(Point p,Point a,Point b) {
    const double dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;
    const double t=length>0?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/length,0.,1.):0.;
    return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);
}
int chooserKindRank(const Project& project,const ObjectRef& ref) {
    return ref.domain=="hydroBuiltin"?mapBuiltinHydroPickOrder():mapPickOrder(project.document(),ref);
}
}
std::vector<ObjectRef> EditorController::mapCandidates(double x,double y,double pixelsPerUnit,double zoom) const {
    std::vector<ObjectRef> found;
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(pixelsPerUnit)||pixelsPerUnit<0)return found;
    const auto point=projection_.unproject(x,y);
    const auto origin=projection_.unproject(0,0),unitX=projection_.unproject(1,0);
    const double xScale=1./(unitX.x-origin.x);
    bool countryFound=false;
    auto renderLayers=project_.layers();renderLayers.insert(renderLayers.begin(),Layer{"",""});
    for(auto layer=renderLayers.rbegin();layer!=renderLayers.rend();++layer){
        if(!layer->visible)continue;
        for(auto unit=project_.document().units.rbegin();unit!=project_.document().units.rend();++unit){
            const auto ref=territorialRef(unit->id);
            if(nativeLayerId(project_.document(),ref)!=layer->id||!objectVisible(ref))continue;
            if(unit->kind==UnitKind::Country&&countryFound)continue;
            const auto geometry=project_.document().geometries.get(unit->geometry);
            if(!geometry)continue;
            bool hit=pointInCountry(point,geometry->polygons);
            // Web subunit/region hit tolerance is 7px desktop / 12px mobile.
            // Country picking contributes only the frontmost country, as in the web picker.
            if(!hit&&unit->kind!=UnitKind::Country&&pixelsPerUnit>0){
                const double tolerance=(mobileMode_?12.:7.)/pixelsPerUnit;
                const Point cursor{point.x*xScale,point.y};
                for(const auto& polygon:geometry->polygons)for(const auto& ring:polygon)
                    for(std::size_t i=1;i<ring.size()&&!hit;++i)
                        hit=segmentDistance(cursor,{ring[i-1].x*xScale,ring[i-1].y},{ring[i].x*xScale,ring[i].y})<=tolerance;
            }
            if(hit){found.push_back(ref);if(unit->kind==UnitKind::Country)countryFound=true;}
        }
    }
    const double tolerance=(mobileMode_?18.:10.)/(pixelsPerUnit>0?pixelsPerUnit:1.);
    const Point cursor{point.x*xScale,point.y};
    std::set<ObjectRef> placedLabels;
    for(const auto& row:labelLayout(pixelsPerUnit>0?pixelsPerUnit:1,0,0,zoom,1e9,1e9))if(const auto ref=existingObjectRef(row.toMap().value("ref").toMap()))placedLabels.insert(*ref);
    std::optional<std::string> selectedDistribution;if(const auto primary=selection_.primary()) {
        if(primary->domain=="distributionLayer")selectedDistribution=primary->id;
        else if(primary->domain=="distributionEntry")for(const auto& entry:project_.document().distributionEntries)if(entry.id==primary->id){selectedDistribution=entry.layerId;break;}
    }
    const auto distributionRows=visibleDistributionEntries(project_.document(),selectedDistribution);const std::set<ObjectRef> displayedDistribution(distributionRows.begin(),distributionRows.end());
    for(const auto& [ref,index]:project_.index().objects) {
        if(ref.domain=="territorial" || !objectVisible(ref)) continue;
        if(ref.domain=="label"&&!placedLabels.count(ref))continue;
        if(ref.domain=="distributionEntry"&&!displayedDistribution.count(ref))continue;
        const auto gr=objectGeometry(project_.document(),project_.index(),ref); if(!gr) continue;
        const auto g=project_.document().geometries.get(*gr); if(!g) continue;
        bool hit=!g->polygons.empty() && pointInCountry(point,g->polygons);
        for(auto p:g->points) hit=hit||std::hypot(cursor.x-p.x*xScale,cursor.y-p.y)<=tolerance;
        for(const auto& line:g->lines) for(std::size_t i=1;i<line.size();++i)
            hit=hit||segmentDistance(cursor,{line[i-1].x*xScale,line[i-1].y},{line[i].x*xScale,line[i].y})<=tolerance;
        if(hit) found.push_back(ref);
    }
    if(const auto frame=hydroRuntime_.frame()){
        std::set<ObjectRef> seen;
        for(const auto& feature:frame->features){
            const auto record=hydroRuntime_.recordByFid(feature.fid);if(!record)continue;
            const ObjectRef ref{"hydroBuiltin",record->awId.toStdString()};
            if(seen.count(ref)||!objectVisible(ref))continue;
            if(record->bounds.size()==4 &&
                (point.x<record->bounds[0]-tolerance/xScale||point.x>record->bounds[2]+tolerance/xScale||
                 point.y<record->bounds[1]-tolerance||point.y>record->bounds[3]+tolerance))continue;
            bool hit=false;
            for(const auto& polygon:feature.geometry.polygons){
                pandoeditor::MultiPolygon rings(1);
                for(const auto& sourceRing:polygon){pandoeditor::Ring ring;ring.reserve(sourceRing.size());
                    for(const auto p:sourceRing)ring.push_back({p.longitude*1e-6,p.latitude*1e-6});
                    rings.front().push_back(std::move(ring));
                }
                if(pointInCountry(point,rings)){hit=true;break;}
            }
            for(const auto& line:feature.geometry.lines)for(std::size_t i=1;i<line.size()&&!hit;i++){
                const Point a{line[i-1].longitude*1e-6*xScale,line[i-1].latitude*1e-6};
                const Point b{line[i].longitude*1e-6*xScale,line[i].latitude*1e-6};
                hit=segmentDistance(cursor,a,b)<=tolerance;
            }
            if(hit){found.push_back(ref);seen.insert(ref);}
        }
    }
    QCollator names(QLocale(QLocale::Korean));
    std::stable_sort(found.begin(),found.end(),[&](const ObjectRef& a,const ObjectRef& b){
        // Chooser order is independent of paint order: kinds first, then names.
        const auto leftRank=chooserKindRank(project_,a);
        const auto rightRank=chooserKindRank(project_,b);
        if(leftRank!=rightRank)return leftRank>rightRank;
        const auto title=[&](const ObjectRef& ref){if(ref.domain=="hydroBuiltin"){
                const auto record=hydroRuntime_.recordById(QString::fromStdString(ref.id));
                return record?record->name:QString::fromStdString(ref.id);
            }
            const auto property=project_.propertyView(ref);
            return property?QString::fromStdString(property->displayName):QString::fromStdString(ref.id);
        };
        return names.compare(title(a),title(b))<0;
    });
    return found;
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
    auto refs=mapCandidates(x,y,pixelsPerUnit,zoom);
    for(auto& ref:refs)if(ref.domain=="distributionEntry")for(const auto& entry:project_.document().distributionEntries)if(entry.id==ref.id){ref={"distributionLayer",entry.layerId};break;}
    std::set<ObjectRef> uniqueRefs;refs.erase(std::remove_if(refs.begin(),refs.end(),[&](const auto& ref){return !uniqueRefs.insert(ref).second;}),refs.end());
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
