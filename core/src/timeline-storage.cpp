#include <pandoeditor/timeline-storage.h>

namespace pandoeditor {
namespace {
TimelineRecords validated(const TimelineRecords& records,const GeometryStore& geometries,
                          const std::vector<TimelineEntityIdentity>& entities) {
    return normalizeTimelineRecords(records,{entities,[&geometries](const GeometryRef& ref) {
        const auto geometry=geometries.get(ref);
        return geometry && (geometry->type=="Polygon" || geometry->type=="MultiPolygon");
    }});
}
}

TimelineStorageSnapshot snapshotTimelineStorage(const TimelineRecords& records,const GeometryStore& geometries,
                                                const std::vector<TimelineEntityIdentity>& entities) {
    TimelineStorageSnapshot result;
    result.records=validated(records,geometries,entities);
    result.geometries.reserve(geometries.versions().size());
    // Preserve all versions, including versions used by other document domains.
    // Only immutable allocations are shared; the snapshot's registry is independent.
    for(const auto& [ref,geometry]:geometries.versions()) result.geometries.push_back({ref,geometry});
    return result;
}

TimelineStorage restoreTimelineStorage(const TimelineStorageSnapshot& input,
                                      const std::vector<TimelineEntityIdentity>& entities) {
    if(input.schemaVersion!=TimelineStorageSchemaVersion)
        throw TimelineError("TIMELINE_STORAGE_SCHEMA","Unsupported storage version.");
    TimelineStorage candidate;
    for(const auto& value:input.geometries) {
        if(!value.geometry) throw std::invalid_argument("INVALID_GEOMETRY: missing geometry allocation");
        // Reuse the canonical geometry owner's validation and duplicate-version rules.
        // Copy input coordinates so even an external mutable alias cannot alter the result.
        candidate.geometries.insert(value.ref,*value.geometry);
    }
    candidate.records=validated(input.records,candidate.geometries,entities);
    return candidate;
}
}
