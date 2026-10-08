#include <pandoeditor/timeline-view.h>
#include <algorithm>
#include <stdexcept>

namespace pandoeditor {
TimelineDocumentView timelineDocumentView(const ProjectDocument& source,const std::string& month) {
    TimelineDocumentView result;
    result.world=resolveWorld(source.timelineRecords,
        {timelineEntityCatalog(source),[&](const GeometryRef& ref){return source.geometries.get(ref)!=nullptr;}},month);
    result.document=source;
    result.document.timelineRecords={};
    for(const auto& unit:source.units) {
        const auto* active=result.world.find(unit.id);
        GeometryRef geometry;
        std::string parent,coverage="explicit";
        if(active) {
            geometry=active->geometryRef;
            parent=active->parentId;
            coverage=active->coverageMode;
        } else {
            result.inactiveIds.insert(unit.id);
            const auto found=std::find_if(source.timelineRecords.geometryBindings.begin(),
                source.timelineRecords.geometryBindings.end(),[&](const auto& row){return row.entityId==unit.id;});
            if(found==source.timelineRecords.geometryBindings.end())
                throw std::invalid_argument("TIMELINE_GEOMETRY: missing entity binding");
            geometry=found->geometryRef;
        }
        addStaticTerritorialRecords(result.document,unit.id,geometry,parent,coverage);
    }
    for(const auto& unit:source.units)if(result.inactiveIds.count(unit.id)) {
        const auto group=unit.kind==UnitKind::Regional?"regions":
            staticParentRelation(result.document,unit.id).parentId.empty()?"countries":"subunits";
        result.document.presentation.webPresentation.hiddenItems[group].insert(unit.id);
    }
    return result;
}
}
