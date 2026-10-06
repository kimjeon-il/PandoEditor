#include <pandoeditor/map/geometrysnap.h>
#include "territorial_fixture.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <limits>
using namespace pandoeditor;
using namespace geometrysnap;
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
Geometry polygon(Ring ring){Geometry g;g.type="Polygon";g.polygons={{std::move(ring)}};return g;}
void add(ProjectDocument& d,const std::string& id,Geometry g,bool generic=false){d.geometries.insert({id,1},std::move(g));if(generic){GenericFeature f;f.id=id;f.geometry={id,1};f.locked=true;d.genericFeatures.push_back(f);}else {TerritorialUnit u;u.id=id;u.name=id;u.locked=true;appendTerritory(d,u,{id,1});d.presentation.objectStyles[territorialRef(id)]={};}}
Candidate vertex(Point p,std::string owner){Candidate c;c.kind="vertex";c.coordinate=p;c.ownerIds={owner};return c;}
Candidate edge(Point a,Point b,std::string kind="edge"){Candidate c;c.kind=kind;c.a=a;c.b=b;return c;}
const ProjectPoint identity=[](Point p)->std::optional<Point>{return p;};
void numeric(){
 require(marginForScale(1)==4&&marginForScale(1e9)==.03,"margin clamps");
 require(snapThreshold()==10&&snapThreshold("touch")==18&&snapThreshold("pen")==10,"pointer thresholds");
 require(nodeKey({-0.,-1e-9})=="0.0000000,-0.0000000","JS signed-zero formatting");
 require(nodeKey({1.23456785,-1.23456785})=="1.2345678,-1.2345678","JS binary toFixed tie");
 require(nodeKey({.00000005,-.00000005})=="0.0000000,-0.0000000","JS subdecimal rounding");
}
void resolver(){
 const auto numeric=resolveSnap({0,0},{0,0},{vertex({1.75916951848194,2.823091015452519},"v")},identity);
 require(numeric&&numeric->distancePx==3.3263373665767455,"V8 Math.hypot numeric operation order");
 const auto zero=resolveSnap({0,0},{0,0},{edge({-0.,-0.},{-1.,-1.})},identity);
 require(zero&&zero->segmentT&&!std::signbit(*zero->segmentT)&&std::signbit(zero->coordinate.x),"JS clamp signed zero");
 const auto tiny=std::numeric_limits<double>::denorm_min();const auto subnormal=resolveSnap({0,0},{0,0},{vertex({tiny,tiny},"v")},identity);
 require(subnormal&&subnormal->distancePx==tiny,"subnormal screen distance");
 const auto signedZero=resolveSnap({0,0},{-0.,+0.},{vertex({0.,-0.},"v")},identity);require(signedZero&&signedZero->distancePx==0&&!std::signbit(signedZero->distancePx),"hypot signed zero");
 require(!resolveSnap({0,0},{INFINITY,NAN},{vertex({0,0},"v")},identity),"nonfinite screen distance accepted");
 auto result=resolveSnap({0,0},{0,0},{vertex({2,0},"v"),edge({-2,1},{2,1})},identity);
 require(result&&result->candidate.kind=="edge"&&result->distancePx==1&&result->coordinate.x==0,"nearer edge must beat farther vertex");
 result=resolveSnap({0,0},{0,0},{vertex({1,0},"first"),vertex({-1,0},"second")},identity);
 require(result&&result->candidate.ownerIds==std::vector<std::string>{"first"},"same-kind ties are stable");
 auto intersection=vertex({1,0},"");intersection.kind="intersection";
 result=resolveSnap({0,0},{0,0},{edge({1,0},{1,0},"neighbor"),edge({1,0},{1,0}),edge({1,0},{1,0},"boundary"),intersection,vertex({1,0},"v")},identity);
 require(result&&result->candidate.kind=="vertex","tie priority vertex");
 std::vector<Candidate> priorities{edge({1,0},{1,0},"neighbor"),edge({1,0},{1,0}),edge({1,0},{1,0},"boundary"),intersection};
 for(const auto& expected:std::vector<std::string>{"intersection","boundary","edge","neighbor"}){result=resolveSnap({0,0},{0,0},priorities,identity);require(result&&result->candidate.kind==expected,"exact type priority order");priorities.pop_back();}
 require(resolveSnap({0,0},{0,0},{vertex({10,0},"")},identity).has_value(),"mouse inclusive threshold");
 require(!resolveSnap({0,0},{0,0},{vertex({std::nextafter(10.,11.),0},"")},identity),"mouse outside threshold");
 require(resolveSnap({0,0},{0,0},{vertex({18,0},"")},identity,"touch").has_value(),"touch threshold");
 require(!resolveSnap({0,0},{0,0},{vertex({18,0},"")},identity,"pen"),"pen mouse threshold");
 result=resolveSnap({0,0},{0,0},{edge({179,0},{-179,0})},identity);
 require(result&&result->coordinate.x==0&&result->segmentT==.5&&(*result->segmentEndpoints)[0].x==179,"raw dateline endpoints and interpolation");
}
void candidates(){
 ProjectDocument d;d.documentId="snap";
 add(d,"z",polygon({{0,0},{2,0},{2,2},{0,2},{0,0}}));
 add(d,"a",polygon({{0,0},{1,1},{-1,1},{0,0}}),true);
 Project p;p.replace(d);Index index;index.prepare(p.snapshot());Request request;request.coordinate={0,0};request.margin=1;request.activeOwnerIds={"z"};
 const auto batch=index.collect(request);require(!batch.candidates.empty(),"indexed candidates missing");
 require(batch.candidates[0].kind=="vertex"&&batch.candidates[0].ownerIds==std::vector<std::string>{"z"},"first source owns duplicate vertex");
 bool neighbor=false;for(const auto& c:batch.candidates)if(c.kind=="neighbor"&&c.ownerIds==std::vector<std::string>{"a"})neighbor=true;
 require(neighbor,"locked generic polygon excluded");
 request.sourceKey="draft:1";request.sourceGeometry=std::make_shared<Geometry>(polygon({{-.5,-1},{.5,1},{1,-1},{-.5,-1}}));
 const auto draft=index.collect(request);bool boundary=false,intersection=false;for(const auto& c:draft.candidates){boundary|=c.kind=="boundary"&&c.segmentKey.find("draft:1:")==0&&c.ownerIds==request.activeOwnerIds;intersection|=c.kind=="intersection";}
 require(boundary&&intersection,"draft boundaries and proper intersections");
 const auto warm=index.collect(request);require(warm.diagnostics.geometryIndexBuilds==0,"warm query rebuilt geometry indexes");
 request.activeOwnerIds={"z","z"};request.sourceGeometry=std::make_shared<Geometry>(polygon({{-.5,-1},{.5,1},{-.5,1},{.5,-1},{-.5,-1}}));const auto duplicates=index.collect(request);for(const auto& c:duplicates.candidates)if(c.kind=="intersection")require(std::count(c.ownerIds.begin(),c.ownerIds.end(),"z")==1,"intersection must deduplicate first edge owners too");
}
void conservativeDateline(){ProjectDocument d;d.documentId="wide";add(d,"wide",polygon({{0,0},{120,1},{-120,1},{0,0}}));Project p;p.replace(d);Index index;index.prepare(p.snapshot());Request request;request.coordinate={-60,.5};request.margin=.01;const auto batch=index.collect(request);bool found=false;for(const auto& c:batch.candidates)found|=c.segmentKey=="wide:0:0:2";require(found,"GeoSpatialIndex false negative must be supplemented");}
void insertionOrder(){ProjectDocument d;d.documentId="ordering";const auto g=polygon({{0,0},{1,0},{1,1},{0,0}});add(d,"z",g);add(d,"a",g,true);Project p;p.replace(d);Index index;index.prepare(p.snapshot());Request q;q.coordinate={0,0};q.margin=1;const auto result=index.collect(q);require(!result.candidates.empty()&&result.candidates.front().ownerIds.front()=="z","initial vector source order");index.prepare(p.snapshot());const auto warm=index.collect(q);require(warm.diagnostics.geometryIndexBuilds==0,"unchanged prepare retains geometry indexes");}
void atomicPreparation(){ProjectDocument a;a.documentId="a";add(a,"first",polygon({{0,0},{1,0},{1,1},{0,0}}));Project first;first.replace(a);ProjectDocument b;b.documentId="b";add(b,"second",polygon({{0,0},{1,0},{1,1},{0,0}}));Project second;second.replace(b);Index index;index.prepare(first.snapshot());Request q;q.coordinate={0,0};q.margin=1;const auto result=index.prepareAndCollect(second.snapshot(),q);require(!result.candidates.empty()&&result.candidates.front().ownerIds.front()=="second","atomic collection used wrong snapshot");}

