#include "hydrometadata.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cmath>
#include <limits>

namespace {
bool unsignedId(const QJsonValue& value,quint32& output) {
    if(!value.isDouble())return false;
    const double number=value.toDouble();
    if(!std::isfinite(number)||number<0||number>std::numeric_limits<quint32>::max()||std::floor(number)!=number)return false;
    output=static_cast<quint32>(number);return true;
}
bool payload(const QByteArray& bytes,QJsonArray& features,QString& error) {
    QJsonParseError parseError;
    const auto document=QJsonDocument::fromJson(bytes,&parseError);
    if(parseError.error!=QJsonParseError::NoError||!document.isObject()||
       document.object().value("version").toInt(-1)!=5||
       !document.object().value("features").isArray()){
        error=QStringLiteral("수계 metadata v5 형식이 올바르지 않습니다.");return false;
    }
    features=document.object().value("features").toArray();return true;
}
bool optionalString(const QJsonObject& object,const char* key,QString& output) {
    const auto value=object.value(QLatin1String(key));
    if(!value.isUndefined()&&!value.isNull()&&!value.isString())return false;
    output=value.toString();return true;
}
}
bool parseHydroCoreMetadata(const QByteArray& bytes,int expectedCount,
                            HydroMetadata& output,QString& error) {
    QJsonArray rows;
    if(!payload(bytes,rows,error))return false;
    if(rows.size()!=expectedCount){error=QStringLiteral("수계 metadata feature 수가 다릅니다.");return false;}
    HydroMetadata parsed;
    for(const auto& value:rows) {
        if(!value.isObject()){error=QStringLiteral("수계 metadata feature가 올바르지 않습니다.");return false;}
        const auto row=value.toObject();HydroMetadataRecord record;
        const auto bounds=row.value("bounds").toArray();
        if(!unsignedId(row.value("fid"),record.fid)||
           !unsignedId(row.value("logicalFid"),record.logicalFid)||
           !row.value("awId").isString()||!row.value("name").isString()||
           !row.value("layerId").isString()||!row.value("category").isString()||
           row.value("awId").toString().isEmpty()||row.value("layerId").toString().isEmpty()||
           bounds.size()!=4||parsed.contains(record.fid)||
           !optionalString(row,"systemId",record.systemId)||
           !optionalString(row,"role",record.role)){
            error=QStringLiteral("수계 metadata feature 식별자가 올바르지 않습니다.");return false;
        }
        record.awId=row.value("awId").toString();record.name=row.value("name").toString();
        record.layerId=row.value("layerId").toString();record.category=row.value("category").toString();
        for(const auto& coordinate:bounds){
            if(!coordinate.isDouble()||!std::isfinite(coordinate.toDouble())||
               std::floor(coordinate.toDouble())!=coordinate.toDouble()||
               std::abs(coordinate.toDouble())>180000000.){
                error=QStringLiteral("수계 metadata bounds가 올바르지 않습니다.");return false;
            }
            record.bounds.append(coordinate.toDouble()/1000000.);
        }
        if(record.bounds[0]<-180||record.bounds[2]>180||record.bounds[1]<-90||
           record.bounds[3]>90||record.bounds[0]>record.bounds[2]||record.bounds[1]>record.bounds[3]){
            error=QStringLiteral("수계 metadata bounds 순서가 올바르지 않습니다.");return false;
        }
        parsed.insert(record.fid,std::move(record));
    }
    output.swap(parsed);return true;
}
bool mergeHydroDetailMetadata(const QByteArray& bytes,HydroMetadata& output,QString& error) {
    QJsonArray rows;
    if(!payload(bytes,rows,error))return false;
    HydroMetadata merged=output;
    QSet<quint32> seen;
    for(const auto& value:rows) {
        const auto row=value.toObject();quint32 fid=0;QString source,sourceId;
        if(!value.isObject()||!unsignedId(row.value("fid"),fid)||
           !merged.contains(fid)||seen.contains(fid)||
           !optionalString(row,"source",source)||!optionalString(row,"sourceId",sourceId)){
            error=QStringLiteral("수계 상세 metadata 식별자가 올바르지 않습니다.");return false;
        }
        seen.insert(fid);
        merged[fid].source=source;merged[fid].sourceId=sourceId;
    }
    output.swap(merged);return true;
}
