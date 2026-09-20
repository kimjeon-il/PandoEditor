#pragma once
#include <pandoeditor/presentation.h>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace pandoeditor {
using Ring = std::vector<Point>;
using Polygon = std::vector<Ring>;
using MultiPolygon = std::vector<Polygon>;
inline ObjectRef territorialRef(const std::string& id) { return {"territorial",id}; }
struct GeometryRef {
    std::string id;
    std::uint32_t version=1;
    bool operator<(const GeometryRef& b) const { return std::tie(id,version)<std::tie(b.id,b.version); }
    bool operator==(const GeometryRef& b) const { return id==b.id && version==b.version; }
};
struct Geometry {
    std::string type="MultiPolygon";
    std::vector<Point> points;
    std::vector<Ring> lines;
    MultiPolygon polygons;
};
class GeometryStore {
public:
    void insert(GeometryRef ref, Geometry geometry);
    std::shared_ptr<const Geometry> get(const GeometryRef& ref) const;
    const auto& versions() const { return versions_; }
private:
    std::map<GeometryRef,std::shared_ptr<const Geometry>> versions_;
};
enum class UnitKind { Country, Subunit, Region };
struct Validity { std::optional<std::string> from, to; };
struct SourceProvenance {
    std::string kind="user", dataset, version, sourceId, sourceFormat, sourceType, importedAt;
    // Lossless JSON object; parsing is owned by the codec boundary.
    std::string details="{}";
};
enum class FlagPolicy { Default, None, Embedded };
struct TerritorialSymbolStyle {
    FlagPolicy policy=FlagPolicy::Default;
    std::string embeddedDataUrl;
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
    std::string id, name, type="language";
    std::uint32_t color=0x3388cc;
    bool locked=false;
    std::optional<std::string> parentId;
    std::vector<std::string> groups;
    Validity validity;
    std::string metadata="{}";
};
struct DistributionEntry {
    std::string id, layerId;
    std::optional<ObjectRef> territory;
    std::optional<GeometryRef> geometry;
    double share=100;
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
struct TemporalValue {
    std::string text, precision;
    std::int64_t start, end;
};
TemporalValue parseTemporal(const std::string& text);
std::pair<std::int64_t,std::int64_t> temporalBounds(const Validity& validity);
struct TerritorialUnit {
    std::string id, name, notes;
    UnitKind kind=UnitKind::Country;
    GeometryRef geometry;
    bool locked=false;
    Validity validity;
    std::string coverageMode="explicit";
    // Country base name is immutable source data; name is the current override.
    std::string baseName;
    bool nameExplicit=true;
};
struct TerritorialRelation {
    std::string id;
    ObjectRef unit;
    std::optional<ObjectRef> parent, sovereign;
    bool dated=false;
    Validity validity;
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
};
struct ProjectDocument {
    // Read-time provenance for a migration notice, not document content or wire data.
    int nativeSourceVersion=6;
    std::string documentId;
    std::vector<TerritorialUnit> units;
    std::vector<TerritorialRelation> relations;
    std::map<ObjectRef,CountryDetails> countryDetails;
    std::map<ObjectRef,TerritorialSymbolStyle> symbols;
    std::vector<PlaceLabel> labels;
    std::vector<HydroFeature> hydro;
    std::vector<DistributionLayer> distributionLayers;
    std::vector<DistributionEntry> distributionEntries;
    std::vector<GenericFeature> genericFeatures;
    PhysicalDataSettings physicalData;
    GeometryStore geometries;
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
    std::map<ObjectRef,std::vector<ObjectRef>> sovereignMembers;
    std::map<ObjectRef,std::vector<std::size_t>> relationsByUnit;
    std::map<GeometryRef,std::vector<ObjectRef>> geometryUsers;
};
DocumentIndex validateDocument(const ProjectDocument& document);
void indexContent(const ProjectDocument&, DocumentIndex&);
bool sameContent(const ProjectDocument&, const ProjectDocument&);
std::vector<ObjectRef> dominantDistributionEntries(const ProjectDocument&, const std::vector<std::string>& visibleLayers);
std::optional<GeometryRef> objectGeometry(const ProjectDocument&,const DocumentIndex&,const ObjectRef&);
bool objectLocked(const ProjectDocument&,const DocumentIndex&,const ObjectRef&);
std::string contentGroup(const ProjectDocument&,const ObjectRef&);
std::vector<CountryView> countryViews(const ProjectDocument& document);
const std::string& nativeLayerId(const ProjectDocument&,const ObjectRef&);
const TerritorialRelation* effectiveRelation(const ProjectDocument&, const std::string& unitId, std::int64_t date);
std::vector<std::string> blockingExtensions(const ProjectDocument&, const ObjectRef&, const std::string& effect);
bool effectAllowed(const ProjectDocument&, const ObjectRef&, const std::string& effect);
} // namespace pandoeditor