void apply(Project& project,ContentEdit edit){CommandArguments args;args.action=std::move(edit);auto prepared=CommandProcessor::prepare(project,CommandProcessor::makeRequest(project,"content.edit",args));require(prepared.preview.has_value(),prepared.detail.c_str());require(CommandProcessor::confirm(project,*prepared.preview).ok(),"content fixture commit failed");}
void mutationOrder(){
 ProjectDocument d;d.documentId="mutations";const auto g=polygon({{0,0},{1,0},{1,1},{0,0}});add(d,"z",g,true);add(d,"a",g,true);for(auto& f:d.genericFeatures)f.locked=false;Project p;p.replace(d);Index index;Request q;q.coordinate={0,0};q.margin=1;
 auto batch=index.prepareAndCollect(p.snapshot(),q);require(batch.candidates.front().ownerIds.front()=="z","initial generic order");
 auto z=p.document().genericFeatures[0];z.name="metadata change";apply(p,ContentEdit{{"generic","z"},z,{},false});batch=index.prepareAndCollect(p.snapshot(),q);require(batch.candidates.front().ownerIds.front()=="z"&&batch.diagnostics.geometryIndexBuilds==0,"metadata change reordered or rebuilt geometry");
 z.geometry={"z",2};apply(p,ContentEdit{{"generic","z"},z,std::make_pair(z.geometry,g),false});batch=index.prepareAndCollect(p.snapshot(),q);require(batch.candidates.front().ownerIds.front()=="z"&&batch.diagnostics.geometryIndexBuilds==1,"geometry update reordered or rebuilt unrelated geometry");
 apply(p,ContentEdit{{"generic","z"},{},{},false});batch=index.prepareAndCollect(p.snapshot(),q);require(batch.candidates.front().ownerIds.front()=="a","deleted owner remained");require(p.undo(),"fixture deletion undo");batch=index.prepareAndCollect(p.snapshot(),q);require(batch.candidates.front().ownerIds.front()=="a","reinserted owner did not append");
 require(p.redo(),"fixture deletion redo");batch=index.prepareAndCollect(p.snapshot(),q);for(const auto& c:batch.candidates)require(c.ownerIds!=std::vector<std::string>{"z"},"redo retained removed membership");
}
void localWorkBound(){
 ProjectDocument d;d.documentId="bounded";add(d,"local",polygon({{0,0},{1,0},{1,1},{0,0}}));
 for(int i=0;i<500;++i){const double x=40+(i%50)*2,y=30+(i/50)*2;add(d,"far"+std::to_string(i),polygon({{x,y},{x+1,y},{x+1,y+1},{x,y}}),true);}
 Project p;p.replace(d);Index index;Request q;q.coordinate={.1,.1};q.margin=.1;const auto cold=index.prepareAndCollect(p.snapshot(),q);const auto warm=index.collect(q);
 require(cold.diagnostics.nearbyObjects==1&&cold.diagnostics.geometryIndexBuilds==1,"local query prepared distant geometry segments");require(warm.diagnostics.nearbyObjects==1&&warm.diagnostics.geometryIndexBuilds==0&&warm.diagnostics.preparedObjects==0&&warm.diagnostics.visitedSegments<=3&&warm.diagnostics.segmentEntriesExamined<=3,"warm query work scales with distant objects");
 q.sourceKey="draft";q.sourceGeometry=std::make_shared<Geometry>(polygon({{0,0},{1,0},{1,1},{0,0}}));auto source=index.collect(q);require(source.diagnostics.geometryIndexBuilds==1,"source identity not indexed independently");source=index.collect(q);require(source.diagnostics.geometryIndexBuilds==0,"stable source rebuilt");++q.sourceRevision;source=index.collect(q);require(source.diagnostics.geometryIndexBuilds==1,"source revision not invalidated");
}
void segmentQueryOrder(){
 ProjectDocument d;d.documentId="segment-order";Geometry g;g.type="MultiPolygon";g.polygons={{{{179,0},{-179,0},{-179,1},{179,0}}},{{{-179,0},{-178,0},{-178,1},{-179,0}}}};add(d,"seam",g);Project p;p.replace(d);Index index;Request q;q.coordinate={180,0};q.margin=3;const auto batch=index.prepareAndCollect(p.snapshot(),q);
 std::vector<std::string> segments;for(const auto& c:batch.candidates)if(c.a)segments.push_back(c.segmentKey);
 require(segments==std::vector<std::string>{"seam:0:0:2","seam:0:0:1","seam:1:0:0","seam:1:0:2","seam:1:0:1","seam:0:0:0"},"shifted query or cell traversal order differs from web");
}
void overlapAndExclusion(){
 ProjectDocument d;d.documentId="overlap";const auto g=polygon({{0,0},{2,0},{2,2},{0,0}});add(d,"first",g);add(d,"second",g,true);Project p;p.replace(d);Index index;Request q;q.coordinate={1,1};q.margin=3;const auto batch=index.prepareAndCollect(p.snapshot(),q);for(const auto& c:batch.candidates)require(c.kind!="intersection","collinear overlap or endpoint became proper intersection");
 auto v=vertex({0,0},"v");v.nodeKey=nodeKey({0,0});const auto result=resolveSnap({0,0},{0,0},{v,edge({0,0},{1,0})},identity,"mouse",v.nodeKey);require(result&&result->candidate.kind=="edge","excludeNodeKey incorrectly excluded segment or retained vertex");
}

