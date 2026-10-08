#pragma once
#include <pandoeditor/timeline-records.h>

namespace pandoeditor {
TimelineRecords replaceGeometryBindingAtMonth(const TimelineRecords&,const std::string& entityId,
                                               const std::string& month,GeometryRef next);
TimelineRecords replaceParentRelationAtMonth(const TimelineRecords&,const std::string& entityId,
                                              const std::string& month,const std::string& parentId,
                                              const std::string& coverageMode);
TimelineRecords truncateTimelineEntityAtMonth(const TimelineRecords&,const std::string& entityId,
                                              const std::string& month);
}
