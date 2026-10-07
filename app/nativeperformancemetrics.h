#pragma once
#include <QVariantMap>
#include <QVariantList>
#include <QString>
#include <map>
#include <cmath>

// Owner-thread events; repeated periodic reads of a gauge are not new work.
class NativePerformanceMetrics {
public:
    void record(const QString& stage,const QString& flow,double milliseconds,
                const QString& disposition=QStringLiteral("completed"),QVariant owners={},qulonglong job=0) {
        if(!std::isfinite(milliseconds)||milliseconds<0)return;
        observeCategory(stage,milliseconds);
        ++serial_;events_.append(QVariantMap{{"eventSerial",serial_},{"stage",stage},{"flow",flow},
            {"durationMs",milliseconds},{"disposition",disposition},{"owners",owners},{"jobId",job}});
        if(events_.size()>256){events_.removeFirst();++dropped_;}
    }
    void observeCategory(const QString& category,double milliseconds) {
        if(!std::isfinite(milliseconds)||milliseconds<0)return;
        auto& state=stages_[category];++state.count;state.total+=milliseconds;state.latest=milliseconds;
    }
    QVariantMap snapshot() const {
        QVariantMap result{{"schema",QStringLiteral("pandoeditor-native-performance")},{"version",2},
            {"resetDomain",QStringLiteral("controller lifetime; counters cumulative, latest durations gauges")},
            {"durationUnit",QStringLiteral("ms")},{"eventSerial",serial_},{"eventsDropped",dropped_},{"events",events_}};
        for(const auto& key:{"compute","preview","prepareCommit","commit","undo","redo",
            "restoreDecode","restoreValidate","restoreProjection","restoreSwitch","restorePublish","restoreHydro"}) {
            const auto at=stages_.find(QString::fromLatin1(key));const auto prefix=QString::fromLatin1(key);
            result.insert(prefix+"Count",at==stages_.end()?qulonglong(0):at->second.count);
            result.insert(prefix+"Ms",at==stages_.end()?QVariant{}:QVariant(at->second.latest));
            result.insert(prefix+"TotalMs",at==stages_.end()?QVariant{}:QVariant(at->second.total));
        }
        for(const auto& key:{"snapQuery","selectionPreparation","riverPartition","splitPreparation","annexPreparation","sharedBoundaryPreparation"}) {
            const auto at=stages_.find(QString::fromLatin1(key));const auto prefix=QString::fromLatin1(key);
            result.insert(prefix+"Count",at==stages_.end()?qulonglong(0):at->second.count);
            result.insert(prefix+"Ms",at==stages_.end()?QVariant{}:QVariant(at->second.latest));
        }
        result.insert("snapCandidatesExamined",QVariant{});
        result.insert("previewComputeMs",result.value("previewMs"));
        result.insert("measurementScope",QStringLiteral("worker calculation excludes queue and GUI publication; commit/Undo include core and GUI publication, exclude presentation"));
        for(const auto& alias:{"preview","commit","undo"})result.insert(QString::fromLatin1(alias)+"LatencyMs",QVariant{});
        return result;
    }
private:
    struct Stage {qulonglong count=0;double total=0,latest=0;};
    std::map<QString,Stage> stages_;
    QVariantList events_;
    qulonglong serial_=0,dropped_=0;
};
