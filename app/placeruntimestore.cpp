#include "placeruntimestore.h"
#include <pandoeditor/map/projectionengine.h>
#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <QJsonParseError>
#include <QUrl>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>

namespace {
constexpr double Pi=3.14159265358979323846,Degrees=180/Pi;
const QStringList Kinds={"capital","city","region","town","mountain","water","custom"};
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void checkpoint(const PlaceRuntimeStore::Cancellation& cancelled) {if(cancelled&&cancelled())throw PlaceRuntimeCancelled();}
QString hash(const QByteArray& bytes) {return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
bool validHash(const QString& text) {static const QRegularExpression p("^[a-f0-9]{64}$");return p.match(text).hasMatch();}
std::size_t textBytes(const QString& value) {return std::size_t(value.capacity())*sizeof(QChar);}
bool ecmaSpace(QChar value) {
    const auto c=value.unicode();return (c>=0x0009&&c<=0x000d)||c==0x0020||c==0x00a0||c==0x1680||(c>=0x2000&&c<=0x200a)||c==0x2028||c==0x2029||c==0x202f||c==0x205f||c==0x3000||c==0xfeff;
}
QString ecmaTrim(const QString& value) {
    qsizetype first=0,last=value.size();while(first<last&&ecmaSpace(value[first]))++first;while(last>first&&ecmaSpace(value[last-1]))--last;return value.mid(first,last-first);
}
QString text(const QString& value,int maximum,bool required=false) {
    const auto result=ecmaTrim(value);require((!required||!result.isEmpty())&&result.toUcs4().size()<=maximum&&!result.contains(QChar(0)),"PL-PLACE-RECORD: invalid text");return result;
}
double normalizedLongitude(double value) {double result=std::fmod(value+180,360);if(result<0)result+=360;return result-180;}
bool order(const PlaceRecord& a,const PlaceRecord& b) {
    if(a.priority!=b.priority)return a.priority>b.priority;
    if(a.population!=b.population)return a.population>b.population;
    return a.id<b.id;
}
double policyMinZoom(const QString& kind) {
    if(kind=="capital")return 0;if(kind=="city")return 1.25;if(kind=="region")return 1;
    if(kind=="town")return 2.5;if(kind=="mountain")return 2;return 1.5;
}
std::optional<pandoeditor::Point> projected(const PlaceRecord& record,const PlaceViewport& viewport) {
    const auto& v=viewport.view;
    if(v.mode==ProjectionMode::Globe){const auto p=projectPoint(record.coordinates,v);if(!p.finite||p.frontness<-.005)return {};return pandoeditor::Point{p.x,p.y};}
    const auto offsets=visibleFlatWorldOffsets(v);std::optional<pandoeditor::Point> nearest;
    for(double offset:offsets){const auto p=projectPoint(record.coordinates,v,offset);if(!p.finite)continue;
        pandoeditor::Point value{p.x,p.y};
        if(p.x>=viewport.safeLeft-30&&p.x<=v.viewportWidth-viewport.safeRight+30&&p.y>=viewport.safeTop-30&&p.y<=v.viewportHeight-viewport.safeBottom+30)return value;
        if(!nearest||std::abs(p.x-v.viewportWidth/2)<std::abs(nearest->x-v.viewportWidth/2))nearest=value;
    }return nearest;
}
bool inViewport(const PlaceRecord& r,const PlaceViewport& v) {
    const auto p=projected(r,v);if(!p)return false;
    // A language toggle must not require a fresh tile request. Admit a place if
    // any recorded language can fit, then measure the chosen rows on the UI thread.
    QStringList candidateNames{r.name,r.nameEn,r.nameNative};candidateNames.append(r.nameNativeExtras);
    for(const auto& name:candidateNames) {
        if(name.isEmpty())continue;
        const double width=std::max(22.,double(name.toUcs4().size())*9+16),height=19;
        if(p->x-width/2>=v.safeLeft&&p->x+width/2<=v.view.viewportWidth-v.safeRight&&
           p->y-height/2>=v.safeTop&&p->y+height/2<=v.view.viewportHeight-v.safeBottom)return true;
    }
    return false;
}
quint64 integer(const QJsonValue& v,quint64 minimum,quint64 maximum) {
    require(v.isDouble(),"PL-PLACE-MANIFEST: integer required");const double n=v.toDouble();
    require(std::isfinite(n)&&n>=double(minimum)&&n<=double(maximum)&&std::floor(n)==n,"PL-PLACE-MANIFEST: integer range");return quint64(n);
}
QString relativePath(const QString& root,const QString& raw) {
    const QUrl url(raw);require(url.isRelative()&&!url.hasQuery()&&!url.hasFragment()&&!raw.contains('\\'),"PL-PLACE-MANIFEST: local relative shard URL required");
    const auto path=QUrl::fromPercentEncoding(raw.toUtf8());require(!path.isEmpty()&&!QDir::isAbsolutePath(path),"PL-PLACE-MANIFEST: absolute shard path");
    for(const auto& component:path.split('/'))require(component!="..", "PL-PLACE-MANIFEST: shard traversal");
    const auto resolved=QDir(root).filePath(QDir::cleanPath(path));
    if(!root.startsWith(':')){const auto realRoot=QDir::fromNativeSeparators(QFileInfo(root).canonicalFilePath()),realFile=QDir::fromNativeSeparators(QFileInfo(resolved).canonicalFilePath());
        const auto prefix=realRoot+(realRoot.endsWith('/')?QString():QString("/"));
#ifdef Q_OS_WIN
        constexpr auto pathCase=Qt::CaseInsensitive;
#else
        constexpr auto pathCase=Qt::CaseSensitive;
#endif
        if(!realFile.isEmpty())require(!realRoot.isEmpty()&&realFile.startsWith(prefix,pathCase),"PL-PLACE-MANIFEST: shard escapes source root");}
    return resolved;
}
QByteArray readFile(const QString& path,std::size_t limit,const PlaceRuntimeStore::Cancellation& cancelled={}) {
    checkpoint(cancelled);QFile file(path);require(file.open(QIODevice::ReadOnly),"PL-PLACE-LOAD: cannot open local source");
    require(file.size()>=0&&quint64(file.size())<=limit,"PL-PLACE-LOAD: source byte budget");
    QByteArray bytes;bytes.reserve(int(file.size()));
    while(!file.atEnd()){checkpoint(cancelled);const auto chunk=file.read(std::min<qint64>(64*1024,qint64(limit-bytes.size()+1)));require(!chunk.isEmpty()||file.error()==QFile::NoError,"PL-PLACE-LOAD: read failed");bytes+=chunk;require(std::size_t(bytes.size())<=limit,"PL-PLACE-LOAD: source grew beyond byte budget");}
    checkpoint(cancelled);return bytes;
}
}
struct PlaceRuntimeStore::Data {
    struct Stage {int id=0,columns=1,rows=1;double minZoom=0;};
    struct Shard {QString path,sha256;std::size_t bytes=0;};
    struct Tile {QString shard,sha256,first,last;std::size_t offset=0,length=0;};
    struct CacheEntry {QByteArray bytes;std::shared_ptr<const std::vector<PlaceRecord>> records;std::size_t resident=0;quint64 used=0;};
    QString revision,manifestHash;
    QByteArray manifestBytes;
    std::vector<Stage> stages;
    QMap<QString,Shard> shards;
    QMap<QString,Tile> tiles;
    QMap<QString,std::vector<Tile>> search;
    mutable std::mutex mutex;
    mutable std::mutex metricsMutex;
    PlaceStoreStats publishedStats;
    QMap<QString,CacheEntry> cache;
    quint64 clock=0;
    PlaceStoreStats stats;
    void publishStats() {
        auto value=stats;value.cachedEntries=std::size_t(cache.size());for(auto i=cache.begin();i!=cache.end();++i){if(i->records)++value.cachedTiles;else ++value.cachedShards;}
        std::lock_guard<std::mutex> lock(metricsMutex);publishedStats=value;
    }
    struct PublishOnExit {Data& data;~PublishOnExit(){data.publishStats();}};
    void put(const QString& key,CacheEntry entry,const Cancellation& cancelled) {
        checkpoint(cancelled);entry.resident+=sizeof(CacheEntry)+textBytes(key);if(entry.resident>stats.cacheBudget)return;
        auto found=cache.find(key);if(found!=cache.end()){stats.cacheBytes-=found->resident;cache.erase(found);}
        while(stats.cacheBytes+entry.resident>stats.cacheBudget&&!cache.isEmpty()){
            auto victim=cache.begin();for(auto i=cache.begin();i!=cache.end();++i)if(i->used<victim->used)victim=i;
            stats.cacheBytes-=victim->resident;cache.erase(victim);++stats.evictions;
        }
        entry.used=++clock;stats.cacheBytes+=entry.resident;cache.insert(key,std::move(entry));
    }
    std::shared_ptr<const std::vector<PlaceRecord>> load(const Tile& tile,const Cancellation& cancelled) {
        // Descriptor hash participates in identity: a warmed physical byte range
        // cannot authorize a later malformed descriptor with another SHA.
        checkpoint(cancelled);const QString key="tile:"+tile.shard+":"+QString::number(tile.offset)+":"+QString::number(tile.length)+":"+tile.sha256;
        auto hit=cache.find(key);if(hit!=cache.end()){hit->used=++clock;++stats.cacheHits;return hit->records;}
        const auto& spec=shards[tile.shard];const QString shardKey="shard:"+tile.shard;QByteArray full;
        auto shardHit=cache.find(shardKey);if(shardHit!=cache.end()){shardHit->used=++clock;++stats.cacheHits;full=shardHit->bytes;}
        else {++stats.fileReadCount;full=readFile(spec.path,PlaceRuntimeLimits::ShardBytes,cancelled);require(std::size_t(full.size())==spec.bytes,"PL-PLACE-LOAD: shard length mismatch");require(spec.sha256.isEmpty()||hash(full)==spec.sha256,"PL-PLACE-LOAD: shard hash mismatch");}
        checkpoint(cancelled);const auto payload=full.mid(qsizetype(tile.offset),qsizetype(tile.length));require(hash(payload)==tile.sha256,"PL-PLACE-LOAD: tile hash mismatch");
        auto decoded=std::make_shared<const std::vector<PlaceRecord>>(PlaceRuntimeStore::decodeTile(payload));checkpoint(cancelled);
        // Admit only completely verified content. Cancellation before decode/hash
        // completion leaves no partial shard or tile entry behind.
        if(shardHit==cache.end())put(shardKey,{full,{},std::size_t(full.capacity()),0},cancelled);
        std::size_t resident=sizeof(std::vector<PlaceRecord>)+decoded->capacity()*sizeof(PlaceRecord);for(const auto& r:*decoded)resident+=PlaceRuntimeStore::recordBytes(r)-sizeof(PlaceRecord);
        put(key,{{},decoded,resident,0},cancelled);return decoded;
    }
    PlaceQueryResult rows(const std::vector<Tile>& rows,const Cancellation& cancelled,const std::function<bool(const PlaceRecord&)>& accepts,std::size_t limit) {
        PlaceQueryResult result;result.tileCount=rows.size();
        for(std::size_t i=0;i<rows.size();i+=4){checkpoint(cancelled);std::vector<std::shared_ptr<const std::vector<PlaceRecord>>> batch;
            std::size_t working=result.records.size();for(std::size_t j=i;j<std::min(i+4,rows.size());++j){auto tile=load(rows[j],cancelled);working+=tile->size();batch.push_back(std::move(tile));}
            stats.peakWorkingRecords=std::max(stats.peakWorkingRecords,working);
            QMap<QString,PlaceRecord> unique;for(const auto& r:result.records)unique.insert(r.id,r);
            for(const auto& tile:batch)for(const auto& r:*tile){checkpoint(cancelled);++result.candidatesExamined;if(accepts(r))unique.insert(r.id,r);}
            result.truncated|=std::size_t(unique.size())>limit;result.records.clear();result.records.reserve(unique.size());for(auto it=unique.begin();it!=unique.end();++it)result.records.push_back(it.value());
            std::sort(result.records.begin(),result.records.end(),order);if(result.records.size()>limit)result.records.resize(limit);
        }
        checkpoint(cancelled);stats.candidatesExamined+=result.candidatesExamined;return result;
    }
};
PlaceRuntimeStore::PlaceRuntimeStore(std::unique_ptr<Data> data):data_(std::move(data)){}
PlaceRuntimeStore::~PlaceRuntimeStore()=default;
std::size_t PlaceRuntimeStore::recordBytes(const PlaceRecord& r) {
    std::size_t bytes=sizeof(PlaceRecord)+textBytes(r.id)+textBytes(r.source)+textBytes(r.sourceId)+
        textBytes(r.name)+textBytes(r.nameEn)+textBytes(r.nameNative)+textBytes(r.kind)+
        textBytes(r.countryCode)+textBytes(r.featureCode)+
        std::size_t(r.nameNativeExtras.capacity())*sizeof(QString)+
        r.nameTimeline.capacity()*sizeof(PlaceNameTransition);
    for(const auto& extra:r.nameNativeExtras)bytes+=textBytes(extra);
    for(const auto& transition:r.nameTimeline) {
        bytes+=textBytes(transition.fromDate)+textBytes(transition.ko)+
            textBytes(transition.en)+textBytes(transition.native);
        if(transition.nativeExtras) {
            bytes+=std::size_t(transition.nativeExtras->capacity())*sizeof(QString);
            for(const auto& name:*transition.nativeExtras)bytes+=textBytes(name);
        }
    }
    return bytes;
}
bool PlaceRuntimeStore::isBuiltinId(const QString& id) {static const QRegularExpression p("^builtin:place:[a-z0-9-]+:.+$");return p.match(id).hasMatch();}
QString PlaceRuntimeStore::normalizeQuery(const QString& value) {
    const auto normalized=ecmaTrim(value.normalized(QString::NormalizationForm_KC)).toLower();QString result;result.reserve(normalized.size());bool previousSpace=false;
    for(const auto c:normalized){const bool space=ecmaSpace(c);if(!space||!previousSpace)result.append(space?QChar(' '):c);previousSpace=space;}return result;
}
std::vector<PlaceRecord> PlaceRuntimeStore::decodeTile(const QByteArray& bytes) {
    require(bytes.size()>=32&&std::size_t(bytes.size())<=PlaceRuntimeLimits::ShardBytes,"PL-PLACE-CODEC: tile byte budget");
    const auto* p=reinterpret_cast<const uchar*>(bytes.constData());
    const auto u16=[&](std::size_t offset){return qFromLittleEndian<quint16>(p+offset);};
    const auto u32=[&](std::size_t offset){return qFromLittleEndian<quint32>(p+offset);};
    // v1/v2 frozen oracle fixtures remain readable, while new publications use
    // PLAC v3's ninth string field for up to two additional native names.
    const quint16 version=u16(4);
    const std::size_t stride=version==3?72:version==2?68:version==1?56:0;
    require(u32(0)==0x43414c50&&stride&&u16(6)==stride,"PL-PLACE-CODEC: header");
    const std::size_t count=u32(8),poolBytes=u32(12),poolStart=32+stride*count;
    require(count<=PlaceRuntimeLimits::TileRecords&&poolStart+poolBytes==std::size_t(bytes.size())&&
            u32(16)==quint32(bytes.size()),"PL-PLACE-CODEC: table range");
    QMap<quint32,QString> pool;
    for(std::size_t offset=0;offset<poolBytes;) {
        require(offset+4<=poolBytes,"PL-PLACE-CODEC: string header");
        const auto length=u32(poolStart+offset);
        require(length<=(version>=2?16*1024u:1024u)&&offset+4+length<=poolBytes,"PL-PLACE-CODEC: string length");
        QStringDecoder decoder(QStringDecoder::Utf8,QStringConverter::Flag::Stateless);
        const QString value=decoder.decode(QByteArrayView(bytes.constData()+poolStart+offset+4,length));
        require(!decoder.hasError(),"PL-PLACE-CODEC: invalid UTF-8");
        pool.insert(quint32(offset),value);offset+=4+length;
    }
    const auto f64=[&](std::size_t offset){const auto bits=qFromLittleEndian<quint64>(p+offset);double value;std::memcpy(&value,&bits,sizeof(value));return value;};
    const auto f32=[&](std::size_t offset){const auto bits=u32(offset);float value;std::memcpy(&value,&bits,sizeof(value));return double(value);};
    const auto extrasFromJson=[&](const QJsonValue& value,const QString& primary)->QStringList {
        require(value.isArray(),"PL-PLACE-CODEC: native extra list required");
        const auto array=value.toArray();
        require(array.size()<=2&&(array.isEmpty()||!primary.isEmpty()),"PL-PLACE-CODEC: native extra count");
        QSet<QString> seen;
        if(!primary.isEmpty())seen.insert(primary.normalized(QString::NormalizationForm_KC).toLower());
        QStringList names;
        for(const auto& element:array) {
            require(element.isString(),"PL-PLACE-CODEC: native extra name");
            const auto name=text(element.toString(),256,true);
            const auto key=name.normalized(QString::NormalizationForm_KC).toLower();
            require(!seen.contains(key),"PL-PLACE-CODEC: duplicate native name");
            seen.insert(key);names.append(name);
        }
        return names;
    };
    std::vector<PlaceRecord> records;records.reserve(count);
    for(std::size_t i=0;i<count;++i) {
        const auto base=32+i*stride;
        PlaceRecord r;r.coordinates={f64(base),f64(base+8)};r.population=f64(base+16);
        r.priority=f32(base+24);r.minZoom=f32(base+28);
        require(std::isfinite(r.coordinates.x)&&std::abs(r.coordinates.x)<=180&&
                std::isfinite(r.coordinates.y)&&std::abs(r.coordinates.y)<=90,"PL-PLACE-CODEC: coordinates");
        require(std::isfinite(r.population)&&r.population>=0&&std::isfinite(r.priority)&&
                std::isfinite(r.minZoom)&&r.minZoom>=0,"PL-PLACE-CODEC: ranking");
        const auto kind=p[base+32];require(kind<Kinds.size(),"PL-PLACE-CODEC: kind");
        r.kind=Kinds[kind];
        QString nativeExtrasText,timelineText;
        QString* fields[]={&r.sourceId,&r.name,&r.countryCode,&r.source,&r.featureCode,
            &r.nameEn,&r.nameNative,&nativeExtrasText,&timelineText};
        const int max[]={128,256,8,32,32,256,256,16*1024,16*1024};
        const int fieldCount=version==3?9:version==2?8:5;
        for(int j=0;j<fieldCount;++j) {
            const auto off=u32(base+36+j*4);
            require(pool.contains(off),"PL-PLACE-CODEC: string offset");
            const int target=(version==2&&j==7)?8:j;
            *fields[target]=target>=7?pool[off]:text(pool[off],max[target],j==0||j==1||j==3);
        }
        if(version==3) {
            QJsonParseError error;
            const auto doc=QJsonDocument::fromJson(nativeExtrasText.toUtf8(),&error);
            require(error.error==QJsonParseError::NoError&&doc.isArray(),"PL-PLACE-CODEC: native extras JSON");
            r.nameNativeExtras=extrasFromJson(doc.array(),r.nameNative);
        }
        if(version>=2) {
            QJsonParseError error;
            const auto doc=QJsonDocument::fromJson(timelineText.toUtf8(),&error);
            require(error.error==QJsonParseError::NoError&&doc.isArray()&&
                    doc.array().size()<=16,"PL-PLACE-CODEC: invalid name timeline");
            QString previous;
            for(const auto& value:doc.array()) {
                require(value.isObject(),"PL-PLACE-CODEC: timeline entry");
                const auto obj=value.toObject();PlaceNameTransition transition;
                const bool hasYear=obj.contains("fromYear"),hasDate=obj.contains("fromDate");
                require(hasYear!=hasDate,"PL-PLACE-CODEC: timeline date precision");
                QString boundary;
                if(hasDate) {
                    require(obj.value("fromDate").isString(),"PL-PLACE-CODEC: exact date");
                    transition.fromDate=obj.value("fromDate").toString();
                    const auto date=QDate::fromString(transition.fromDate,Qt::ISODate);
                    require(date.isValid()&&date.toString(Qt::ISODate)==transition.fromDate&&
                            transition.fromDate.size()==10,"PL-PLACE-CODEC: exact date");
                    boundary=transition.fromDate;
                } else {
                    const auto year=obj.value("fromYear");
                    require(year.isDouble()&&std::isfinite(year.toDouble())&&
                            std::floor(year.toDouble())==year.toDouble()&&year.toDouble()>=1&&
                            year.toDouble()<=9999,"PL-PLACE-CODEC: year-only name date");
                    transition.fromYear=int(year.toDouble());
                    boundary=QString("%1-01-01").arg(transition.fromYear,4,10,QChar('0'));
                }
                require(previous.isEmpty()||previous<boundary,"PL-PLACE-CODEC: unsorted name dates");
                previous=boundary;
                for(const auto* lang:{"ko","en","native"}) {
                    if(!obj.contains(lang))continue;
                    require(obj.value(lang).isString(),"PL-PLACE-CODEC: language name");
                    const auto variant=text(obj.value(lang).toString(),256,true);
                    if(QString::fromLatin1(lang)=="ko")transition.ko=variant;
                    if(QString::fromLatin1(lang)=="en")transition.en=variant;
                    if(QString::fromLatin1(lang)=="native")transition.native=variant;
                }
                if(obj.contains("nativeExtras")) {
                    require(version==3&&!transition.native.isEmpty(),"PL-PLACE-CODEC: native extras require primary");
                    transition.nativeExtras=extrasFromJson(obj.value("nativeExtras"),transition.native);
                }
                require(!transition.ko.isEmpty()||!transition.en.isEmpty()||
                        !transition.native.isEmpty(),"PL-PLACE-CODEC: empty name transition");
                r.nameTimeline.push_back(std::move(transition));
            }
        }
        static const QRegularExpression sourcePattern("^[a-z0-9-]+$");
        require(sourcePattern.match(r.source).hasMatch(),"PL-PLACE-CODEC: source");
        r.id="builtin:place:"+r.source+":"+r.sourceId;records.push_back(std::move(r));
    }
    return records;
}
std::shared_ptr<PlaceRuntimeStore> PlaceRuntimeStore::open(const QString& manifestPath,QString& error,std::size_t budget) {
    try {
        const auto bytes=readFile(manifestPath,PlaceRuntimeLimits::ManifestBytes);QJsonParseError parseError;const auto doc=QJsonDocument::fromJson(bytes,&parseError);
        require(parseError.error==QJsonParseError::NoError&&doc.isObject(),"PL-PLACE-MANIFEST: invalid JSON");const auto raw=doc.object();
        require(raw["version"].isDouble()&&raw["version"].toDouble()==1&&raw["stages"].isArray()&&raw["stages"].toArray().size()<=8&&raw["tiles"].isObject()&&raw["shards"].isObject()&&raw["search"].isObject(),"PL-PLACE-MANIFEST: structure");
        require(raw["revision"].isString()&&!raw["revision"].toString().isEmpty(),"PL-PLACE-MANIFEST: revision required");
        auto d=std::make_unique<Data>();d->manifestBytes=bytes;d->revision=raw["revision"].toString();d->manifestHash=hash(bytes);d->stats.cacheBudget=std::min(budget,PlaceRuntimeLimits::CacheBytes);
        std::set<int> ids;
        for(const auto& value:raw["stages"].toArray()){require(value.isObject(),"PL-PLACE-MANIFEST: stage");const auto stage=value.toObject();Data::Stage s;s.id=int(integer(stage["id"],0,std::numeric_limits<int>::max()));require(ids.insert(s.id).second,"PL-PLACE-MANIFEST: duplicate stage");
            s.columns=int(integer(stage["columns"],1,512));s.rows=int(integer(stage["rows"],1,256));require(stage["minZoom"].isDouble()&&std::isfinite(stage["minZoom"].toDouble())&&stage["minZoom"].toDouble()>=0,"PL-PLACE-MANIFEST: stage zoom");s.minZoom=stage["minZoom"].toDouble();d->stages.push_back(s);}
        const QString root=QFileInfo(manifestPath).dir().absolutePath();const auto shards=raw["shards"].toObject();
        for(auto i=shards.begin();i!=shards.end();++i){require(i.value().isObject(),"PL-PLACE-MANIFEST: shard");const auto value=i.value().toObject();Data::Shard s;
            require(value["url"].isString(),"PL-PLACE-MANIFEST: shard URL");s.path=relativePath(root,value["url"].toString());s.bytes=integer(value["bytes"],1,PlaceRuntimeLimits::ShardBytes);
            if(value.contains("sha256")){require(value["sha256"].isString()&&validHash(value["sha256"].toString()),"PL-PLACE-MANIFEST: shard hash");s.sha256=value["sha256"].toString();}d->shards.insert(i.key(),s);}
        const auto tile=[&](const QJsonValue& value){require(value.isObject(),"PL-PLACE-MANIFEST: tile");const auto row=value.toObject();Data::Tile t;require(row["shard"].isString(),"PL-PLACE-MANIFEST: shard key");t.shard=row["shard"].toString();require(d->shards.contains(t.shard),"PL-PLACE-MANIFEST: missing shard");
            t.offset=integer(row["offset"],0,PlaceRuntimeLimits::ShardBytes);t.length=integer(row["length"],32,PlaceRuntimeLimits::ShardBytes);require(t.offset+t.length<=d->shards[t.shard].bytes,"PL-PLACE-MANIFEST: tile range");require(row["sha256"].isString()&&validHash(row["sha256"].toString()),"PL-PLACE-MANIFEST: tile hash");t.sha256=row["sha256"].toString();return t;};
        const auto tiles=raw["tiles"].toObject();static const QRegularExpression keyPattern("^(\\d+)/(\\d+)-(\\d+)$");
        for(auto i=tiles.begin();i!=tiles.end();++i){const auto match=keyPattern.match(i.key());require(match.hasMatch(),"PL-PLACE-MANIFEST: tile key");bool ok=false;const int stage=match.captured(1).toInt(&ok);require(ok,"PL-PLACE-MANIFEST: stage key overflow");const auto found=std::find_if(d->stages.begin(),d->stages.end(),[&](const auto& s){return s.id==stage;});require(found!=d->stages.end(),"PL-PLACE-MANIFEST: tile stage");const auto x=match.captured(2).toULongLong(&ok);require(ok&&x<quint64(found->columns),"PL-PLACE-MANIFEST: tile column");const auto y=match.captured(3).toULongLong(&ok);require(ok&&y<quint64(found->rows),"PL-PLACE-MANIFEST: tile row");d->tiles.insert(i.key(),tile(i.value()));}
        const auto search=raw["search"].toObject();for(auto i=search.begin();i!=search.end();++i){require(i.value().isArray(),"PL-PLACE-MANIFEST: search pages");std::vector<Data::Tile> pages;
            for(const auto& value:i.value().toArray()){auto t=tile(value);const auto row=value.toObject();require(row["first"].isString()&&row["last"].isString(),"PL-PLACE-MANIFEST: search range");t.first=row["first"].toString();t.last=row["last"].toString();require(t.first<=t.last,"PL-PLACE-MANIFEST: reversed search range");pages.push_back(t);}d->search.insert(i.key(),std::move(pages));}
        d->stats.sourceResidentBytes=std::size_t(bytes.capacity())+sizeof(Data)+textBytes(d->revision)+textBytes(d->manifestHash)+d->stages.capacity()*sizeof(Data::Stage);
        d->stats.manifestTileCount=std::size_t(d->tiles.size());d->stats.manifestShardCount=std::size_t(d->shards.size());d->stats.manifestStageCount=d->stages.size();
        for(auto i=d->shards.begin();i!=d->shards.end();++i)d->stats.sourceResidentBytes+=sizeof(Data::Shard)+textBytes(i.key())+textBytes(i->path)+textBytes(i->sha256);
        for(auto i=d->tiles.begin();i!=d->tiles.end();++i)d->stats.sourceResidentBytes+=sizeof(Data::Tile)+textBytes(i.key())+textBytes(i->shard)+textBytes(i->sha256);
        for(auto i=d->search.begin();i!=d->search.end();++i){d->stats.sourceResidentBytes+=textBytes(i.key())+i->capacity()*sizeof(Data::Tile);for(const auto& t:*i)d->stats.sourceResidentBytes+=textBytes(t.shard)+textBytes(t.sha256)+textBytes(t.first)+textBytes(t.last);}
        d->publishStats();error.clear();return std::shared_ptr<PlaceRuntimeStore>(new PlaceRuntimeStore(std::move(d)));
    }catch(const std::exception& exception){error=QString::fromUtf8(exception.what());return {};}
}
PlaceQueryResult PlaceRuntimeStore::queryViewport(const PlaceViewport& view,const Cancellation& cancelled) {
    checkpoint(cancelled);require(validMapViewState(view.view)&&std::isfinite(view.zoom)&&view.zoom>=0,"PL-PLACE-QUERY: invalid viewport");
    for(const double inset:{view.safeLeft,view.safeTop,view.safeRight,view.safeBottom})require(std::isfinite(inset)&&inset>=0,"PL-PLACE-QUERY: safe inset");
    std::lock_guard<std::mutex> lock(data_->mutex);Data::PublishOnExit publish{*data_};checkpoint(cancelled);++data_->stats.queryCount;
    std::vector<Data::Stage> stages;for(const auto& s:data_->stages)if(s.minZoom<=view.zoom)stages.push_back(s);std::stable_sort(stages.begin(),stages.end(),[](const auto& a,const auto& b){return a.minZoom>b.minZoom;});
    std::vector<QString> keys;QString windowSignature="empty";
    for(const auto& s:stages){keys.clear();const auto& v=view.view;double lon=v.centerLongitude,lat=v.centerLatitude,halfLon,halfLat;
        if(v.mode==ProjectionMode::Globe){lon+=v.rotationLongitude;lat=std::clamp(lat+v.rotationLatitude,-90.,90.);
            const double radius=std::min(Pi,std::asin(std::clamp(std::hypot(v.viewportWidth,v.viewportHeight)*.5/std::max(1.,v.scale),0.,1.))+std::hypot(360./s.columns,180./s.rows)*Pi/360+.04);halfLat=radius*Degrees;
            halfLon=std::abs(lat)/Degrees+radius>=Pi/2-1e-9?180:std::asin(std::clamp(std::sin(radius)/std::max(1e-9,std::cos(lat/Degrees)),-1.,1.))*Degrees;
        }else {const auto center=unprojectFlat(v.viewportWidth/2,v.viewportHeight/2,v);lon=center.x;lat=center.y;
            halfLon=v.viewportWidth/std::max(1.,v.scale)*90/Pi+2;halfLat=v.viewportHeight/std::max(1.,v.scale)*90/Pi+2;}
        lon=normalizedLongitude(lon);lat=std::clamp(lat,-90.,90.);std::vector<int> xs;int start=s.rows,end=-1;
        for(int x=0;x<s.columns;++x)if(halfLon>=180-1e-9||std::abs(normalizedLongitude(-180+(x+.5)*360/s.columns-lon))<=halfLon+180./s.columns+1e-9)xs.push_back(x);
        for(int y=0;y<s.rows;++y)if(std::abs(90-(y+.5)*180/s.rows-lat)<=halfLat+90./s.rows+1e-9){start=std::min(start,y);end=std::max(end,y);}
        for(int y=start;y<=end;++y)for(int x:xs)keys.push_back(QString::number(s.id)+"/"+QString::number(x)+"-"+QString::number(y));
        QStringList columns;for(int x:xs)columns.append(QString::number(x));
        const auto mode=v.mode==ProjectionMode::Globe?QString("globe"):QString("flat");
        windowSignature=mode+";"+QString::number(s.id)+"@"+QString::number(s.minZoom,'g',15)+";"+QString::number(int(std::ceil(v.viewportWidth/256)))+"x"+QString::number(int(std::ceil(v.viewportHeight/256)))+";"+QString::number(s.id)+":"+QString::number(s.columns)+"x"+QString::number(s.rows)+":x"+columns.join(',')+":y"+QString::number(end>=start?start:0)+"-"+QString::number(end>=start?end:-1);
        if(keys.size()<=PlaceRuntimeLimits::QueryTiles)break;
    }
    require(keys.size()<=PlaceRuntimeLimits::QueryTiles,"PL-PLACE-QUERY: tile budget exceeded");std::vector<Data::Tile> rows;for(const auto& k:keys)if(data_->tiles.contains(k))rows.push_back(data_->tiles[k]);
    auto result=data_->rows(rows,cancelled,[&](const PlaceRecord& r){return std::max(r.minZoom,policyMinZoom(r.kind))<=view.zoom&&inViewport(r,view);},PlaceRuntimeLimits::Candidates);
    result.signature=data_->revision+":"+windowSignature;data_->stats.lastTileCount=result.tileCount;data_->stats.lastCandidateCount=result.records.size();return result;
}
PlaceQueryResult PlaceRuntimeStore::search(const QString& value,const Cancellation& cancelled) {
    checkpoint(cancelled);std::lock_guard<std::mutex> lock(data_->mutex);Data::PublishOnExit publish{*data_};checkpoint(cancelled);++data_->stats.searchCount;const auto query=normalizeQuery(value);const auto codepoints=query.toUcs4();if(codepoints.size()<2)return {};
    const auto prefix=QString::fromUcs4(codepoints.constData(),2);std::vector<Data::Tile> rows;bool truncated=false;
    for(const auto& page:data_->search.value(prefix))if(page.last>=query&&page.first<=query+QChar(0xffff)){if(rows.size()<PlaceRuntimeLimits::QueryTiles)rows.push_back(page);else truncated=true;}
    auto result=data_->rows(rows,cancelled,[&](const auto& r){return normalizeQuery(r.name).startsWith(query);},PlaceRuntimeLimits::SearchResults);result.truncated|=truncated;return result;
}
PlaceStoreStats PlaceRuntimeStore::stats() const {std::lock_guard<std::mutex> lock(data_->metricsMutex);return data_->publishedStats;}
QString PlaceRuntimeStore::revision() const {return data_->revision;}
QString PlaceRuntimeStore::manifestSha256() const {return data_->manifestHash;}