void strictIntersectionThreshold(){
 Project p;Index index;index.prepare(p.snapshot());Request q;q.coordinate={0,0};q.margin=20;q.sourceKey="source";q.activeOwnerIds={"a"};
 const auto crossing=[&](double x){Geometry g;g.type="MultiPolygon";g.polygons={{{{0,0},{10,0}}},{{{x,-1},{x,1}}}};q.sourceGeometry=std::make_shared<Geometry>(g);return index.collect(q);};
 auto batch=crossing(1e-6);for(const auto& c:batch.candidates)require(c.kind!="intersection","intersection at strict endpoint threshold accepted");
 batch=crossing(1.1e-6);int count=0;for(const auto& c:batch.candidates)if(c.kind=="intersection"){++count;require(c.coordinate->x==1.1e-6&&c.coordinate->y==0,"intersection math changed");}require(count==1,"strict interior crossing missing or duplicated");require(batch.diagnostics.visitedSegments==2,"open source rings received synthetic closing edges");
}

void farOutFiniteQuery(){
 ProjectDocument d;d.documentId="far-query";add(d,"wide",polygon({{0,0},{120,1},{-120,1},{0,0}}));Project p;p.replace(d);Index index;index.prepare(p.snapshot());Request q;q.margin=.1;
 for(const auto coordinate:std::vector<Point>{{1e20,0},{-1e20,0},{1e308,0},{0,1e308},{double(std::numeric_limits<int>::max()),0}}){q.coordinate=coordinate;require(index.collect(q).candidates.empty(),"far out finite query produced candidates");}
}

