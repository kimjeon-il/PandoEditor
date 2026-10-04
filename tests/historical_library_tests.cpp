#include <pandoeditor/historicallibrary.h>
#include <cassert>
#include <stdexcept>

using namespace pandoeditor;
namespace {
Geometry square() {
    return {"Polygon",{}, {},{{{{0,0},{1,0},{1,1},{0,1},{0,0}}}}};
}
HistoricalGeometryVersion version(std::string id,std::string from,std::string to) {
    HistoricalGeometryVersion v;
    v.id=std::move(id);v.validity={std::move(from),std::move(to)};v.geometry=square();
    return v;
}
template<class F> bool invalid(const F& fn) {
    try {fn();}catch(const std::invalid_argument&){return true;}
    return false;
}
}
int main() {
    assert(historicalUnitKind("territory")==UnitKind::General);
    assert(historicalUnitKind("admin")==UnitKind::General);
    assert(historicalUnitKind("country")==UnitKind::General);
    assert(invalid([]{historicalUnitKind("arbitrary");}));
    HistoricalEntity entity;
    entity.libraryId="historical-country:example";
    entity.canonicalName="Example";
    entity.displayNames={{"ko","예시"}};
    entity.alternateNames={"Former Example"};
    entity.validity={"1918","2003"};
    entity.geographicRegion="Europe";
    entity.geometryVersions={version("early","1918","1941"),
                             version("middle","1945","1992"),
                             version("late","1992-04-27","2003")};
    WorldSnapshot snapshot;
    snapshot.id="pilot";snapshot.name="Pilot";snapshot.referenceDate="1991";
    snapshot.entityRefs={entity.libraryId,"missing"};
    HistoricalLibrary catalog(2,{entity},{snapshot});
    assert(catalog.list().size()==1);
    assert(catalog.get(entity.libraryId)!=nullptr);
    assert(catalog.getSnapshot("pilot")->entityRefs.size()==2);
    assert(catalog.selectGeometryVersion(entity.libraryId,"1991")->id=="middle");
    assert(catalog.selectGeometryVersion(entity.libraryId,"2000")->id=="late");
    assert(catalog.selectGeometryVersion(entity.libraryId,"1943")->id=="middle");
    assert(catalog.selectGeometryVersion(entity.libraryId,"1900")->id=="early");
    assert(catalog.selectGeometryVersion(entity.libraryId,"")->id=="late");
    auto copy=catalog.instantiate(entity.libraryId,"1991");
    assert(copy.geometryVersionId=="middle" && copy.name=="예시");
    copy.geometry.polygons.front().front().front().x=42;
    assert(catalog.instantiate(entity.libraryId,"1991").geometry.polygons.front().front().front().x==0);
    assert(catalog.instantiate(entity.libraryId,"1991","early").geometryVersionId=="early");
    assert(invalid([&]{catalog.instantiate(entity.libraryId,"1991","missing");}));
    assert(catalog.search({"Former","",HistoricalStatus::All,"",""}).size()==1);
    assert(catalog.search({"예시","",HistoricalStatus::All,"",""}).size()==1);
    assert(catalog.search({"","country",HistoricalStatus::Past,"1991","Europe"}).size()==1);
    assert(catalog.search({"","country",HistoricalStatus::Current,"",""}).empty());
    assert(catalog.search({"","country",HistoricalStatus::Past,"2010",""}).empty());
    HistoricalEntity child=entity;child.libraryId="child";child.parentLibraryId=entity.libraryId;
    HistoricalEntity grandchild=entity;grandchild.libraryId="grandchild";grandchild.parentLibraryId="child";
    HistoricalLibrary family(2,{entity,child,grandchild},{snapshot});
    assert((family.entityRefsWithChildren({entity.libraryId},"none")==std::vector<std::string>{entity.libraryId}));
    assert((family.entityRefsWithChildren({entity.libraryId},"level1")==std::vector<std::string>{entity.libraryId,"child"}));
    assert((family.entityRefsWithChildren({entity.libraryId},"all")==std::vector<std::string>{entity.libraryId,"child","grandchild"}));
    assert((family.entityRefsWithChildren({entity.libraryId,"child"},"all")==std::vector<std::string>{entity.libraryId,"child","grandchild"}));
    assert((family.entityRefsWithChildren({"missing"},"all")==std::vector<std::string>{"missing"}));
    assert(invalid([&]{family.entityRefsWithChildren({entity.libraryId},"unrecognized");}));
    assert(invalid([&]{HistoricalLibrary future(3,{entity},{});}));
    assert(invalid([&]{HistoricalLibrary duplicate(2,{entity,entity},{});}));
}
