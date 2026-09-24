#pragma once
#include <pandoeditor/historicallibrary.h>
#include <pandoeditor/geometryoperations.h>
#include <QByteArray>
#include <functional>

namespace pandoeditor {
struct HistoricalSourceVersion {
    HistoricalGeometryVersion version;
    bool direct=false;
    std::vector<std::string> memberCountryIds;
    std::optional<Geometry> includeGeometry,excludeGeometry;
};
struct HistoricalSourceEntity {
    HistoricalEntity entity;
    std::vector<HistoricalSourceVersion> versions;
};
struct HistoricalSource {
    std::vector<HistoricalSourceEntity> entities;
    std::vector<WorldSnapshot> snapshots;
};
struct HistoricalMaterialization {
    HistoricalLibrary library;
    std::vector<std::string> missingEntityIds,missingCountryIds;
};
HistoricalSource parseHistoricalLibrarySource(const QByteArray& bytes);
HistoricalMaterialization materializeHistoricalSource(
    const HistoricalSource& source,
    const std::function<std::optional<Geometry>(const std::string&)>& countryGeometry,
    const GeometryCalculator& calculator);
}
