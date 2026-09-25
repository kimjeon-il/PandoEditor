#include <pandoeditor/historicallibrary.h>
#include <iostream>
using namespace pandoeditor;
namespace {
Geometry box(){return {"Polygon",{}, {},{{{{0,0},{1,0},{1,1},{0,1},{0,0}}}}};}
HistoricalGeometryVersion version(const char* id,const char* from,const char* to) {
    HistoricalGeometryVersion result;
    result.id=id;result.validity={from,to};result.geometry=box();return result;
}
}
int main() {
    HistoricalEntity entity;
    entity.libraryId="historical-subunit:example";
    entity.type=historicalUnitKind("territory");
    entity.canonicalName="Example";entity.displayNames={{"ko","예시"}};
    entity.alternateNames={"Former Example"};
    entity.validity={"1918","2003"};entity.geographicRegion="Europe";
    entity.geometryVersions={version("early","1918","1941"),
                             version("middle","1945","1992"),
                             version("late","1992-04-27","2003")};
    entity.instantiation.mode="country-territory-priority";
    WorldSnapshot snapshot;
    snapshot.id="pilot";snapshot.referenceDate="1991";
    snapshot.entityRefs={entity.libraryId,"missing"};
    HistoricalLibrary catalog(2,{entity},{snapshot});
    std::cout<<"type|subunit\n";
    for(const auto& date:{"1991","2000","1943","1900",""})
        std::cout<<"version|"<<catalog.selectGeometryVersion(entity.libraryId,date)->id<<'\n';
    std::cout<<"search|"<<catalog.search({"Former","",HistoricalStatus::All,"",""}).size()
             <<"|"<<catalog.search({"","subunit",HistoricalStatus::Past,"1991","Europe"}).size()
             <<"|"<<catalog.search({"","subunit",HistoricalStatus::Current,"",""}).size()<<'\n';
    const auto materialized=catalog.instantiate(entity.libraryId,"1991","early");
    std::cout<<"instantiate|"<<materialized.geometryVersionId<<"|"<<materialized.name
             <<"|"<<materialized.instantiation.mode<<"|"
             <<catalog.getSnapshot("pilot")->entityRefs.size()<<'\n';
    HistoricalEntity child=entity;child.libraryId="child";child.parentLibraryId=entity.libraryId;
    child.instantiation.mode="independent";
    HistoricalEntity grandchild=child;grandchild.libraryId="grandchild";
    grandchild.parentLibraryId="child";
    HistoricalLibrary family(2,{entity,child,grandchild},{snapshot});
    for(const auto& depth:{"none","level1","all"}) {
        std::cout<<"children|"<<depth<<"|";
        const auto refs=family.entityRefsWithChildren({entity.libraryId},depth);
        for(std::size_t i=0;i<refs.size();++i)std::cout<<(i?",":"")<<refs[i];
        std::cout<<'\n';
    }
}
