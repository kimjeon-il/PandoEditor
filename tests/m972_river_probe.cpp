#include "riverpartitioncalculator.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <future>
#include <atomic>
#include <thread>
#include <chrono>
#include <stdexcept>
using namespace pandoeditor;
namespace {
QJsonObject memory() {
    QJsonObject output;
#ifdef Q_OS_LINUX
    QFile status("/proc/self/status");if(status.open(QIODevice::ReadOnly))for(const auto& line:status.readAll().split('\n')) {
        if(line.startsWith("VmRSS:")||line.startsWith("VmHWM:")){const auto fields=line.simplified().split(' ');output[QString::fromLatin1(fields[0])]=fields.value(1).toDouble();}
    }
#endif
    return output;
}
void logStage(const QString& name,const char* stage){QTextStream(stderr)<<name<<" "<<stage<<" "<<QJsonDocument(memory()).toJson(QJsonDocument::Compact)<<'\n';}
QByteArray read(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error(("Missing required fixture: "+path).toStdString());return f.readAll();}
Geometry geometry(const QJsonObject& object){Geometry g;g.type=object["type"].toString().toStdString();auto coordinates=object["coordinates"].toArray();if(g.type=="Polygon")coordinates=QJsonArray{coordinates};
    for(const auto& polygon:coordinates){Polygon rings;for(const auto& ring:polygon.toArray()){Ring points;for(const auto& point:ring.toArray()){auto p=point.toArray();points.push_back({p[0].toDouble(),p[1].toDouble()});}rings.push_back(points);}g.polygons.push_back(rings);}return g;}
QJsonObject lineJson(const Geometry& g){QJsonArray parts;for(const auto& part:g.lines){QJsonArray line;for(auto p:part)line.append(QJsonArray{p.x,p.y});parts.append(line);}return {{"type",QString::fromStdString(g.type)},{"coordinates",g.type=="LineString"?parts[0]:QJsonValue(parts)}};}
RiverPartitionRequest request(const QJsonObject& row,const std::vector<HydroRiverFeatureValue>& features){RiverPartitionRequest result;const auto input=row["request"].toObject();
    const auto live=row["liveDonorIndices"].toArray();int index=0;
    for(const auto& entry:input["donors"].toArray()){const auto d=entry.toObject();RiverPartitionDonor donor;donor.countryId=d["countryId"].toString();donor.geometry=geometry(d["geometry"].toObject());donor.geometryRevision=d["geometryRevision"];donor.revisionMode=live.contains(index++)?RiverRevisionMode::LiveCoordinates:RiverRevisionMode::Explicit;result.donors.push_back(donor);}
    for(const auto& entry:row["components"].toArray()){const auto c=entry.toObject();RiverBaseComponent component;component.key=c["key"].toString();component.countryId=c["countryId"].toString();component.componentKey=c["componentKey"].toString();component.polygonIndex=c["polygonIndex"].toInt();component.sourcePolygonIndex=c["sourcePolygonIndex"].toInt();component.geometry=geometry(c["geometry"].toObject());component.attributes=c;result.components.push_back(component);}
    result.hydroRevision=input["hydroRevision"];result.configOverrides=input["config"].toObject();if(input.contains("algorithmRevision"))result.algorithmRevision=input["algorithmRevision"].toString();result.riverFeatures=features;return result;
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);const auto args=app.arguments();
    try{if(args.size()<2)throw std::runtime_error("Usage: m972_river_probe CASES.json [--provider MANIFEST.json]");
        const auto cases=QJsonDocument::fromJson(read(args[1])).array();if(cases.isEmpty())throw std::runtime_error("Required cases missing or empty");
        HydroRuntimeProvider provider;const auto providerArg=args.indexOf("--provider");
        if(providerArg>=0){QString error;if(!provider.open(args.value(providerArg+1),"river-differential",false,error))throw std::runtime_error(error.toStdString());}
        QJsonArray observations,sourceObservations,memorySamples;
        if(args.contains("--cancel-test")){
            std::atomic_bool cancel{false};const auto bytes=QJsonDocument(cases[0].toObject()).toJson(QJsonDocument::Compact);
            auto future=std::async(std::launch::async,[&]{return calculateRiverPartitionsJson(bytes,[&]{return cancel.load();});});
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if(future.wait_for(std::chrono::milliseconds(0))==std::future_status::ready)throw std::runtime_error("Real case completed before cancellation could be exercised");
            cancel.store(true);const auto result=future.get();
            if(result.status!=RiverPartitionStatus::Cancelled||!result.candidates.empty()||!result.components.empty()||!result.donors.empty()||!result.json.isEmpty())throw std::runtime_error("Real cancellation retained result");
            QTextStream(stdout)<<"{\"cancelledWhileRunning\":true,\"resultDiscarded\":true}\n";return 0;
        }
        for(const auto& entry:cases){const auto row=entry.toObject();const auto name=row["name"].toString();logStage(name,"begin");RiverPartitionResult result;
            if(providerArg<0){const auto bytes=QJsonDocument(row).toJson(QJsonDocument::Compact);result=std::async(std::launch::async,[bytes]{return calculateRiverPartitionsJson(bytes);}).get();}
            else {std::vector<GeoBounds> bounds;for(const auto& d:row["request"].toObject()["donors"].toArray()){const auto b=riverPartitionQueryBounds(geometry(d.toObject()["geometry"].toObject()));bounds.insert(bounds.end(),b.begin(),b.end());}
                Project project;project.replace(std::vector<Country>{{"A","Alpha",{{{{0,0},{2,0},{2,2},{0,2},{0,0}}}},0x123456}});JobScheduler scheduler;auto ticket=scheduler.enqueue(project.snapshot(),"river-differential");scheduler.takeNext();
                const auto sourceJob=provider.riverPartitionSourceJob(bounds,{});const auto source=std::async(std::launch::async,[sourceJob,ticket]{return sourceJob(ticket.token());}).get();
                logStage(name,"source-loaded");
                if(source.status!=HydroRiverSourceStatus::Ready||source.diagnostics.failedRiverLoads)throw std::runtime_error(("Required complete source failed: "+source.detail).toStdString());
                QJsonArray features,ids;for(auto id:source.discoveredLogicalIds)ids.append(double(id));for(const auto& f:source.features){QJsonObject feature{{"id",f.id},{"pandolabId",f.pandolabId},{"logicalFid",f.logicalFid?QJsonValue(double(*f.logicalFid)):QJsonValue()},{"geometry",lineJson(f.geometry)}};if(f.sourceFeatureId)feature["sourceFeatureId"]=*f.sourceFeatureId;features.append(feature);}
                sourceObservations.append(QJsonObject{{"name",row["name"]},{"version",source.identity.version},{"indexSha256",source.identity.indexSha256},{"logicalIds",ids},{"features",features}});
                const auto typed=request(row,source.features);result=std::async(std::launch::async,[typed]{return calculateRiverPartitions(typed);}).get();}
            logStage(name,"kernel-returned");memorySamples.append(QJsonObject{{"name",name},{"processKiB",memory()},{"providerCacheBytes",double(provider.cachedBytes())}});
            if(!result.succeeded())throw std::runtime_error((row["name"].toString()+": "+result.detail).toStdString());
            auto observation=QJsonDocument::fromJson(result.json).object();observation["name"]=row["name"];observations.append(observation);
        }
        QTextStream(stdout)<<QJsonDocument(QJsonObject{{"qtVersion",qVersion()},{"observations",observations},{"sources",sourceObservations},{"memorySamples",memorySamples}}).toJson(QJsonDocument::Compact)<<'\n';return 0;
    }catch(const std::exception& e){QTextStream(stderr)<<e.what()<<'\n';return 1;}}
