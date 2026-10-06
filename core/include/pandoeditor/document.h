#pragma once
#include <pandoeditor/presentation.h>
#include <pandoeditor/temporal.h>
#include <pandoeditor/geometry-types.h>
#include <pandoeditor/timeline-records.h>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace pandoeditor {
inline ObjectRef territorialRef(const std::string& id) { return {"territorial",id}; }
enum class UnitKind { General, Regional };
struct SourceProvenance {
    std::string kind="user", dataset, version, sourceId, sourceFormat, sourceType, importedAt;
    // Lossless JSON object; parsing is owned by the codec boundary.
    std::string details="{}";
};
struct LibraryOrigin {
    std::string libraryId,geometryVersionId;
    std::optional<std::string> referenceDate;
    std::string sourceId,sourceVersion,certainty,datePrecision;
    bool partial=false;
    std::vector<std::string> missingLibraryRefs;
    bool operator==(const LibraryOrigin& other) const {
        return std::tie(libraryId,geometryVersionId,referenceDate,sourceId,sourceVersion,
                        certainty,datePrecision,partial,missingLibraryRefs)==
               std::tie(other.libraryId,other.geometryVersionId,other.referenceDate,other.sourceId,
                        other.sourceVersion,other.certainty,other.datePrecision,other.partial,
                        other.missingLibraryRefs);
    }
};
enum class FlagPolicy { Default, None, Embedded };
struct TerritorialSymbolStyle {
    FlagPolicy policy=FlagPolicy::Default;
    std::string embeddedDataUrl;
    // Original country default retained when a country becomes a subunit.
    // Independent of the editable override so reset can restore the default.
    std::string defaultCountryId;
    // Present (including empty) when conversion captured an explicit override.
    std::optional<std::string> defaultFlagDataUrl;
};
struct CountryDetails { std::string capital; };
struct PlaceLabel {
    std::string id, name, kind="custom", notes;
    GeometryRef geometry;
    std::optional<ObjectRef> territory;
    SourceProvenance source;
};
struct HydroFeature {
    std::string id, name, kind="river", notes;
    GeometryRef geometry;
    std::uint32_t color=0x3388cc;
    bool locked=false;
    SourceProvenance source;
    std::optional<std::string> sourceFeatureId;
};
struct DistributionLayer {
    std::string id, name, unit;
    std::uint32_t color=0x8c68d8;
    bool locked=false;
    std::optional<std::string> parentId;
    std::vector<std::string> groups;
    Validity validity;
    std::string metadata="{}";
    DistributionValueScale valueScale;
};
struct DistributionEntry {
    std::string id, layerId;
    std::optional<ObjectRef> territory;
    std::optional<GeometryRef> geometry;
    double value=0;
    std::string certainty="unknown", metadata="{}";
    Validity validity;
};
struct GenericFeature {
    std::string id, name, notes;
    GeometryRef geometry;
    std::uint32_t color=0x888888;
    bool locked=false, fallbackOnly=true;
    SourceProvenance source;
};
struct PhysicalDataSettings {
    std::string dataset, version, source;
    std::vector<std::string> hiddenHydroIds;
};
struct TerritorialUnit {
    std::string id, name, notes;
    UnitKind kind=UnitKind::General;
    bool locked=false;
    // Country base name is immutable source data; name is the current override.
    std::string baseName;
    bool nameExplicit=true;
    std::optional<LibraryOrigin> libraryOrigin;
    // Additional supported annotation metadata, lossless through the codec.
    std::string metadata="{}", sourceFolderId, sourceEntityId, sourceGeometryVersion;
};
struct Layer {
    std::string id, name;
    bool visible=true, locked=false;
    double opacity=1;
};
struct ObjectStyle { std::uint32_t color=0xa8c7db; double opacity=1; bool explicitColor=true; };
struct PresentationState {
    WebPresentation webPresentation;
    std::vector<Layer> userLayers; // bottom to top; independent of territorial ancestry
    std::map<ObjectRef,std::string> membership;
    std::map<ObjectRef,ObjectStyle> objectStyles;
};
struct PreservedExtension {
    std::string id, sourceFormat="pandoeditor-project";
    int sourceSchema=3;
    std::string jsonPointer, payload;
    std::string status="unsupported", dependencyKnowledge="unknown";
    std::vector<ObjectRef> dependencies;
    std::vector<std::string> forbiddenEffects;
    // Unknown envelope fields are retained by the codec too.
    std::string envelopeExtras="{}";
};
// Legacy input DTO only. Never stored alongside the canonical units.
struct Country {
    std::string id, name;
    MultiPolygon polygons;
    std::uint32_t color;
    std::string memo;
    double opacity=1;
    std::string layerId="countries";
    bool locked=false;
};
struct ProjectDocument {
    std::string documentId;
    // Supported file header, source and physical-view annotations. The codec
    // owns this lossless JSON object; temporal/content facts remain typed below.
    std::string exchangeMetadata="{}";
    std::vector<TerritorialUnit> units;
    std::map<ObjectRef,CountryDetails> countryDetails;
    std::map<ObjectRef,TerritorialSymbolStyle> symbols;
    std::vector<PlaceLabel> labels;
    std::vector<HydroFeature> hydro;
    std::vector<DistributionLayer> distributionLayers;
    std::vector<DistributionEntry> distributionEntries;
    std::vector<GenericFeature> genericFeatures;
    PhysicalDataSettings physicalData;
    GeometryStore geometries;
    TimelineRecords timelineRecords;
    PresentationState presentation;
    std::vector<PreservedExtension> extensions;
    ProjectDocument()=default;
    ProjectDocument(std::vector<Country> countries,std::vector<Layer> layers);
};
// References into the canonical document: no second editable copy, including geometry.
struct CountryView {
    const std::string &id, &name;
    const MultiPolygon& polygons;
    const std::uint32_t& color;
    const std::string& memo;
    const double& opacity;
    const std::string& layerId;
    const bool& locked;
};
struct DocumentIndex {
    std::map<ObjectRef,std::size_t> objects;
    std::map<std::string,std::size_t> layers;
    std::map<ObjectRef,std::vector<ObjectRef>> children, dependents;
    std::map<ObjectRef,std::vector<std::size_t>> parentRelationsByUnit;
    std::map<GeometryRef,std::vector<ObjectRef>> geometryUsers;
};
DocumentIndex validateDocument(const ProjectDocument& document);
void indexContent(const ProjectDocument&, DocumentIndex&);
bool sameContent(const ProjectDocument&, const ProjectDocument&);
std::optional<GeometryRef> objectGeometry(const ProjectDocument&,const DocumentIndex&,const ObjectRef&);
bool objectLocked(const ProjectDocument&,const DocumentIndex&,const ObjectRef&);
std::string contentGroup(const ProjectDocument&,const ObjectRef&);
std::vector<CountryView> countryViews(const ProjectDocument& document);
const std::string& nativeLayerId(const ProjectDocument&,const ObjectRef&);
std::vector<TimelineEntityIdentity> timelineEntityCatalog(const ProjectDocument&);
bool isStaticTimeline(const ProjectDocument&);
void requireStaticTimeline(const ProjectDocument&);
const TimelineGeometryBinding& staticGeometryBinding(const ProjectDocument&,const std::string& entityId);
TimelineGeometryBinding& staticGeometryBinding(ProjectDocument&,const std::string& entityId);
const TimelineParentRelation& staticParentRelation(const ProjectDocument&,const std::string& entityId);
TimelineParentRelation& staticParentRelation(ProjectDocument&,const std::string& entityId);
const TimelineLifetime& staticLifetime(const ProjectDocument&,const std::string& entityId);
TimelineLifetime& staticLifetime(ProjectDocument&,const std::string& entityId);
void addStaticTerritorialRecords(ProjectDocument&,const std::string& entityId,GeometryRef,
                                 const std::string& parentId="",const std::string& coverageMode="explicit");
void removeTerritorialRecords(ProjectDocument&,const std::vector<std::string>& entityIds);
bool isRootGeneral(const ProjectDocument&,const TerritorialUnit&);
std::vector<std::string> blockingExtensions(const ProjectDocument&, const ObjectRef&, const std::string& effect);
bool effectAllowed(const ProjectDocument&, const ObjectRef&, const std::string& effect);
} // namespace pandoeditor
