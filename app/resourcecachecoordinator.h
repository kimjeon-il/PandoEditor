#pragma once
#include <pandoeditor/map/resourcecachepolicy.h>
#include <QVariantMap>
#include <QString>
#include <QDateTime>
#include <limits>

// GUI-thread value snapshots only. Never owns resources or holds a domain lock.
class ResourceCacheCoordinator {
public:
    void setDomain(const QString& name,const pandoeditor::ResourceCacheSnapshot& value,
                   const QString& budgetMode=QStringLiteral("bounded"),QVariantMap extra={}) {
        QVariantMap map=std::move(extra);
        const auto put=[&](const char* key,auto number){map.insert(QString::fromLatin1(key),qulonglong(number));};
        map["domain"]=name;map["budgetMode"]=budgetMode;
        put("scopeEpoch",value.scopeEpoch);put("budgetBytes",value.budgetBytes);
        put("residentCount",value.residentCount);put("residentBytes",value.residentBytes);
        put("activeBytes",value.activeBytes);put("protectedBytes",value.protectedBytes);
        map["retiredBytesAvailable"]=value.retiredBytes.has_value();
        map["retiredBytes"]=value.retiredBytes?QVariant::fromValue(qulonglong(*value.retiredBytes)):QVariant{};
        put("pendingCount",value.pendingCount);put("pendingEstimatedBytes",value.pendingEstimatedBytes);put("pendingUnknownCount",value.pendingUnknownCount);
        put("evictableBytes",value.evictableBytes);put("overBudgetBytes",value.overBudgetBytes);put("protectedOverBudgetBytes",value.protectedOverBudgetBytes);
        put("hitCount",value.hitCount);put("missCount",value.missCount);put("evictionCount",value.evictionCount);put("evictionBytes",value.evictionBytes);
        put("invalidationCount",value.invalidationCount);put("staleCompletionCount",value.staleCompletionCount);put("failureCount",value.failureCount);put("trimBlockedCount",value.trimBlockedCount);
        const char* names[]={"visible","selected","editing","inFlight","fallback"};QVariantMap reasons;
        for(std::size_t i=0;i<value.protectionCounts.size();++i)reasons[names[i]]=qulonglong(value.protectionCounts[i]);
        map["protectionCounts"]=reasons;map["sampleSequence"]=qulonglong(nextSample());map["observedAt"]=QDateTime::currentMSecsSinceEpoch();
        domains_[name]=std::move(map);
    }
    void setQsgSource(quintptr source){if(source_!=source){source_=source;generation_=0;qsg_.clear();}}
    bool acceptQsgSnapshot(quintptr source,qulonglong generation,QVariantMap value) {
        if(source!=source_||generation<generation_)return false;
        generation_=generation;value["resourceGeneration"]=generation;value["available"]=true;
        value["sampleSequence"]=qulonglong(nextSample());value["observedAt"]=QDateTime::currentMSecsSinceEpoch();
        value["accountingScope"]="CPU QSG geometry allocations, not driver VRAM";qsg_=std::move(value);return true;
    }
    QVariantMap snapshot() const {
        auto result=domains_;qulonglong bytes=0;bool available=true;
        for(auto it=domains_.cbegin();it!=domains_.cend();++it) {
            const auto n=it.value().toMap().value("residentBytes").toULongLong();
            if(n>std::numeric_limits<qulonglong>::max()-bytes){available=false;break;}bytes+=n;
        }
        result["cacheOwnedCpuBytesAvailable"]=available;
        result["cacheOwnedCpuBytes"]=available?QVariant::fromValue(bytes):QVariant{};
        result["accountingScope"]="Cache-owned CPU payload storage; excludes allocator metadata, retained scenes, glyph atlas and driver VRAM";
        result["qsg"]=qsg_.isEmpty()?QVariantMap{{"available",false}}:qsg_;
        return result;
    }
private:
    std::uint64_t nextSample(){if(sample_!=std::numeric_limits<std::uint64_t>::max())++sample_;return sample_;}
    QVariantMap domains_,qsg_;
    quintptr source_=0;
    qulonglong generation_=0;
    std::uint64_t sample_=0;
};
