#pragma once
#include <pandoeditor/timeline-records.h>

namespace pandoeditor {
inline constexpr std::uint32_t TimelineStorageSchemaVersion = 1;

// A snapshot shares only the existing store's immutable geometry allocations.
// File codecs materialize geometry values before constructing a restore candidate.
struct TimelineGeometrySnapshot {
    GeometryRef ref;
    std::shared_ptr<const Geometry> geometry;
};
struct TimelineStorageSnapshot {
    std::uint32_t schemaVersion = TimelineStorageSchemaVersion;
    TimelineRecords records;
    std::vector<TimelineGeometrySnapshot> geometries;
};
struct TimelineStorage {
    TimelineRecords records;
    GeometryStore geometries;
};

// No live project, history stack or serializer is mutated by these functions.
TimelineStorageSnapshot snapshotTimelineStorage(const TimelineRecords&, const GeometryStore&,
                                                const std::vector<TimelineEntityIdentity>&);
TimelineStorage restoreTimelineStorage(const TimelineStorageSnapshot&,
                                      const std::vector<TimelineEntityIdentity>&);
}
