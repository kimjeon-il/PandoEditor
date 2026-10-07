#include "territoriallibrarycatalog.h"
#include "losslessjson.h"
#include "webjson.h"
#include <pandoeditor/geometry-types.h>
#include <pandoeditor/temporal.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringConverter>
#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pandoeditor {
namespace {
constexpr qint64 assetLimit=64ll*1024*1024;
void require(bool okay,const char* message) {
    if(!okay)throw std::invalid_argument(message);
}
QString text(const QJsonValue& value) {return webjson::jsTrim(value.toString());}
bool matches(const QString& value,const char* pattern) {
    return QRegularExpression(QString::fromLatin1(pattern)).match(value).hasMatch();
}
QJsonObject object(const QJsonValue& value,const char* message) {
    require(value.isObject(),message);return value.toObject();
}
QJsonArray array(const QJsonValue& value,const char* message) {
    require(value.isArray(),message);return value.toArray();
}
QJsonObject parseObject(const QByteArray& bytes) {
    require(!bytes.isEmpty()&&bytes.size()<=assetLimit,"PL-LIB-SIZE: invalid JSON asset size");
    QStringDecoder utf8(QStringDecoder::Utf8,QStringConverter::Flag::Stateless);const QString decodedUtf8=utf8(bytes);
    require(!utf8.hasError()&&!decodedUtf8.isEmpty(),"PL-LIB-JSON: invalid UTF-8");
    // QJsonDocument alone accepts duplicate object keys. Keep this boundary as
    // strict as the native exchange reader before projecting into Qt JSON.
    const auto lossless=losslessjson::parse(bytes);
    require(lossless.kind==losslessjson::Value::Object,"PL-LIB-JSON: object required");
    QJsonParseError error;const auto parsed=QJsonDocument::fromJson(bytes,&error);
    require(error.error==QJsonParseError::NoError&&parsed.isObject(),"PL-LIB-JSON: invalid document");
    return parsed.object();
}
void validateNames(const QJsonValue& value) {
    const auto names=object(value,"PL-LIB-NAMES: names object required");
    require(!names.isEmpty(),"PL-LIB-NAMES: names required");
    for(auto it=names.begin();it!=names.end();++it)
        require(it.value().isString()&&!text(it.value()).isEmpty(),"PL-LIB-NAMES: nonempty string required");
}
TemporalInterval interval(const QJsonObject& value) {
    require(value.contains("validFrom")&&value.contains("validTo"),"PL-LIB-DATE: both interval endpoints required");
    const auto endpoint=[](const QJsonValue& input)->std::optional<std::string> {
        if(input.isNull())return std::nullopt;
        require(input.isString(),"PL-LIB-DATE: endpoint must be string or null");
        return parseTemporal(input.toString().toStdString()).canonical;
    };
    return normalizeTemporalInterval(endpoint(value.value("validFrom")),endpoint(value.value("validTo")));
}
QJsonObject canonicalInterval(const QJsonObject& value) {
    const auto parsed=interval(value);
    return {{"validFrom",parsed.validFrom?QJsonValue(QString::fromStdString(*parsed.validFrom)):QJsonValue::Null},
            {"validTo",parsed.validTo?QJsonValue(QString::fromStdString(*parsed.validTo)):QJsonValue::Null}};
}
bool existsAt(const QJsonObject& lifetime,const TemporalValue& point) {
    const auto limits=interval(lifetime);
    // Fixed Web's coarse cursor is END of year/month, including its upper bound.
    return (!limits.start||compareTemporal(*limits.start,point,TemporalBoundary::Start,TemporalBoundary::End)<=0)
        &&(!limits.end||compareTemporal(*limits.end,point,TemporalBoundary::End,TemporalBoundary::End)>=0);
}
QJsonObject selectVersion(const QJsonObject& entity,const TemporalValue& point) {
    if(!existsAt(entity.value("lifetime").toObject(),point))return {};
    QJsonObject selected;
    for(const auto& raw:entity.value("geometryVersions").toArray()) {
        const auto version=raw.toObject();if(!existsAt(version,point))continue;
        require(selected.isEmpty(),"PL-LIB-GEOMETRY: ambiguous date selection");selected=version;
    }
    return selected;
}
void validateVersions(const QJsonArray& versions,bool indexed) {
    require(!versions.isEmpty(),"PL-LIB-GEOMETRY: missing geometry versions");
    std::set<QString> ids;std::vector<TemporalInterval> intervals;
    for(const auto& raw:versions) {
        const auto version=object(raw,"PL-LIB-GEOMETRY: version object required");
        const auto id=text(version.value("versionId"));
        require(version.value("versionId").isString()&&!id.isEmpty()&&ids.insert(id).second&&!version.contains("id"),
                "PL-LIB-GEOMETRY: duplicate/retired version identity");
        require(!indexed||!version.contains("geometry"),"PL-LIB-GEOMETRY: catalog cannot inline geometry");
        const auto validity=interval(version);
        for(const auto& previous:intervals)
            require(!temporalIntervalsOverlap(previous,validity),"PL-LIB-GEOMETRY: overlapping geometry intervals");
        intervals.push_back(validity);
    }
}
void validateRelations(const QJsonValue& input,const std::set<QString>* entityIds=nullptr) {
    std::set<QByteArray> seen;
    for(const auto& raw:array(input,"PL-LIB-LINEAGE: relations required")) {
        const auto relation=object(raw,"PL-LIB-LINEAGE: relation object required");
        const auto from=relation.value("from").toString(),to=relation.value("to").toString();
        const auto key=QJsonDocument(QJsonArray{relation.value("type"),from,to}).toJson(QJsonDocument::Compact);
        require(relation.value("type")==QStringLiteral("successor")&&!from.isEmpty()&&!to.isEmpty()&&from!=to&&seen.insert(key).second,
                "PL-LIB-LINEAGE: invalid successor relation");
        require(!entityIds||(entityIds->count(from)&&entityIds->count(to)),"PL-LIB-LINEAGE: missing relation endpoint");
    }
}
qint64 byteCount(const QJsonValue& value) {
    const auto number=value.toDouble(-1);
    require(value.isDouble()&&std::isfinite(number)&&number>0&&number<=assetLimit&&number==std::floor(number),
            "PL-LIB-SIZE: invalid asset length");
    return qint64(number);
}
void validateEntityIdentity(const QJsonObject& entity,bool indexed) {
    require(entity.value("schemaVersion")==2&&matches(entity.value("entityId").toString(),"^[a-z]+:[A-Za-z0-9_-]+$"),
            "PL-LIB-SCHEMA: invalid schema or entity identity");
    const auto kind=entity.value("entityKind").toString();
    require(kind=="general"||kind=="regional","PL-LIB-SCHEMA: invalid entity kind");
    for(const auto* retired:{"canonicalName","displayNames","libraryId","isHistorical","isCurrent","startDate","endDate"})
        require(!entity.contains(retired),"PL-LIB-SCHEMA: retired catalog fields");
    validateNames(entity.value("names"));
    interval(object(entity.value("lifetime"),"PL-LIB-DATE: lifetime required"));
    validateVersions(array(entity.value("geometryVersions"),"PL-LIB-GEOMETRY: versions required"),indexed);
    require(entity.value("parentEntityId").isString(),"PL-LIB-PARENT: parent string required");
}
void validateGeometry(const QJsonObject& raw) {
    require(raw.size()==2&&raw.contains("type")&&raw.contains("coordinates"),"PL-LIB-GEOMETRY: geometry fields");
    Geometry geometry;geometry.type=raw.value("type").toString().toStdString();
    const auto polygon=[](const QJsonValue& input) {
        Polygon result;
        for(const auto& boundary:array(input,"PL-LIB-GEOMETRY: polygon array required")) {
            Ring ring;
            for(const auto& position:array(boundary,"PL-LIB-GEOMETRY: ring array required")) {
                const auto point=array(position,"PL-LIB-GEOMETRY: coordinate required");
                require(point.size()==2&&point[0].isDouble()&&point[1].isDouble(),"PL-LIB-GEOMETRY: numeric coordinate pair required");
                ring.push_back({point[0].toDouble(),point[1].toDouble()});
            }
            result.push_back(std::move(ring));
        }
        return result;
    };
    if(geometry.type=="Polygon")geometry.polygons.push_back(polygon(raw.value("coordinates")));
    else if(geometry.type=="MultiPolygon")
        for(const auto& item:array(raw.value("coordinates"),"PL-LIB-GEOMETRY: multipolygon required"))geometry.polygons.push_back(polygon(item));
    else throw std::invalid_argument("PL-LIB-GEOMETRY: Polygon/MultiPolygon required");
    GeometryStore validator;validator.insert({"catalog-source-validation",1},std::move(geometry));
}
QJsonObject normalizeEntity(const QJsonObject& raw) {
    validateEntityIdentity(raw,false);
    QJsonArray aliases;std::set<QString> seenAliases;
    for(const auto& value:raw.value("alternateNames").toArray()) {
        const auto name=text(value);if(!name.isEmpty()&&seenAliases.insert(name).second)aliases.push_back(name);
    }
    QJsonArray versions;
    for(const auto& rawVersion:raw.value("geometryVersions").toArray()) {
        auto version=rawVersion.toObject();validateGeometry(object(version.value("geometry"),"PL-LIB-GEOMETRY: missing geometry"));
        const auto validity=canonicalInterval(version);version["versionId"]=text(version.value("versionId"));
        version["validFrom"]=validity.value("validFrom");version["validTo"]=validity.value("validTo");versions.push_back(version);
    }
    const auto instantiation=raw.contains("instantiation")?object(raw.value("instantiation"),"PL-LIB-MODE: instantiation object required")
        :QJsonObject{{"mode","independent"},{"countryUpdates",QJsonObject{}}};
    require(instantiation.value("mode")==QStringLiteral("independent")||instantiation.value("mode")==QStringLiteral("territory-replacement"),
            "PL-LIB-MODE: unsupported instantiation");
    QJsonObject normalized{{"schemaVersion",2},{"entityId",text(raw.value("entityId"))},{"entityKind",raw.value("entityKind")},
        {"names",raw.value("names")},{"alternateNames",aliases},{"lifetime",canonicalInterval(raw.value("lifetime").toObject())},
        {"parentEntityId",text(raw.value("parentEntityId"))},{"geometryVersions",versions},{"instantiation",instantiation},
        {"metadata",raw.value("metadata").toObject()},{"sourceInfo",raw.value("sourceInfo").toObject()}};
    if(!raw.value("lineageId").toString().isEmpty())normalized["lineageId"]=raw.value("lineageId");
    return normalized;
}
QByteArray decodeGzip(const QByteArray& bytes,qint64 expected) {
    QByteArray decoded(qsizetype(expected+1),Qt::Uninitialized);
    z_stream stream{};stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(bytes.constData()));
    stream.avail_in=uInt(bytes.size());stream.next_out=reinterpret_cast<Bytef*>(decoded.data());stream.avail_out=uInt(decoded.size());
    require(inflateInit2(&stream,MAX_WBITS+16)==Z_OK,"PL-LIB-DECODE: gzip decoder initialization");
    const int result=inflate(&stream,Z_FINISH);const auto actual=stream.total_out;const auto remaining=stream.avail_in;
    inflateEnd(&stream);
    // zlib validates gzip CRC/ISIZE. Exact bounded output and complete input
    // consumption also reject truncation, extra bytes and expansion overrun.
    require(result==Z_STREAM_END&&actual==uLong(expected)&&remaining==0,"PL-LIB-DECODE: gzip CRC or decoded length mismatch");
    decoded.resize(qsizetype(expected));return decoded;
}
QJsonObject comparableMetadata(const QJsonObject& entity,bool chunk) {
    // Fixed Web deliberately omits sourceInfo here: actual chunks carry richer
    // source records than the index. Their stored SHA protects those records.
    QJsonObject metadata;
    for(const auto* key:{"schemaVersion","entityId","lineageId","entityKind","names","alternateNames","lifetime","parentEntityId","instantiation","metadata"})
        metadata[key]=entity.value(key);
    QJsonArray versions;
    for(const auto& raw:entity.value("geometryVersions").toArray()) {
        auto version=raw.toObject();if(chunk)version.remove("geometry");versions.push_back(version);
    }
    metadata["geometryVersions"]=versions;return metadata;
}
QString displayName(const QJsonObject& names) {
    for(const auto* language:{"ko","en"})if(!names.value(language).toString().isEmpty())return names.value(language).toString();
    return names.isEmpty()?QString{}:names.begin().value().toString();
}
}

