#include "m974_snap_fixture.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
using namespace pandoeditor;
using namespace m974snapfixture;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
Geometry triangle(double x,double y){Geometry g;g.type="Polygon";g.polygons={{{{x,y},{x+1,y},{x+1,y+1},{x,y}}}};return g;}
QJsonArray encode(const std::vector<geometrysnap::Candidate>& rows){QJsonArray result;for(const auto& row:rows)result.append(json(row));return result;}
QJsonObject measure(int distant,QJsonArray& expected){
 ProjectDocument document;document.documentId="snap-cpu-diagnostic";
 const auto add=[&](std::string id,Geometry geometry){const GeometryRef ref{id,1};document.geometries.insert(ref,std::move(geometry));GenericFeature feature;feature.id=id;feature.geometry=ref;document.genericFeatures.push_back(std::move(feature));};
 add("local",triangle(0,0));for(int i=0;i<distant;++i)add("distant-"+std::to_string(i),triangle(40+(i%50)*2,30+((i/50)%20)*2));
 Project project;project.replace(std::move(document));geometrysnap::Index index;geometrysnap::Request request;request.coordinate={.1,.1};request.margin=.1;
 const auto start=std::chrono::steady_clock::now();const auto cold=index.prepareAndCollect(project.snapshot(),request);const auto prepared=std::chrono::steady_clock::now();const auto coldCandidates=encode(cold.candidates);if(expected.empty())expected=coldCandidates;require(expected==coldCandidates,"fixture size changed exact candidate output");
 std::vector<double> timings;timings.reserve(100);geometrysnap::Diagnostics maximum;
 for(int i=0;i<100;++i){const auto begin=std::chrono::steady_clock::now();const auto batch=index.collect(request);const auto end=std::chrono::steady_clock::now();timings.push_back(std::chrono::duration<double,std::micro>(end-begin).count());require(encode(batch.candidates)==expected,"warm query changed exact candidate output");require(batch.diagnostics.geometryIndexBuilds==0&&batch.diagnostics.preparedObjects==0,"warm query rebuilt geometry");require(batch.diagnostics.nearbyObjects==1&&batch.diagnostics.segmentEntriesExamined<=3,"warm local work grew with distant geometry");maximum.nearbyObjects=std::max(maximum.nearbyObjects,batch.diagnostics.nearbyObjects);maximum.segmentEntriesExamined=std::max(maximum.segmentEntriesExamined,batch.diagnostics.segmentEntriesExamined);maximum.visitedSegments=std::max(maximum.visitedSegments,batch.diagnostics.visitedSegments);maximum.intersectionTests=std::max(maximum.intersectionTests,batch.diagnostics.intersectionTests);}
 std::sort(timings.begin(),timings.end());return {{"distantPolygons",distant},{"totalPolygons",distant+1},{"sourceVertices",(distant+1)*4},{"sourceCoordinateBytes",qint64((distant+1)*4*sizeof(Point))},{"samples",100},{"preparationAndFirstQueryMs",std::chrono::duration<double,std::milli>(prepared-start).count()},{"medianWarmQueryUs",(timings[49]+timings[50])/2},{"p95WarmQueryUs",timings[94]},{"maximumCounters",QJsonObject{{"nearbyObjects",qint64(maximum.nearbyObjects)},{"segmentEntriesExamined",qint64(maximum.segmentEntriesExamined)},{"visitedSegments",qint64(maximum.visitedSegments)},{"geometryIndexBuilds",0},{"preparedObjects",0},{"intersectionTests",qint64(maximum.intersectionTests)}}},{"candidateCount",expected.size()}};
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{QJsonArray expected,rows;rows.append(measure(500,expected));rows.append(measure(5000,expected));
#ifdef NDEBUG
 const char* build="Release / NDEBUG";
#else
 const char* build="Debug / assertions enabled";
#endif
 #ifdef _MSC_VER
 const auto compiler=QString("MSVC %1").arg(_MSC_VER);
#else
 const auto compiler=QString::fromLatin1(__VERSION__);
#endif
 QJsonObject report{{"schema","native-app-snap-helper-cpu-diagnostic"},{"scope","CPU indexed query only; no old-algorithm speedup or GPU claim"},{"timingsAreGate",false},{"structuralCountersPass",true},{"exactCandidatesMatchAcrossSizes",true},{"compiler",compiler},{"qtRuntime",qVersion()},{"build",build},{"floatingPointContract","-ffp-contract=off or MSVC /fp:strict"},{"samples",rows},{"candidates",expected}};const auto bytes=QJsonDocument(report).toJson(QJsonDocument::Indented);std::fwrite(bytes.data(),1,bytes.size(),stdout);return 0;}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
