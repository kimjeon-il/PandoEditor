#include <pandoeditor/historicalinstantiation.h>
#include <pandoeditor/project.h>
#include <algorithm>
#include <set>
#include <stdexcept>

namespace pandoeditor {
HistoricalInstantiationPlan planIndependentHistorical(const ProjectSnapshot& project,
    const HistoricalLibrary& catalog,const std::vector<HistoricalAddRequest>& requests) {
    if(requests.empty())throw std::invalid_argument("INVALID_LIBRARY: empty selection");
    HistoricalInstantiationPlan plan{project.instanceId(),project.document().documentId,project.revision(),{}};
    std::set<std::string> ids;
    for(const auto& request:requests) {
        auto selection=catalog.instantiate(request.libraryId,request.referenceDate,request.geometryVersionId);
        if(selection.instantiation.mode!="independent")
            throw std::invalid_argument("INVALID_LIBRARY: territory replacement requires M4 plan");
        if(selection.partial&&!request.approvePartial)
            throw std::invalid_argument("INVALID_LIBRARY: partial source requires approval");
        if(!ids.insert(selection.libraryId).second || project.index().objects.count(territorialRef(selection.libraryId)))
            throw std::invalid_argument("DUPLICATE_ID: historical unit");
        if(selection.type==UnitKind::Subunit && (!request.parent||!request.sovereign))
            throw std::invalid_argument("SOVEREIGN_MISMATCH: explicit ownership required");
        if(selection.type==UnitKind::Country && (request.parent||request.sovereign))
            throw std::invalid_argument("INVALID_LIBRARY: country ownership");
        plan.additions.push_back({std::move(selection),normalizeTemporal(request.referenceDate),
                                  request.parent,request.sovereign,request.approvePartial});
    }
    // Verify every relation, geometry and sibling constraint before exposing a
    // plan; CommandProcessor repeats this against a candidate at prepare time.
    auto candidate=project.document();
    applyHistoricalInstantiation(candidate,plan);
    validateDocument(candidate);
    return plan;
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
    for(const auto& addition:plan.additions) {
        const auto& selection=addition.selection;
        GeometryRef ref{"historical-geometry:"+selection.libraryId,1};
        if(document.geometries.get(ref))throw std::invalid_argument("DUPLICATE_ID: historical geometry");
        document.geometries.insert(ref,selection.geometry);
        TerritorialUnit unit;
        unit.id=selection.libraryId;unit.kind=selection.type;unit.name=selection.name;
        unit.baseName=selection.type==UnitKind::Country?selection.name:"";
        unit.nameExplicit=true;unit.geometry=ref;unit.validity=selection.validity;
        unit.coverageMode=selection.type==UnitKind::Subunit?"partition":"explicit";
        unit.libraryOrigin=LibraryOrigin{selection.libraryId,selection.geometryVersionId,
            addition.referenceDate,selection.sourceId,"2",selection.certainty,selection.datePrecision,
            selection.partial,selection.missingSourceIds};
        document.units.push_back(std::move(unit));
        const auto owner=territorialRef(selection.libraryId);
        document.presentation.objectStyles.emplace(owner,ObjectStyle{});
        if(!document.presentation.userLayers.empty())
            document.presentation.membership.emplace(owner,document.presentation.userLayers.front().id);
        if(selection.type!=UnitKind::Country) {
            auto relationId="historical-relation:"+selection.libraryId;
            if(std::any_of(document.relations.begin(),document.relations.end(),[&](const auto& r){return r.id==relationId;}))
                throw std::invalid_argument("DUPLICATE_ID: historical relation");
            document.relations.push_back({relationId,owner,addition.parent,addition.sovereign,false,{}});
        }
    }
}
}
