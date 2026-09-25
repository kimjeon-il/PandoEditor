#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pandoeditor {
class ProjectSnapshot;
enum class GisExchangeTarget { Project, Country, Subunit, Region, Distribution, Generic };
struct GisTargetDescriptor {
    GisExchangeTarget target;
    std::string domain;
    bool replaceOnly=false,fallback=false;
};
std::optional<GisExchangeTarget> normalizeExchangeTarget(const std::string&,
    std::optional<GisExchangeTarget> fallback=GisExchangeTarget::Generic);
GisTargetDescriptor exchangeTargetDescriptor(GisExchangeTarget);

enum class GisImportKind { ProjectReplace, CountryMerge, Territorial, Generic, Distribution };
struct GisSource {std::string fileName,sourceKind;};
// Metadata-only immutable-in-use plan. File adapters populate mapping/payload
// after validation; no canonical mutation occurs before CommandProcessor confirm.
struct GisImportPlan {
    int version=1;
    std::string id,projectInstanceId,documentId;
    std::uint64_t revision=0;
    GisImportKind kind=GisImportKind::Generic;
    GisSource source;
    GisExchangeTarget target=GisExchangeTarget::Generic;
    std::vector<std::string> affectedIds;
    std::string mappingJson="{}",payloadJson="{}",summaryJson="{}";
};
GisImportPlan createGisImportPlan(const ProjectSnapshot&,std::string id,GisImportKind kind,
                                 GisSource,GisExchangeTarget,std::vector<std::string> affectedIds);
void assertCurrentGisImportPlan(const ProjectSnapshot&,const GisImportPlan&);
}
