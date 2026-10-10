#include <pandoeditor/timeline-edit.h>
#include <pandoeditor/temporal.h>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <set>

namespace pandoeditor {
namespace {
bool active(const Validity& validity,const TemporalValue& point) {
    const auto dates=normalizeTemporalInterval(validity.from,validity.to);
    return (!dates.start || !(point.endKey<dates.start->startKey))
        && (!dates.end || !(dates.end->endKey<point.endKey));
}
std::string previousMonth(const TemporalValue& point) {
    int year=point.year,month=*point.month-1;
    if(!month){month=12;year=year==1?-1:year-1;}
    std::ostringstream out;
    if(year<0)out<<'-';else if(year>9999)out<<'+';
    out<<std::setw(4)<<std::setfill('0')<<std::abs(year)<<'-'<<std::setw(2)<<month;
    return out.str();
}
std::string nextRecordId(const TimelineRecords& rows,const std::string& base) {
    std::set<std::string> ids;
    for(const auto& row:rows.lifetimes)ids.insert(row.id);
    for(const auto& row:rows.geometryBindings)ids.insert(row.id);
    for(const auto& row:rows.parentRelations)ids.insert(row.id);
    auto result=base;
    for(unsigned suffix=1;ids.count(result);++suffix)result=base+":"+std::to_string(suffix);
    return result;
}
template<class Rows,class Change> TimelineRecords replace(const TimelineRecords& original,Rows TimelineRecords::* field,
        const std::string& id,const std::string& cursor,Change change) {
    const auto month=parseTemporal(cursor);
    if(month.precision!="month")throw TimelineError("INVALID_TIMELINE_CURSOR","month precision required");
    const auto point=temporalMonthEnd(month.canonical);
    auto result=original;
    auto& rows=result.*field;
    const auto it=std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.entityId==id&&active(row.validity,point);});
    if(it==rows.end())throw TimelineError("TIMELINE_INACTIVE","entity has no record at month end");
    auto changed=*it;
    if(!change(changed))return original;
    const bool split=!it->validity.from || parseTemporal(*it->validity.from).startKey<month.startKey;
    if(!split){*it=std::move(changed);return result;}
    const auto index=std::distance(rows.begin(),it);
    changed.id=nextRecordId(original,it->id+":"+month.canonical);
    changed.validity.from=month.canonical;
    rows[index].validity.to=previousMonth(month);
    rows.insert(rows.begin()+index+1,std::move(changed));
    return result;
}
}
TimelineRecords replaceGeometryBindingAtMonth(const TimelineRecords& rows,const std::string& id,
                                               const std::string& month,GeometryRef next) {
    return replace(rows,&TimelineRecords::geometryBindings,id,month,[&](auto& row){
        if(row.geometryRef==next)return false;row.geometryRef=std::move(next);return true;
    });
}
TimelineRecords replaceParentRelationAtMonth(const TimelineRecords& rows,const std::string& id,
                                              const std::string& month,const std::string& parentId,
                                              const std::string& coverageMode) {
    return replace(rows,&TimelineRecords::parentRelations,id,month,[&](auto& row){
        if(row.parentId==parentId&&row.coverageMode==coverageMode)return false;
        row.parentId=parentId;row.coverageMode=coverageMode;return true;
    });
}
TimelineRecords truncateTimelineEntityAtMonth(const TimelineRecords& original,const std::string& id,
                                              const std::string& cursor) {
    const auto month=parseTemporal(cursor);
    if(month.precision!="month")throw TimelineError("INVALID_TIMELINE_CURSOR","month precision required");
    const auto lastMonth=previousMonth(month);
    auto result=original;
    const auto truncate=[&](auto& rows) {
        using Row=typename std::decay_t<decltype(rows)>::value_type;
        std::vector<Row> kept;
        kept.reserve(rows.size());
        for(auto row:rows) {
            if(row.entityId!=id){kept.push_back(std::move(row));continue;}
            const auto interval=normalizeTemporalInterval(row.validity.from,row.validity.to);
            if(interval.end&&interval.end->endKey<month.startKey){kept.push_back(std::move(row));continue;}
            if(interval.start&&!(interval.start->startKey<month.startKey))continue;
            row.validity.to=lastMonth;
            kept.push_back(std::move(row));
        }
        rows=std::move(kept);
    };
    truncate(result.lifetimes);truncate(result.geometryBindings);truncate(result.parentRelations);
    return result;
}
}
