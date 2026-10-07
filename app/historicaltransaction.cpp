#include "historicaltransaction.h"
#include <pandoeditor/project.h>
#include <pandoeditor/geometrypredicates.h>
#include <map>
#include <set>
#include <stdexcept>
#include <limits>
#include <algorithm>

namespace pandoeditor {
namespace {
struct PolygonBounds {
    double left=std::numeric_limits<double>::infinity(),bottom=left,right=-left,top=-left;
};
PolygonBounds polygonBounds(const Polygon& polygon) {
    PolygonBounds bounds;
    for(const auto& ring:polygon)for(const auto point:ring) {
        bounds.left=std::min(bounds.left,point.x);bounds.right=std::max(bounds.right,point.x);
        bounds.bottom=std::min(bounds.bottom,point.y);bounds.top=std::max(bounds.top,point.y);
    }
    return bounds;
}
// The clipping kernel operates on these raw planar coordinates. Inclusive
// bounds retain touching components and never edit, round or reorder a ring.
Geometry relevantReplacement(const Geometry& original,const Geometry& combined) {
    std::vector<PolygonBounds> originals;
    for(const auto& polygon:original.polygons)originals.push_back(polygonBounds(polygon));
    Geometry relevant;
    for(const auto& polygon:combined.polygons) {
        const auto bounds=polygonBounds(polygon);
        if(std::any_of(originals.begin(),originals.end(),[&](const auto& other) {
            return bounds.left<=other.right&&bounds.right>=other.left&&bounds.bottom<=other.top&&bounds.top>=other.bottom;
        }))relevant.polygons.push_back(polygon);
    }
    relevant.type=relevant.polygons.size()==1?"Polygon":"MultiPolygon";return relevant;
}
}
HistoricalInstantiationPlan prepareHistoricalTransaction(const ProjectSnapshot& project,
    const HistoricalLibrary& catalog,const std::vector<HistoricalAddRequest>& requests,
    const GeometryCalculator& calculator,const GeometryCancellation& cancelled) {
    std::vector<HistoricalAddition> additions;
    for(const auto& request:requests) {
        HistoricalAddition addition{catalog.instantiate(request.libraryId,request.referenceDate,request.geometryVersionId),
            normalizeTemporal(request.referenceDate),request.parent,request.sovereign,request.approvePartial,
            request.asIndependentCountry,request.countryName};
        addition.instanceId=request.instanceId;additions.push_back(std::move(addition));
    }
    return prepareHistoricalTransaction(project,std::move(additions),calculator,cancelled);
}
HistoricalInstantiationPlan prepareHistoricalTransaction(const ProjectSnapshot& project,
    std::vector<HistoricalAddition> additions,
    const GeometryCalculator& calculator,const GeometryCancellation& cancelled) {
    if(!calculator)throw std::invalid_argument("INVALID_LIBRARY: M4 calculator required");
    if(cancelled&&cancelled())throw std::runtime_error("CANCELLED");
    std::vector<Geometry> replacements;
    std::map<ObjectRef,Geometry> replacementTargets;
    std::vector<GeometryReplacement> patches;
    const auto identity=[](const HistoricalAddition& item)->const std::string& {
        return item.instanceId.empty()?item.selection.libraryId:item.instanceId;
    };
    std::map<std::string,std::size_t> selectedById;
    for(std::size_t n=0;n<additions.size();++n)selectedById.emplace(identity(additions[n]),n);
    std::map<std::string,std::vector<Geometry>> expansions;
    for(const auto& item:additions)if(!item.instanceId.empty()&&item.parent) {
        std::string parent=item.parent->id;std::set<std::string> seen;
        while(!parent.empty()) {
            if(!seen.insert(parent).second)throw std::invalid_argument("PL-LIB-PARENT: cyclic ownership");
            if(const auto found=selectedById.find(parent);found!=selectedById.end()) {
                const auto& owner=additions.at(found->second);
                if(owner.selection.type!=UnitKind::General)throw std::invalid_argument("INVALID_PARENT_KIND");
                if(!owner.parent){expansions[parent].push_back(item.selection.geometry);break;}
                if(!geometryContains(owner.selection.geometry,item.selection.geometry))throw std::invalid_argument("PL-LIB-PARENT: boundary outside intermediate parent");
                parent=owner.parent->id;
            } else {
                const auto projectParent=project.index().objects.find(territorialRef(parent));
                if(projectParent==project.index().objects.end()||project.document().units.at(projectParent->second).kind!=UnitKind::General)
                    throw std::invalid_argument("INVALID_PARENT_KIND");
                const auto& owner=project.document().units.at(projectParent->second);
                if(isRootGeneral(project.document(),owner)){expansions[parent].push_back(item.selection.geometry);break;}
                const auto& shape=*project.document().geometries.get(staticGeometryBinding(project.document(),parent).geometryRef);
                if(!geometryContains(shape,item.selection.geometry))throw std::invalid_argument("PL-LIB-PARENT: boundary outside intermediate parent");
                parent=staticParentRelation(project.document(),parent).parentId;
            }
        }
    }
    // Fixed Web territorial-library-batch expands a root to cover imported
    // descendants. Intermediate subunits retain exact containment constraints.
    for(auto& [id,operands]:expansions) {
        Geometry original;
        const auto fresh=selectedById.find(id);
        if(fresh!=selectedById.end())original=additions.at(fresh->second).selection.geometry;
        else original=*project.document().geometries.get(staticGeometryBinding(project.document(),id).geometryRef);
        bool needed=false;for(const auto& shape:operands)needed=needed||!geometryContains(original,shape);
        if(!needed)continue;
        operands.insert(operands.begin(),original);
        auto expanded=calculator({GeometryOperation::Union,{},{},std::move(operands)},cancelled);
        if(expanded.status!=GeometryOperationStatus::Completed)throw std::runtime_error("PL-LIB-GEOMETRY: root expansion failed");
        if(fresh!=selectedById.end())additions.at(fresh->second).selection.geometry=std::move(expanded.geometry);
        else {
            patches.push_back({territorialRef(id),std::move(expanded.geometry)});
            replacementTargets.emplace(territorialRef(id),patches.back().geometry);
            replacements.push_back(patches.back().geometry);
        }
    }
    for(const auto& request:additions) {
        const auto& selected=request.selection;
        if(selected.instantiation.mode=="territory-replacement"||
           (!request.instanceId.empty()&&selected.type==UnitKind::General&&!request.parent)) {
            if((selected.type!=UnitKind::General||request.parent) && !request.asIndependentCountry)
                throw std::invalid_argument("INVALID_LIBRARY: territory replacement requires country");
            replacements.push_back(selected.geometry);
            replacementTargets.emplace(territorialRef(identity(request)),selected.geometry);
        }
    }
    std::map<ObjectRef,ObjectRef> transfers;
    if(!replacements.empty()) {
        auto combined=replacements.front();
        if(replacements.size()>1) {
            auto result=calculator({GeometryOperation::Union,{},{},std::move(replacements)},cancelled);
            if(result.status!=GeometryOperationStatus::Completed)throw std::runtime_error("INVALID_LIBRARY: territory union failed");
            combined=std::move(result.geometry);
        }
        std::map<ObjectRef,Geometry> donorRemainders;
        for(const auto& unit:project.document().units)if(isRootGeneral(project.document(),unit)) {
            if(replacementTargets.count(territorialRef(unit.id)))continue;
            if(cancelled&&cancelled())throw std::runtime_error("CANCELLED");
            const auto& original=*project.document().geometries.get(pandoeditor::staticGeometryBinding(project.document(),unit.id).geometryRef);
            const auto relevant=relevantReplacement(original,combined);
            if(relevant.polygons.empty())continue;
            const auto intersection=calculator({GeometryOperation::Intersection,original,relevant,{}},cancelled);
            if(!intersection.succeeded())throw std::runtime_error("INVALID_LIBRARY: M4 intersection failed");
            if(intersection.status==GeometryOperationStatus::Empty ||
               !significantArea(planarArea(intersection.geometry),planarArea(original)))continue;
            auto remaining=calculator({GeometryOperation::Difference,original,relevant,{}},cancelled);
            if(remaining.status==GeometryOperationStatus::Empty ||
               (remaining.status==GeometryOperationStatus::Completed &&
                !significantArea(planarArea(remaining.geometry),planarArea(original)))) {
                std::optional<ObjectRef> destination;
                for(const auto& [target,shape]:replacementTargets)if(geometryContains(shape,original)) {
                    if(destination)throw std::invalid_argument("INVALID_LIBRARY: ambiguous absorbed country");
                    destination=target;
                }
                if(!destination)throw std::invalid_argument("INVALID_LIBRARY: split absorbed country");
                transfers.emplace(territorialRef(unit.id),*destination);
                continue;
            }
            if(remaining.status!=GeometryOperationStatus::Completed)
                throw std::runtime_error("INVALID_LIBRARY: M4 difference failed");
            donorRemainders.emplace(territorialRef(unit.id),remaining.geometry);
            patches.push_back({territorialRef(unit.id),std::move(remaining.geometry)});
        }
        for(const auto& unit:project.document().units)if(unit.kind==UnitKind::General&&!isRootGeneral(project.document(),unit)) {
            if(cancelled&&cancelled())throw std::runtime_error("CANCELLED");
            const auto& relation=staticParentRelation(project.document(),unit.id);
            const auto parent=territorialRef(relation.parentId);if(relation.parentId.empty()||!donorRemainders.count(parent))continue;
            const auto& original=*project.document().geometries.get(staticGeometryBinding(project.document(),unit.id).geometryRef);
            auto remaining=calculator({GeometryOperation::Intersection,original,donorRemainders.at(parent),{}},cancelled);
            if(remaining.status==GeometryOperationStatus::Empty)
                throw std::invalid_argument("INVALID_LIBRARY: territory would erase dependent unit");
            if(remaining.status!=GeometryOperationStatus::Completed)
                throw std::runtime_error("INVALID_LIBRARY: dependent geometry reconciliation failed");
            patches.push_back({territorialRef(unit.id),std::move(remaining.geometry)});
        }
    }
    if(cancelled&&cancelled())throw std::runtime_error("CANCELLED");
    for(auto& item:additions)if(!item.instanceId.empty())
        for(const auto& [donor,target]:transfers)item.selection.instantiation.countryNameUpdates.erase(donor.id);
    return planHistoricalSelections(project,additions,patches,transfers);
}
}
