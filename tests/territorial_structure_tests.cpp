#include "territorial_fixture.h"
#include <pandoeditor/commands.h>
#include <pandoeditor/project.h>
#include <cassert>
using namespace pandoeditor;
static Geometry box(double x,double y,double n){Geometry g;g.type="Polygon";g.polygons.push_back(Polygon{Ring{{x,y},{x+n,y},{x+n,y+n},{x,y+n},{x,y}}});return g;}
static Project project(){
    ProjectDocument d({{"A","A",box(0,0,10).polygons,0x112233,"",1,"countries"},{"B","B",box(-5,-5,40).polygons,0x112233,"",1,"countries"}},{{"countries","Countries"}});
    const GeometryRef pg{"p",1},g{"s",1};d.geometries.insert(pg,box(1,1,6));d.geometries.insert(g,box(2,2,2));
    appendTerritory(d,{"P","P","",UnitKind::General,false},pg,"A","partition");
    appendTerritory(d,{"S","S","",UnitKind::General,false},g,"P","partition");
    for(auto id:{"P","S"}){d.presentation.membership[territorialRef(id)]="countries";d.presentation.objectStyles[territorialRef(id)]={};}
    Project p;p.replace(d);return p;
}
static PrepareResult prepare(Project& p,const TerritorialMutationPlan& plan,std::optional<GeometryPatch> patch={},const char* command="territorial.relation.parent"){
    CommandArguments args;args.action=ApplyTerritorialMutation{plan,std::move(patch)};
    return CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,command,args));
}
int main(){
    auto p=project();auto plan=CommandProcessor::planTerritorial(p,ChangeParentIntent{territorialRef("S"),territorialRef("A")});
    assert(plan.ok());auto prepared=prepare(p,*plan.plan);assert(prepared.ok());assert(CommandProcessor::confirm(p,*prepared.preview).changed());
    assert(staticParentRelation(p.document(),"S").parentId=="A");assert(p.undo());
    CreateTerritorialIntent create{UnitKind::Regional,"R","R",box(30,0,2)};
    auto cp=CommandProcessor::planTerritorial(p,create);assert(cp.ok());prepared=prepare(p,*cp.plan,{},"territorial.create");
    assert(prepared.ok());assert(CommandProcessor::confirm(p,*prepared.preview).changed());assert(p.index().objects.count(territorialRef("R")));
    const auto revision=p.revision();
    assert(!CommandProcessor::planTerritorial(p,ChangeRegionSovereignIntent{territorialRef("R"),territorialRef("B")}).ok());
    assert(p.revision()==revision&&staticParentRelation(p.document(),"R").parentId.empty());
    assert(!CommandProcessor::planTerritorial(p,ChangeParentIntent{territorialRef("S"),territorialRef("S")}).ok());
    assert(!CommandProcessor::planTerritorial(p,ChangeParentIntent{territorialRef("P"),territorialRef("S")}).ok());
    // Administrative parenting is independent of country/root ownership.
    assert(CommandProcessor::planTerritorial(p,ChangeParentIntent{territorialRef("S"),territorialRef("B")}).ok());
    assert(!CommandProcessor::planTerritorial(p,ChangeParentIntent{territorialRef("R"),territorialRef("B")}).ok());
    assert(!CommandProcessor::planTerritorial(p,DeleteTerritorialIntent{{territorialRef("A")}}).ok());
    auto transfer=CommandProcessor::planTerritorial(p,TransferSubunitIntent{territorialRef("S"),territorialRef("B")});assert(transfer.ok());
    const auto unchanged=p.revision();
    prepared=prepare(p,*transfer.plan,GeometryPatch{p.revision()+1,{{territorialRef("S"),box(2,2,2)},{territorialRef("B"),box(-5,-5,40)}},{},{}},"territorial.geometry.commit");
    assert(!prepared.ok()&&p.revision()==unchanged);
    prepared=prepare(p,*transfer.plan,GeometryPatch{p.revision(),{{territorialRef("S"),box(2,2,2)}},{},{}},"territorial.geometry.commit");
    assert(!prepared.ok()&&p.revision()==unchanged);
    prepared=prepare(p,*transfer.plan,GeometryPatch{p.revision(),{{territorialRef("S"),box(2,2,2)},{territorialRef("S"),box(2,2,2)}},{},{territorialRef("B")}},"territorial.geometry.commit");
    assert(!prepared.ok()&&p.revision()==unchanged);
    prepared=prepare(p,*transfer.plan,GeometryPatch{p.revision(),{{territorialRef("S"),box(2,2,2)},{territorialRef("B"),box(-5,-5,40)},{territorialRef("A"),box(0,0,10)},{territorialRef("P"),box(1,1,6)}},{},{}},"territorial.geometry.commit");
    assert(prepared.ok());assert(CommandProcessor::confirm(p,*prepared.preview).changed());assert(staticParentRelation(p.document(),"S").parentId=="B");assert(p.undo());
    const auto archive=p.document().geometries.versions();const auto ref=staticGeometryBinding(p.document(),"S").geometryRef;
    auto promote=CommandProcessor::planTerritorial(p,ConvertTerritorialTypeIntent{territorialRef("S"),UnitKind::General,{},{},{}});assert(promote.ok());
    assert(promote.plan->geometry.kind==GeometryRequirementKind::None);prepared=prepare(p,*promote.plan);assert(prepared.ok());
    assert(CommandProcessor::confirm(p,*prepared.preview).changed());assert(staticParentRelation(p.document(),"S").parentId.empty());
    assert(staticGeometryBinding(p.document(),"S").geometryRef==ref&&p.document().geometries.versions().size()==archive.size());assert(p.undo());
    auto demote=CommandProcessor::planTerritorial(p,ConvertTerritorialTypeIntent{territorialRef("A"),UnitKind::General,{},territorialRef("B"),"A"});assert(demote.ok());
    prepared=prepare(p,*demote.plan);assert(prepared.ok());assert(CommandProcessor::confirm(p,*prepared.preview).changed());
    assert(p.index().objects.count(territorialRef("A"))&&staticParentRelation(p.document(),"A").parentId=="B");
    assert(staticParentRelation(p.document(),"P").parentId=="A");assert(p.undo());
    CreateTerritorialIntent child{UnitKind::General,"N","New",box(1,1,1)};child.parent=territorialRef("A");
    child.sovereign=territorialRef("A");assert(!CommandProcessor::planTerritorial(p,child).ok());child.sovereign.reset();
    auto created=CommandProcessor::planTerritorial(p,child);assert(created.ok());prepared=prepare(p,*created.plan,{},"territorial.create");
    assert(prepared.ok());assert(CommandProcessor::confirm(p,*prepared.preview).changed());assert(p.index().objects.count(territorialRef("N")));assert(p.undo());
    child.id="outside";child.geometry=box(100,0,1);assert(!CommandProcessor::planTerritorial(p,child).ok());
    assert(!CommandProcessor::planTerritorial(p,ConvertTerritorialTypeIntent{territorialRef("A"),UnitKind::General,{},territorialRef("B"),"other-id"}).ok());
    auto waiting=CommandProcessor::planTerritorial(p,TransferSubunitIntent{territorialRef("S"),territorialRef("B")});assert(waiting.ok());
    assert(!prepare(p,*waiting.plan,{},"territorial.geometry.commit").ok());
}
