#include <pandoeditor/timeline-resolver.h>
#include <pandoeditor/temporal.h>
#include <algorithm>

namespace pandoeditor {
namespace {
bool active(const Validity& validity,const TemporalValue& point) {
    const auto interval=normalizeTemporalInterval(validity.from,validity.to);
    return (!interval.start || !(point.endKey<interval.start->startKey))
        && (!interval.end || !(interval.end->endKey<point.endKey));
}
template<class Rows> const typename Rows::value_type* selected(const Rows& rows,const std::string& id,
                                                                  const TemporalValue& point) {
    const auto it=std::find_if(rows.begin(),rows.end(),[&](const auto& row){
        return row.entityId==id&&active(row.validity,point);
    });
    return it==rows.end()?nullptr:&*it;
}
}
const ResolvedEntity* ResolvedWorld::find(const std::string& id) const {
    const auto it=std::find_if(entities.begin(),entities.end(),[&](const auto& row){return row.id==id;});
    return it==entities.end()?nullptr:&*it;
}
std::string initialTimelineMonth(const TimelineRecords& records,const std::string& currentMonth) {
    const auto fallback=parseTemporal(currentMonth);
    if(fallback.precision!="month")throw TimelineError("INVALID_TIMELINE_CURSOR","month precision required");
    std::optional<TemporalValue> latest;
    const auto inspect=[&](const auto& rows){for(const auto& row:rows)if(row.validity.from) {
        const auto parsed=parseTemporal(*row.validity.from);
        if(!latest||latest->startKey<parsed.startKey)latest=parsed;
    }};
    inspect(records.lifetimes);inspect(records.geometryBindings);inspect(records.parentRelations);
    if(!latest)return fallback.canonical;
    if(latest->precision=="year")return latest->canonical+"-01";
    if(latest->precision=="date")return latest->canonical.substr(0,latest->canonical.size()-3);
    return latest->canonical;
}
ResolvedWorld resolveWorld(const TimelineRecords& records,const TimelineValidationContext& context,
                           const std::string& month) {
    const auto cursor=parseTemporal(month);
    if(cursor.precision!="month")throw TimelineError("INVALID_TIMELINE_CURSOR","month precision required");
    const auto point=temporalMonthEnd(cursor.canonical);
    const auto normalized=normalizeTimelineRecords(records,context);
    ResolvedWorld result;result.month=point.canonical;
    for(const auto& identity:context.entities) {
        if(!selected(normalized.lifetimes,identity.id,point))continue;
        const auto* geometry=selected(normalized.geometryBindings,identity.id,point);
        const auto* parent=selected(normalized.parentRelations,identity.id,point);
        result.entities.push_back({identity.id,geometry->geometryRef,parent->parentId,parent->coverageMode});
    }
    for(auto& entity:result.entities) {
        auto parent=entity.parentId;
        while(!parent.empty()) {
            entity.ancestors.push_back(parent);
            parent=result.find(parent)->parentId;
        }
        entity.rootId=entity.ancestors.empty()?entity.id:entity.ancestors.back();
    }
    return result;
}
}
