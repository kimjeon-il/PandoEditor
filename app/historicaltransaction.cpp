#include "historicaltransaction.h"
#include <pandoeditor/project.h>
#include <pandoeditor/geometrypredicates.h>
#include <map>
#include <stdexcept>

namespace pandoeditor {
HistoricalInstantiationPlan prepareHistoricalTransaction(const ProjectSnapshot& project,
    const HistoricalLibrary& catalog,const std::vector<HistoricalAddRequest>& requests,
    const GeometryCalculator& calculator,const GeometryCancellation& cancelled) {
    if(!calculator)throw std::invalid_argument("INVALID_LIBRARY: M4 calculator required");
    if(cancelled&&cancelled())throw std::runtime_error("CANCELLED");
    std::vector<Geometry> replacements;
    std::map<ObjectRef,Geometry> replacementTargets;
    for(const auto& request:requests) {
        const auto selected=catalog.instantiate(request.libraryId,request.referenceDate,request.geometryVersionId);
        if(selected.instantiation.mode=="territory-replacement") {
            if((selected.type!=UnitKind::General||request.parent) && !request.asIndependentCountry)
                throw std::invalid_argument("INVALID_LIBRARY: territory replacement requires country");
            replacements.push_back(selected.geometry);
            replacementTargets.emplace(territorialRef(selected.libraryId),selected.geometry);
        }
    }
    std::vector<GeometryReplacement> patches;
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
            if(cancelled&&cancelled())throw std::runtime_error("CANCELLED");
            const auto& original=*project.document().geometries.get(pandoeditor::staticGeometryBinding(project.document(),unit.id).geometryRef);
            const auto intersection=calculator({GeometryOperation::Intersection,original,combined,{}},cancelled);
            if(!intersection.succeeded())throw std::runtime_error("INVALID_LIBRARY: M4 intersection failed");
            if(intersection.status==GeometryOperationStatus::Empty ||
               !significantArea(planarArea(intersection.geometry),planarArea(original)))continue;
            auto remaining=calculator({GeometryOperation::Difference,original,combined,{}},cancelled);
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
    return planHistorical(project,catalog,requests,patches,transfers);
}
}