void sourceCacheLifetime(){
 Project p;Index index;index.prepare(p.snapshot());Request q;q.coordinate={0,0};q.margin=.1;q.sourceKey="far";auto source=std::make_shared<Geometry>(polygon({{10,10},{11,10},{11,11},{10,10}}));std::weak_ptr<const Geometry> weak=source;q.sourceGeometry=source;index.collect(q);source.reset();q.sourceGeometry.reset();require(!weak.expired(),"source bounds cache lost geometry identity lifetime");
 q.sourceKey="shifted";q.coordinate={179,0};q.margin=2;q.sourceGeometry=std::make_shared<Geometry>(polygon({{539,-1},{541,-1},{541,1},{539,-1}}));const auto shifted=index.collect(q);require(!shifted.candidates.empty()&&shifted.candidates.front().coordinate->x==539,"valid shifted source endpoints changed or excluded");
 for(int i=0;i<8;++i){q.sourceKey="replacement"+std::to_string(i);q.sourceGeometry=std::make_shared<Geometry>(polygon({{10,10},{11,10},{11,11},{10,10}}));index.collect(q);}require(weak.expired(),"draft geometry retention exceeded eight identities");
}

void rejectNonfiniteSource(){Project p;Index index;index.prepare(p.snapshot());Request q;q.coordinate={0,0};q.margin=2;q.sourceKey="invalid";q.sourceGeometry=std::make_shared<Geometry>(polygon({{NAN,0},{1,0},{1,1},{NAN,0}}));bool rejected=false;try{index.collect(q);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"nonfinite source entered snap index");}

