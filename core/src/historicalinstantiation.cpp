#include <pandoeditor/historicalinstantiation.h>
#include <pandoeditor/project.h>
#include <pandoeditor/geometrypredicates.h>
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace pandoeditor {
HistoricalInstantiationPlan planHistorical(const ProjectSnapshot& project,
    const HistoricalLibrary& catalog,const std::vector<HistoricalAddRequest>& requests,
    const std::vector<GeometryReplacement>& replacements,
    const std::map<ObjectRef,ObjectRef>& transfers) {
    requireStaticTimeline(project.document());
    if(requests.empty())throw std::invalid_argument("INVALID_LIBRARY: empty selection");
    HistoricalInstantiationPlan plan{project.instanceId(),project.document().documentId,project.revision(),{}};
    std::set<std::string> ids;
    for(const auto& request:requests) {
        auto selection=catalog.instantiate(request.libraryId,request.referenceDate,request.geometryVersionId);
        if(!selection.sovereignLibraryId.empty())throw std::invalid_argument("UNSUPPORTED_POLITICAL_RELATION: historical catalog sovereignty");
        if(selection.instantiation.mode!="independent"&&selection.instantiation.mode!="territory-replacement")
            throw std::invalid_argument("INVALID_LIBRARY: unsupported instantiation");
        if(selection.partial&&!request.approvePartial)
            throw std::invalid_argument("INVALID_LIBRARY: partial source requires approval");
        if(!ids.insert(selection.libraryId).second || project.index().objects.count(territorialRef(selection.libraryId)))
            throw std::invalid_argument("DUPLICATE_ID: historical unit");
        if(request.asIndependentCountry && (selection.type!=UnitKind::General || request.parent || request.sovereign || request.countryName.empty()))
            throw std::invalid_argument("INVALID_LIBRARY: independent country ownership");
        if(request.sovereign)throw std::invalid_argument("UNSUPPORTED_POLITICAL_RELATION");
        if(selection.type==UnitKind::Regional&&request.parent)throw std::invalid_argument("INVALID_PARENT_KIND");
        if(selection.validity.from||selection.validity.to)throw std::invalid_argument("TIMELINE_ACTIVATION: dated creation requires T4");
        for(const auto& [country,name]:selection.instantiation.countryNameUpdates) {
            const auto existing=plan.countryNameUpdates.find(country);
            if(existing!=plan.countryNameUpdates.end()&&existing->second!=name)
                throw std::invalid_argument("INVALID_LIBRARY: conflicting country updates");
            plan.countryNameUpdates[country]=name;
        }
        plan.additions.push_back({std::move(selection),normalizeTemporal(request.referenceDate),
                                  request.parent,request.sovereign,request.approvePartial,
                                  request.asIndependentCountry,request.countryName});
    }
    plan.territoryReplacements=replacements;
    plan.territoryTransfers=transfers;
    for(const auto& [donor,target]:transfers) {
        const auto found=project.index().objects.find(donor);
        const auto selected=std::find_if(plan.additions.begin(),plan.additions.end(),[&](const auto& addition){
            return territorialRef(addition.selection.libraryId)==target&&
                (addition.selection.type==UnitKind::General||addition.asIndependentCountry)&&
                addition.selection.instantiation.mode=="territory-replacement";
        });
        if(donor.domain!="territorial"||target.domain!="territorial"||found==project.index().objects.end()||
           project.document().units.at(found->second).kind!=UnitKind::General||donor==target||
           selected==plan.additions.end()||
           !geometryContains(selected->selection.geometry,
               *project.document().geometries.get(staticGeometryBinding(project.document(),donor.id).geometryRef)))
            throw std::invalid_argument("INVALID_LIBRARY: territory transfer target");
    }
    for(const auto& [id,name]:plan.countryNameUpdates) {
        if(transfers.count(territorialRef(id)))
            throw std::invalid_argument("INVALID_LIBRARY: cannot rename transferred country");
        auto found=project.index().objects.find(territorialRef(id));
        if(found==project.index().objects.end()||project.document().units.at(found->second).kind!=UnitKind::General||name.empty())
            throw std::invalid_argument("INVALID_LIBRARY: country update target");
    }
    // Verify every relation, geometry and sibling constraint before exposing a
    // plan; CommandProcessor repeats this against a candidate at prepare time.
    auto candidate=project.document();
    applyHistoricalInstantiation(candidate,plan);
    reconcileGeometryProvenance(project.document(),candidate);
    validateDocument(candidate);
    return plan;
}
HistoricalInstantiationPlan planIndependentHistorical(const ProjectSnapshot& project,
    const HistoricalLibrary& catalog,const std::vector<HistoricalAddRequest>& requests) {
    for(const auto& request:requests) {
        const auto selection=catalog.instantiate(request.libraryId,request.referenceDate,request.geometryVersionId);
        if(selection.instantiation.mode!="independent")
            throw std::invalid_argument("INVALID_LIBRARY: territory replacement requires M4 plan");
    }
    return planHistorical(project,catalog,requests);
}
HistoricalInstantiationPlan planIndependentHistoricalSnapshot(const ProjectSnapshot& project,
    const HistoricalLibrary& catalog,const std::string& snapshotId,
    const std::vector<HistoricalAddRequest>& overrides) {
    const auto* snapshot=catalog.getSnapshot(snapshotId);
    if(!snapshot)throw std::invalid_argument("INVALID_LIBRARY: missing snapshot");
    const auto refs=catalog.entityRefsWithChildren(snapshot->entityRefs,"all");
    std::map<std::string,HistoricalAddRequest> choices;
    for(const auto& choice:overrides)
        if(!choices.emplace(choice.libraryId,choice).second)
            throw std::invalid_argument("INVALID_LIBRARY: duplicate override");
    std::vector<HistoricalAddRequest> requests;
    for(const auto& id:refs) {
        if(!catalog.get(id))throw std::invalid_argument("INVALID_LIBRARY: missing snapshot entity");
        HistoricalAddRequest request;
        const auto it=choices.find(id);
        if(it!=choices.end()){request=it->second;choices.erase(it);}
        request.libraryId=id;
        request.referenceDate=snapshot->referenceDate.value_or("");
        requests.push_back(std::move(request));
    }
    if(!choices.empty())throw std::invalid_argument("INVALID_LIBRARY: override outside snapshot");
    return planIndependentHistorical(project,catalog,requests);
}
void applyHistoricalInstantiation(ProjectDocument& document,const HistoricalInstantiationPlan& plan) {
    std::set<ObjectRef> replaced;
    for(const auto& patch:plan.territoryReplacements) {
        if(patch.owner.domain!="territorial"||!replaced.insert(patch.owner).second)
            throw std::invalid_argument("INVALID_LIBRARY: geometry patch owner");
        auto unit=std::find_if(document.units.begin(),document.units.end(),[&](const auto& u){return u.id==patch.owner.id;});
        if(unit==document.units.end() || (patch.geometry.type!="Polygon" && patch.geometry.type!="MultiPolygon"))
            throw std::invalid_argument("INVALID_LIBRARY: geometry patch target");
        auto& binding=staticGeometryBinding(document,unit->id);GeometryRef ref=binding.geometryRef;
        do {
            if(ref.version==std::numeric_limits<std::uint32_t>::max())throw std::invalid_argument("INVALID_LIBRARY: geometry version overflow");
            ++ref.version;
        }while(document.geometries.get(ref));
        document.geometries.insert(ref,patch.geometry);binding.geometryRef=ref;
    }
    for(const auto& [id,name]:plan.countryNameUpdates) {
        auto unit=std::find_if(document.units.begin(),document.units.end(),[&](const auto& u){return u.id==id&&u.kind==UnitKind::General;});
        if(unit==document.units.end()||name.empty())throw std::invalid_argument("INVALID_LIBRARY: country update target");
        unit->name=name;unit->nameExplicit=true;
    }
    for(const auto& addition:plan.additions) {
        const auto& selection=addition.selection;
        const auto kind=addition.asIndependentCountry?UnitKind::General:selection.type;
        GeometryRef ref{"historical-geometry:"+selection.libraryId,1};
        if(document.geometries.get(ref))throw std::invalid_argument("DUPLICATE_ID: historical geometry");
        document.geometries.insert(ref,selection.geometry);
        TerritorialUnit unit;
        unit.id=selection.libraryId;unit.kind=kind;unit.name=addition.asIndependentCountry?addition.countryName:selection.name;
        unit.baseName=kind==UnitKind::General?unit.name:"";
        unit.nameExplicit=true;
        unit.sourceEntityId=selection.libraryId;
        unit.sourceGeometryVersion=selection.geometryVersionId;
        unit.libraryOrigin=LibraryOrigin{selection.libraryId,selection.geometryVersionId,
            addition.referenceDate,selection.sourceId,"2",selection.certainty,selection.datePrecision,
            selection.partial,selection.missingSourceIds};
        document.units.push_back(std::move(unit));
        const auto owner=territorialRef(selection.libraryId);
        document.presentation.objectStyles.emplace(owner,ObjectStyle{});
        if(!document.presentation.userLayers.empty())
            document.presentation.membership.emplace(owner,document.presentation.userLayers.front().id);
        if(addition.sovereign)throw std::invalid_argument("UNSUPPORTED_POLITICAL_RELATION");
        addStaticTerritorialRecords(document,selection.libraryId,ref,addition.parent?addition.parent->id:"","explicit");
    }
    for(const auto& [donor,target]:plan.territoryTransfers) {
        auto existing=std::find_if(document.units.begin(),document.units.end(),[&](const auto& u){return territorialRef(u.id)==donor;});
        if(existing==document.units.end()||existing->kind!=UnitKind::General)
            throw std::invalid_argument("INVALID_LIBRARY: missing transferred country");
        if(document.symbols.count(donor))
            throw std::invalid_argument("INVALID_LIBRARY: transferred country has flag asset");
        const auto details=document.countryDetails.find(donor);
        if(details!=document.countryDetails.end()&&!details->second.capital.empty())
            throw std::invalid_argument("INVALID_LIBRARY: transferred country has capital");
        document.countryDetails.erase(donor);
        for(auto& relation:document.timelineRecords.parentRelations)if(relation.parentId==donor.id)relation.parentId=target.id;
        removeTerritorialRecords(document,{donor.id});
        for(auto& label:document.labels)if(label.territory==donor)label.territory=target;
        for(auto& entry:document.distributionEntries)if(entry.territory==donor)entry.territory=target;
        document.presentation.membership.erase(donor);
        document.presentation.objectStyles.erase(donor);
        auto& web=document.presentation.webPresentation;
        for(auto& [group,hidden]:web.hiddenItems)hidden.erase(donor.id);
        for(auto it=web.objectStyles.begin();it!=web.objectStyles.end();)
            it=it->first.find(":"+donor.id)!=std::string::npos?web.objectStyles.erase(it):std::next(it);
        web.objectOrder.erase(std::remove_if(web.objectOrder.begin(),web.objectOrder.end(),
            [&](const auto& key){return key.find(":"+donor.id)!=std::string::npos;}),web.objectOrder.end());
        document.units.erase(existing);
    }
}
}
