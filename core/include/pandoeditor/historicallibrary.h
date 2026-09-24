#pragma once
#include <pandoeditor/document.h>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pandoeditor {
constexpr int historicalLibrarySchemaVersion=2;
struct HistoricalGeometryVersion {
    std::string id;
    Validity validity;
    Geometry geometry; // Materialized Polygon/MultiPolygon owned by the catalog.
    std::string datePrecision="unknown", certainty="unknown", sourceId, notes;
};
struct HistoricalInstantiationPolicy {
    std::string mode="independent";
    std::map<std::string,std::string> countryNameUpdates;
};
struct HistoricalEntity {
    std::string libraryId;
    UnitKind type=UnitKind::Country;
    std::string canonicalName;
    std::map<std::string,std::string> displayNames;
    std::vector<std::string> alternateNames;
    Validity validity;
    std::string parentLibraryId, sovereignLibraryId;
    std::vector<HistoricalGeometryVersion> geometryVersions;
    HistoricalInstantiationPolicy instantiation;
    std::string metadata="{}", sourceInfo="{}", geographicRegion;
};
struct WorldSnapshot {
    std::string id,name;
    std::optional<std::string> referenceDate;
    std::vector<std::string> entityRefs;
    std::string metadata="{}",sourceInfo="{}";
};
struct HistoricalSelection {
    std::string libraryId,geometryVersionId,name;
    UnitKind type=UnitKind::Country;
    Geometry geometry; // A copy; editing this never changes the catalog.
    Validity validity;
    std::string parentLibraryId,sovereignLibraryId;
    HistoricalInstantiationPolicy instantiation;
    std::string metadata,sourceInfo,certainty,datePrecision,sourceId;
};
enum class HistoricalStatus { All, Current, Past };
struct HistoricalSearch {
    std::string query,type;
    HistoricalStatus status=HistoricalStatus::All;
    std::string referenceDate,geographicRegion;
};
UnitKind historicalUnitKind(const std::string& raw);
class HistoricalLibrary {
public:
    HistoricalLibrary(int schemaVersion,std::vector<HistoricalEntity> entities,
                      std::vector<WorldSnapshot> snapshots);
    const HistoricalEntity* get(const std::string& id) const;
    const WorldSnapshot* getSnapshot(const std::string& id) const;
    std::vector<const HistoricalEntity*> list() const;
    std::vector<const HistoricalEntity*> search(const HistoricalSearch& options) const;
    const HistoricalGeometryVersion* selectGeometryVersion(const std::string& libraryId,
                                            const std::string& referenceDate) const;
    HistoricalSelection instantiate(const std::string& libraryId,
                                    const std::string& referenceDate,
                                    const std::string& geometryVersionId="") const;
private:
    std::vector<HistoricalEntity> entities_;
    std::vector<WorldSnapshot> snapshots_;
    std::map<std::string,std::size_t> entityIds_, snapshotIds_;
};
}