void rejectNonprogressingSource(){
 Project p;Index index;index.prepare(p.snapshot());Request q;q.coordinate={0,0};q.margin=2;q.sourceKey="unrepresentable-source";
 for(const double longitude:{1e20,-1e20}){q.sourceGeometry=std::make_shared<Geometry>(polygon({{0,0},{longitude,1},{0,2},{0,0}}));bool rejected=false;try{index.collect(q);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"nonprogressing longitude unwrap was not rejected");}
}

void rejectUnsupportedNodeKeyMagnitude(){for(const double value:{1e13,-1e13,1e15,1e20,std::numeric_limits<double>::max()}){bool rejected=false;try{nodeKey({value,0});}catch(const std::invalid_argument&){rejected=true;}require(rejected,"unsupported node-key magnitude silently overflowed");}require(nodeKey({540,-540})=="540.0000000,-540.0000000","supported shifted node-key changed");}

void sourceRanksSnapshotMismatch(){
 ProjectDocument d;d.documentId="rank-snapshot";add(d,"owner",polygon({{0,0},{1,0},{1,1},{0,0}}));Project p;p.replace(d);const auto snapshot=p.snapshot();Index index;index.prepare(snapshot);Request q;q.coordinate={0,0};q.margin=1;q.sourceRanks=std::make_shared<SourceRanks>(SourceRanks{{territorialRef("owner"),0}});q.sourceRanksInstance=snapshot.instanceId();q.sourceRanksRevision=snapshot.revision();
 const auto reject=[&](){bool rejected=false;try{index.collect(q);}catch(const std::invalid_argument& e){rejected=std::string(e.what())=="snap source-order snapshot does not match geometry snapshot";}require(rejected,"mismatched source-rank snapshot accepted");};
 q.sourceRanksInstance="another-instance";reject();q.sourceRanksInstance=snapshot.instanceId();++q.sourceRanksRevision;reject();q.sourceRanksRevision=snapshot.revision();
 require(!index.prepareAndCollect(snapshot,q).candidates.empty(),"matching source-rank snapshot rejected");
}

void sourceRanksMissing(){
 ProjectDocument d;d.documentId="rank-missing";const auto g=polygon({{0,0},{1,0},{1,1},{0,0}});add(d,"z",g);add(d,"a",g,true);Project p;p.replace(d);const auto snapshot=p.snapshot();Index index;index.prepare(snapshot);Request q;q.coordinate={0,0};q.margin=1;q.sourceRanksInstance=snapshot.instanceId();q.sourceRanksRevision=snapshot.revision();
 require(index.collect(q).candidates.front().ownerIds==std::vector<std::string>{"z"},"absent rank map lost insertion-order fallback");
 for(const auto& ranks:std::vector<SourceRanks>{{},{ {territorialRef("z"),0} }}){q.sourceRanks=std::make_shared<SourceRanks>(ranks);bool rejected=false;try{index.collect(q);}catch(const std::invalid_argument& e){rejected=std::string(e.what())=="canonical snap source has no insertion rank";}require(rejected,"explicit rank map with missing nearby owner accepted");}
}

