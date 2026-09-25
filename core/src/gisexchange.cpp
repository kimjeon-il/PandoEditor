#include <pandoeditor/gisexchange.h>
#include <pandoeditor/project.h>
#include <algorithm>
#include <cctype>
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
        GenericFeature row;row.id=feature.id;row.name=feature.name;row.geometry=geometry;
        row.source.kind="gis";row.source.dataset=plan.info.source.fileName;
        row.source.sourceFormat=plan.info.source.sourceKind;
        row.source.sourceId=feature.id;row.source.details=feature.propertiesJson;
        document.genericFeatures.push_back(std::move(row));
    }
}
}
