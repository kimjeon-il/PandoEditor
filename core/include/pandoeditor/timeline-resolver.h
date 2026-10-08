#pragma once
#include <pandoeditor/timeline-records.h>

namespace pandoeditor {
struct ResolvedEntity {
    std::string id;
    GeometryRef geometryRef;
    std::string parentId, coverageMode, rootId;
    std::vector<std::string> ancestors;
};
struct ResolvedWorld {
    std::string month;
    std::vector<ResolvedEntity> entities;
    const ResolvedEntity* find(const std::string& id) const;
};

// The input catalog supplies stable identity order; records and geometry are
// validated before the month-end view is assembled.
ResolvedWorld resolveWorld(const TimelineRecords&, const TimelineValidationContext&,
                           const std::string& month);
std::string initialTimelineMonth(const TimelineRecords&,const std::string& currentMonth);
}