void sourceRanksDuplicate(){
 ProjectDocument d;d.documentId="rank-duplicate";const auto g=polygon({{0,0},{1,0},{1,1},{0,0}});add(d,"z",g);add(d,"a",g,true);Project p;p.replace(d);const auto snapshot=p.snapshot();Index index;index.prepare(snapshot);Request q;q.coordinate={0,0};q.margin=1;q.sourceRanksInstance=snapshot.instanceId();q.sourceRanksRevision=snapshot.revision();q.sourceRanks=std::make_shared<SourceRanks>(SourceRanks{{territorialRef("z"),7},{{"generic","a"},7}});
 bool rejected=false;try{index.collect(q);}catch(const std::invalid_argument& e){rejected=std::string(e.what())=="canonical snap sources have duplicate insertion ranks";}require(rejected,"duplicate nearby insertion ranks accepted");
 q.sourceRanks=std::make_shared<SourceRanks>(SourceRanks{{territorialRef("z"),8},{{"generic","a"},7}});require(index.collect(q).candidates.front().ownerIds==std::vector<std::string>{"a"},"valid rank map failed after duplicate rejection");
}

void sourceRanksHighOrder(){
 ProjectDocument d;d.documentId="rank-high";const auto g=polygon({{0,0},{1,0},{1,1},{0,0}});add(d,"first",g);add(d,"middle",g,true);add(d,"a",g,true);add(d,"z",g,true);Project p;p.replace(d);const auto snapshot=p.snapshot();Index index;Request q;q.coordinate={0,0};q.margin=1;q.sourceRanksInstance=snapshot.instanceId();q.sourceRanksRevision=snapshot.revision();const auto maximum=std::numeric_limits<std::uint64_t>::max();q.sourceRanks=std::make_shared<SourceRanks>(SourceRanks{{territorialRef("first"),0},{{"generic","middle"},std::uint64_t{1}<<63},{{"generic","a"},maximum},{{"generic","z"},maximum-1}});
 const auto batch=index.prepareAndCollect(snapshot,q);require(batch.candidates.front().ownerIds==std::vector<std::string>{"first"},"high unsigned insertion ranks changed first duplicate-vertex owner");std::vector<std::string> edgeOwners;for(const auto& candidate:batch.candidates)if(candidate.kind=="edge"&&(edgeOwners.empty()||edgeOwners.back()!=candidate.ownerIds.front()))edgeOwners.push_back(candidate.ownerIds.front());
 require(edgeOwners==std::vector<std::string>{"first","middle","z","a"},"low and adjacent high uint64 ranks were narrowed, rounded or reordered");
}

}
int main(){int failed=0;for(const auto& test:std::vector<std::pair<const char*,void(*)()>>{{"source-ranks-snapshot-mismatch",sourceRanksSnapshotMismatch},{"source-ranks-missing",sourceRanksMissing},{"source-ranks-duplicate",sourceRanksDuplicate},{"source-ranks-high-order",sourceRanksHighOrder},{"reject-unsupported-node-key",rejectUnsupportedNodeKeyMagnitude},{"reject-nonprogressing-source",rejectNonprogressingSource},{"reject-nonfinite-source",rejectNonfiniteSource},{"source-cache-lifetime",sourceCacheLifetime},{"far-out-finite-query",farOutFiniteQuery},{"strict-intersection-threshold",strictIntersectionThreshold},{"mutation-order",mutationOrder},{"local-work-bound",localWorkBound},{"segment-query-order",segmentQueryOrder},{"overlap-and-exclusion",overlapAndExclusion},{"atomic-preparation",atomicPreparation},{"numeric",numeric},{"resolver",resolver},{"candidates",candidates},{"conservative-dateline",conservativeDateline},{"insertion-order",insertionOrder}}){try{test.second();std::printf("PASS %s\n",test.first);}catch(const std::exception& e){++failed;std::fprintf(stderr,"FAIL %s: %s\n",test.first,e.what());}}return failed?1:0;}
