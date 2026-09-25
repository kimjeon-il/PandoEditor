#include <pandoeditor/gisexchange.h>
#include <pandoeditor/project.h>
#include <pandoeditor/geometrypredicates.h>
#include <algorithm>
#include <cctype>
#include <limits>
#include <set>
#include <stdexcept>

namespace pandoeditor {
namespace {
std::string trim(const std::string& raw) {
    const auto start=raw.find_first_not_of(" \t\r\n"),end=raw.find_last_not_of(" \t\r\n");
    return start==std::string::npos?"":raw.substr(start,end-start+1);
}
}
std::optional<GisExchangeTarget> normalizeExchangeTarget(const std::string& source,
    std::optional<GisExchangeTarget> fallback) {
    const auto name=trim(source);
    if(name=="project")return GisExchangeTarget::Project;
    if(name=="country")return GisExchangeTarget::Country;
    if(name=="subunit"||name=="admin"||name=="administrative"||name=="territory")
        return GisExchangeTarget::Subunit;
    if(name=="region")return GisExchangeTarget::Region;
    if(name=="distribution")return GisExchangeTarget::Distribution;
    if(name=="generic")return GisExchangeTarget::Generic;
    return fallback;
}
GisTargetDescriptor exchangeTargetDescriptor(GisExchangeTarget target) {
    switch(target) {
    case GisExchangeTarget::Project:return {target,"project",true,false};
    case GisExchangeTarget::Country:
    case GisExchangeTarget::Subunit:
    case GisExchangeTarget::Region:return {target,"territorial",false,false};
    case GisExchangeTarget::Distribution:return {target,"distribution",false,false};
    case GisExchangeTarget::Generic:return {target,"generic",false,true};
    }
    throw std::invalid_argument("INVALID_GIS_TARGET");
}
GisImportPlan createGisImportPlan(const ProjectSnapshot& project,std::string id,GisImportKind kind,
                                 GisSource source,GisExchangeTarget target,
                                 std::vector<std::string> affectedIds) {
    if(id.empty())throw std::invalid_argument("INVALID_GIS_PLAN: id");
    if(kind<GisImportKind::ProjectReplace||kind>GisImportKind::Distribution)
        throw std::invalid_argument("INVALID_GIS_PLAN: kind");
    (void)exchangeTargetDescriptor(target);
    std::set<std::string> seen;
    std::vector<std::string> unique;
    for(auto& item:affectedIds) {
        item=trim(item);
        if(!item.empty()&&seen.insert(item).second)unique.push_back(item);
    }
    return {1,std::move(id),project.instanceId(),project.document().documentId,
            project.revision(),kind,std::move(source),target,std::move(unique)};
}
void assertCurrentGisImportPlan(const ProjectSnapshot& project,const GisImportPlan& plan) {
    if(plan.version!=1||plan.id.empty()||plan.kind<GisImportKind::ProjectReplace||
       plan.kind>GisImportKind::Distribution)
        throw std::invalid_argument("INVALID_GIS_PLAN");
    (void)exchangeTargetDescriptor(plan.target);
    if(plan.projectInstanceId!=project.instanceId()||plan.documentId!=project.document().documentId||
       plan.revision!=project.revision())
        throw std::invalid_argument("STALE_GIS_PLAN");
}
GisTerritorialImportPlan planTerritorialGisImport(const ProjectSnapshot& project,
    std::string planId,GisSource source,GisExchangeTarget target,
    std::vector<GisTerritorialInput> units,std::vector<GeometryReplacement> replacements) {
    if(target!=GisExchangeTarget::Country&&target!=GisExchangeTarget::Subunit&&
       target!=GisExchangeTarget::Region)
        throw std::invalid_argument("INVALID_GIS_PLAN: territorial target");
    if(units.empty())throw std::invalid_argument("INVALID_GIS_PLAN: empty territorial import");
    std::vector<std::string> ids;
    std::set<std::string> unique;
    const auto kind=target==GisExchangeTarget::Country?UnitKind::Country:
        target==GisExchangeTarget::Subunit?UnitKind::Subunit:UnitKind::Region;
    for(const auto& row:units) {
        if(row.id.empty()||row.name.empty()||row.kind!=kind||!unique.insert(row.id).second||
           (row.color&&*row.color>0xffffff))
            throw std::invalid_argument("INVALID_GIS_PLAN: territorial row");
        const auto found=project.index().objects.find(territorialRef(row.id));
        if(row.replaceExisting != (found!=project.index().objects.end()) ||
           (row.replaceExisting && project.document().units.at(found->second).kind!=UnitKind::Country))
            throw std::invalid_argument("DUPLICATE_ID: territorial import");
        ids.push_back(row.id);
    }
    std::set<ObjectRef> patched;
    for(const auto& patch:replacements) {
        const auto found=project.index().objects.find(patch.owner);
        if(patch.owner.domain!="territorial"||found==project.index().objects.end()||
           project.document().units.at(found->second).kind!=UnitKind::Country||
           !patched.insert(patch.owner).second||unique.count(patch.owner.id))
            throw std::invalid_argument("INVALID_GIS_PLAN: country patch owner");
    }
    GisTerritorialImportPlan plan{createGisImportPlan(project,std::move(planId),
        kind==UnitKind::Country?GisImportKind::CountryMerge:GisImportKind::Territorial,
        std::move(source),target,std::move(ids)),std::move(units),std::move(replacements)};
    auto candidate=project.document();
    applyTerritorialGisImport(candidate,plan);
    validateDocument(candidate);
    for(std::size_t i=0;i<candidate.units.size();++i)if(candidate.units[i].kind==UnitKind::Country)
        for(std::size_t j=i+1;j<candidate.units.size();++j)if(candidate.units[j].kind==UnitKind::Country) {
            const auto& left=*candidate.geometries.get(candidate.units[i].geometry);
            const auto& right=*candidate.geometries.get(candidate.units[j].geometry);
            if(geometrySignificantOverlap(left,right))
                throw std::invalid_argument("GIS_COUNTRY_OVERLAP");
        }
    return plan;
}
void applyTerritorialGisImport(ProjectDocument& document,const GisTerritorialImportPlan& plan) {
    if(plan.units.empty()||plan.info.affectedIds.size()!=plan.units.size()||
       (plan.info.target!=GisExchangeTarget::Country&&plan.info.target!=GisExchangeTarget::Subunit&&
        plan.info.target!=GisExchangeTarget::Region)||
       plan.info.kind!=(plan.info.target==GisExchangeTarget::Country?
           GisImportKind::CountryMerge:GisImportKind::Territorial))
        throw std::invalid_argument("INVALID_GIS_PLAN: territorial payload");
    std::set<std::string> seen;
    std::set<ObjectRef> patched;
    auto replace=[&](TerritorialUnit& unit,const Geometry& geometry) {
        GeometryRef ref=unit.geometry;
        do {
            if(ref.version==std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("INVALID_GIS_PLAN: geometry version overflow");
            ++ref.version;
        }while(document.geometries.get(ref));
        document.geometries.insert(ref,geometry);
        unit.geometry=ref;
    };
    for(const auto& patch:plan.countryReplacements) {
        auto unit=std::find_if(document.units.begin(),document.units.end(),
            [&](const auto& row){return territorialRef(row.id)==patch.owner;});
        if(patch.owner.domain!="territorial"||!patched.insert(patch.owner).second||
           unit==document.units.end()||unit->kind!=UnitKind::Country)
            throw std::invalid_argument("INVALID_GIS_PLAN: country patch");
        replace(*unit,patch.geometry);
    }
    for(std::size_t i=0;i<plan.units.size();++i) {
        const auto& input=plan.units[i];
        const auto kind=plan.info.target==GisExchangeTarget::Country?UnitKind::Country:
            plan.info.target==GisExchangeTarget::Subunit?UnitKind::Subunit:UnitKind::Region;
        if(input.id.empty()||input.name.empty()||plan.info.affectedIds[i]!=input.id||
           input.kind!=kind||!seen.insert(input.id).second||
           (input.color&&*input.color>0xffffff))
            throw std::invalid_argument("INVALID_GIS_PLAN: territorial row");
        auto unit=std::find_if(document.units.begin(),document.units.end(),
            [&](const auto& row){return row.id==input.id;});
        if(input.replaceExisting) {
            if(kind!=UnitKind::Country||unit==document.units.end()||unit->kind!=kind||
               patched.count(territorialRef(input.id)))
                throw std::invalid_argument("INVALID_GIS_PLAN: country replacement");
            replace(*unit,input.geometry);
            unit->name=input.name;unit->nameExplicit=true;unit->notes=input.notes;
            unit->validity=input.validity;
        } else {
            if(unit!=document.units.end())throw std::invalid_argument("DUPLICATE_ID: territorial import");
            GeometryRef ref{"gis-territorial:"+input.id,1};
            if(document.geometries.get(ref))throw std::invalid_argument("DUPLICATE_ID: GIS geometry");
            document.geometries.insert(ref,input.geometry);
            TerritorialUnit created;created.id=input.id;created.name=input.name;
            created.nameExplicit=true;created.baseName=kind==UnitKind::Country?input.name:"";
            created.notes=input.notes;created.kind=kind;created.geometry=ref;
            created.validity=input.validity;
            created.coverageMode=kind==UnitKind::Subunit?"partition":"explicit";
            document.units.push_back(std::move(created));
            const auto owner=territorialRef(input.id);
            document.presentation.objectStyles.emplace(owner,ObjectStyle{});
            if(!document.presentation.userLayers.empty())
                document.presentation.membership.emplace(owner,document.presentation.userLayers.front().id);
            if(kind!=UnitKind::Country) {
                const auto relationId="gis-relation:"+input.id;
                if(std::any_of(document.relations.begin(),document.relations.end(),
                    [&](const auto& relation){return relation.id==relationId;}))
                    throw std::invalid_argument("DUPLICATE_ID: GIS relation");
                document.relations.push_back({relationId,owner,input.parent,input.sovereign,false,{}});
            }
        }
        if(input.color) {
            auto& style=document.presentation.objectStyles[territorialRef(input.id)];
            style.color=*input.color;style.explicitColor=true;
        }
    }
}
GisGenericImportPlan planGenericGisImport(const ProjectSnapshot& project,std::string planId,
    GisSource source,std::vector<GisGenericInput> features) {
    if(features.empty())throw std::invalid_argument("INVALID_GIS_PLAN: empty generic import");
    std::set<std::string> seen;
    std::vector<std::string> ids;
    for(const auto& feature:features) {
        if(feature.id.empty()||!seen.insert(feature.id).second||
           project.index().objects.count({"generic",feature.id}))
            throw std::invalid_argument("DUPLICATE_ID: generic feature");
        ids.push_back(feature.id);
    }
    GisGenericImportPlan plan{createGisImportPlan(project,std::move(planId),
        GisImportKind::Generic,std::move(source),GisExchangeTarget::Generic,std::move(ids)),
        std::move(features)};
    auto candidate=project.document();
    applyGenericGisImport(candidate,plan);
    validateDocument(candidate);
    return plan;
}
void applyGenericGisImport(ProjectDocument& document,const GisGenericImportPlan& plan) {
    if(plan.info.kind!=GisImportKind::Generic||plan.info.target!=GisExchangeTarget::Generic||
       plan.features.empty()||plan.info.affectedIds.size()!=plan.features.size())
        throw std::invalid_argument("INVALID_GIS_PLAN: generic payload");
    std::set<std::string> seen;
    for(std::size_t i=0;i<plan.features.size();++i) {
        const auto& feature=plan.features[i];
        if(feature.id.empty()||plan.info.affectedIds[i]!=feature.id||!seen.insert(feature.id).second)
            throw std::invalid_argument("INVALID_GIS_PLAN: generic IDs");
        if(std::any_of(document.genericFeatures.begin(),document.genericFeatures.end(),
             [&](const auto& existing){return existing.id==feature.id;}))
            throw std::invalid_argument("DUPLICATE_ID: generic feature");
        GeometryRef geometry{"gis-generic:"+feature.id,1};
        document.geometries.insert(geometry,feature.geometry);
        GenericFeature row;row.id=feature.id;row.name=feature.name;row.notes=feature.notes;
        row.color=feature.color;row.geometry=geometry;
        row.source.kind="gis";row.source.dataset=plan.info.source.fileName;
        row.source.sourceFormat=plan.info.source.sourceKind;
        row.source.sourceId=feature.id;row.source.details=feature.propertiesJson;
        document.genericFeatures.push_back(std::move(row));
    }
}
}