TerritorialLibraryCatalog::TerritorialLibraryCatalog(const QByteArray& bytes,const QByteArray& expectedSha256,
                                                     const QString& root,Reader reader)
    :root_(root),reader_(std::move(reader)) {
    require(matches(QString::fromLatin1(expectedSha256),"^[a-fA-F0-9]{64}$"),"PL-LIB-HASH: index integrity pin required");
    require(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()==expectedSha256.toLower(),"PL-LIB-HASH: index integrity mismatch");
    const auto candidate=parseObject(bytes);
    require(candidate.value("schemaVersion")==2,"PL-LIB-SCHEMA: catalog schemaVersion2 required");
    std::set<QString> ids;
    for(const auto& raw:array(candidate.value("entities"),"PL-LIB-SCHEMA: entities required")) {
        const auto entity=object(raw,"PL-LIB-SCHEMA: entity object required");validateEntityIdentity(entity,true);
        const auto id=entity.value("entityId").toString();require(ids.insert(id).second,"PL-LIB-SCHEMA: duplicate entity identity");
        auto file=id;file.replace(':','-');file+=".json.gz";
        require(entity.value("file")==file,"PL-LIB-PATH: entity chunk file mismatch");
        require(matches(entity.value("sha256").toString(),"^[a-f0-9]{64}$"),"PL-LIB-HASH: stored chunk hash required");
        byteCount(entity.value("compressedBytes"));byteCount(entity.value("decodedBytes"));
        require(entity.value("geometryVersionCount").toDouble(-1)==entity.value("geometryVersions").toArray().size(),"PL-LIB-GEOMETRY: indexed version count mismatch");
        const auto bounds=array(entity.value("bbox"),"PL-LIB-GEOMETRY: bounds required");require(bounds.size()==4,"PL-LIB-GEOMETRY: four bounds required");
        for(const auto& value:bounds)require(value.isDouble()&&std::isfinite(value.toDouble()),"PL-LIB-GEOMETRY: finite bounds required");
        const auto lifetime=entity.value("lifetime").toObject();
        require(entity.value("validFrom")==lifetime.value("validFrom")&&entity.value("validTo")==lifetime.value("validTo"),"PL-LIB-DATE: indexed lifetime mismatch");
        entries_.emplace(id,entity);
    }
    std::set<QString> lineageIds,membership;
    for(const auto& raw:array(candidate.value("lineages"),"PL-LIB-LINEAGE: lineages required")) {
        const auto lineage=object(raw,"PL-LIB-LINEAGE: lineage object required");const auto id=lineage.value("lineageId").toString();
        require(matches(id,"^[a-z0-9][a-z0-9_-]*$")&&lineageIds.insert(id).second,"PL-LIB-LINEAGE: duplicate/invalid lineage identity");
        validateNames(lineage.value("names"));validateRelations(lineage.value("relations"),&ids);
        const auto refs=array(lineage.value("entityRefs"),"PL-LIB-LINEAGE: entityRefs required");require(!refs.isEmpty(),"PL-LIB-LINEAGE: empty lineage");
        for(const auto& ref:refs) {
            const auto entityId=ref.toString();require(ids.count(entityId)&&membership.insert(entityId).second&&entries_.at(entityId).value("lineageId")==id,
                "PL-LIB-LINEAGE: missing/duplicate lineage membership");
        }
    }
    require(membership.size()==ids.size(),"PL-LIB-LINEAGE: missing lineage membership");
    for(const auto& [id,entity]:entries_) {
        auto parent=entity.value("parentEntityId").toString();std::set<QString> seen{id};
        while(!parent.isEmpty()) {
            require(ids.count(parent)&&seen.insert(parent).second,"PL-LIB-PARENT: missing or cyclic catalog parent");
            parent=entries_.at(parent).value("parentEntityId").toString();
        }
    }
    std::set<QString> snapshotIds;
    for(const auto& raw:array(candidate.value("snapshots"),"PL-LIB-SNAPSHOT: snapshots required")) {
        const auto snapshot=object(raw,"PL-LIB-SNAPSHOT: snapshot object required");const auto id=snapshot.value("id").toString();
        require(snapshot.value("schemaVersion")==1&&!id.isEmpty()&&snapshotIds.insert(id).second,"PL-LIB-SNAPSHOT: invalid snapshot identity");
        parseTemporal(snapshot.value("referenceDate").toString().toStdString());std::set<QString> seen;
        for(const auto& ref:array(snapshot.value("entityRefs"),"PL-LIB-SNAPSHOT: entityRefs required"))
            require(ids.count(ref.toString())&&seen.insert(ref.toString()).second,"PL-LIB-SNAPSHOT: invalid snapshot entity reference");
    }
    index_=candidate;
}
QJsonObject TerritorialLibraryCatalog::entry(const QString& id) const {
    const auto found=entries_.find(id);require(found!=entries_.end(),"PL-LIB-ENTITY: unknown entity");return found->second;
}
QJsonArray TerritorialLibraryCatalog::search(const QString& query,const QString& date) const {
    const auto point=parseTemporal(date.toStdString());const auto needle=webjson::jsTrim(query).toLower();QJsonArray groups;
    const auto namesMatch=[&](const QJsonObject& names) {
        for(auto it=names.begin();it!=names.end();++it)if(it.value().toString().toLower().contains(needle))return true;return false;
    };
    for(const auto& raw:lineages()) {
        const auto lineage=raw.toObject();const bool groupMatch=needle.isEmpty()||namesMatch(lineage.value("names").toObject());QJsonArray entities;
        for(const auto& ref:lineage.value("entityRefs").toArray()) {
            auto entity=entry(ref.toString());const auto lifetime=entity.value("lifetime").toObject();
            if(lifetime.value("validFrom").isNull()&&lifetime.value("validTo").isNull())continue;
            if(!existsAt(lifetime,point))continue;
            bool entityMatch=groupMatch||namesMatch(entity.value("names").toObject());
            for(const auto& alias:entity.value("alternateNames").toArray())entityMatch=entityMatch||alias.toString().toLower().contains(needle);
            if(!entityMatch)continue;const auto selected=selectVersion(entity,point);
            entity["selectedVersionId"]=selected.isEmpty()?QJsonValue::Null:selected.value("versionId");entities.push_back(entity);
        }
        if(!entities.isEmpty())groups.push_back(QJsonObject{{"lineageId",lineage.value("lineageId")},{"names",lineage.value("names")},{"entities",entities}});
    }
    return groups;
}
QString TerritorialLibraryCatalog::selectedVersionId(const QString& id,const QString& date) const {
    return selectVersion(entry(id),parseTemporal(date.toStdString())).value("versionId").toString();
}
QJsonObject TerritorialLibraryCatalog::loadEntity(const QString& id) const {
    const auto spec=entry(id);std::lock_guard lock(mutex_);
    if(const auto found=loaded_.find(id);found!=loaded_.end())return found->second;
    QByteArray stored;
    if(reader_)stored=reader_(spec.value("file").toString());
    else {
        QFile file(QDir(root_).filePath(spec.value("file").toString()));
        require(file.open(QIODevice::ReadOnly)&&file.size()==byteCount(spec.value("compressedBytes")),"PL-LIB-READ: missing or wrong-size chunk");stored=file.readAll();
    }
    require(stored.size()==byteCount(spec.value("compressedBytes")),"PL-LIB-SIZE: stored chunk length mismatch");
    require(QCryptographicHash::hash(stored,QCryptographicHash::Sha256).toHex()==spec.value("sha256").toString().toLatin1(),"PL-LIB-HASH: stored chunk integrity mismatch");
    const auto decoded=decodeGzip(stored,byteCount(spec.value("decodedBytes")));const auto normalized=normalizeEntity(parseObject(decoded));
    require(comparableMetadata(normalized,true)==comparableMetadata(spec,false),"PL-LIB-IDENTITY: chunk/index metadata mismatch");
    loaded_.emplace(id,normalized);return normalized;
}
QJsonObject TerritorialLibraryCatalog::preview(const QString& id,const QString& date) const {
    const auto point=parseTemporal(date.toStdString());
    // Do not decode an unusable selection merely to discover an indexed gap.
    require(!selectVersion(entry(id),point).isEmpty(),"PL-LIB-GEOMETRY-GAP: no boundary at the selected date");
    const auto entity=loadEntity(id);const auto version=selectVersion(entity,point);
    require(!version.isEmpty(),"PL-LIB-GEOMETRY-GAP: no boundary at the selected date");
    return {{"entity",entity},{"version",version}};
}
QStringList TerritorialLibraryCatalog::entityRefsWithChildren(const QStringList& roots,const QString& date,const QString& depth) const {
    const auto point=parseTemporal(date.toStdString());
    require(depth=="none"||depth=="level1"||depth=="all","PL-LIB-PARENT: unsupported child depth");
    QStringList selected;std::set<QString> seen;
    for(const auto& id:roots){entry(id);if(seen.insert(id).second)selected.push_back(id);}
    if(depth=="none")return selected;
    auto frontier=selected;
    while(!frontier.isEmpty()) {
        const std::set<QString> parents(frontier.begin(),frontier.end());frontier.clear();
        for(const auto& raw:entries()) {
            const auto entity=raw.toObject();const auto id=entity.value("entityId").toString();
            if(parents.count(entity.value("parentEntityId").toString())&&!seen.count(id)&&existsAt(entity.value("lifetime").toObject(),point)) {
                seen.insert(id);selected.push_back(id);frontier.push_back(id);
            }
        }
        if(depth=="level1")break;
    }
    return selected;
}
QJsonArray TerritorialLibraryCatalog::instantiateDescriptors(const QStringList& roots,const QString& date,const QString& depth) const {
    QJsonArray descriptors;
    for(const auto& id:entityRefsWithChildren(roots,date,depth)) {
        const auto selected=preview(id,date);const auto entity=selected.value("entity").toObject(),version=selected.value("version").toObject();
        auto metadata=entity.value("metadata").toObject();metadata["sourceInfo"]=entity.value("sourceInfo");metadata["sourceLifetime"]=entity.value("lifetime");
        metadata["sourceGeometryValidity"]=QJsonObject{{"validFrom",version.value("validFrom")},{"validTo",version.value("validTo")}};
        metadata["sourceReferenceDate"]=date;
        if(version.contains("certainty"))metadata["geometryCertainty"]=version.value("certainty");
        if(version.contains("datePrecision"))metadata["geometryDatePrecision"]=version.value("datePrecision");
        descriptors.push_back(QJsonObject{{"entityId",entity.value("entityId")},{"geometryVersionId",version.value("versionId")},
            {"entityKind",entity.value("entityKind")},{"name",displayName(entity.value("names").toObject())},{"parentEntityId",entity.value("parentEntityId")},
            {"geometry",version.value("geometry")},{"validFrom",QJsonValue::Null},{"validTo",QJsonValue::Null},
            {"metadata",metadata},{"instantiation",entity.value("instantiation")}});
    }
    return descriptors;
}
std::size_t TerritorialLibraryCatalog::loadedEntityCount() const {std::lock_guard lock(mutex_);return loaded_.size();}
}
