#include "territorial_fixture.h"
#include <pandoeditor/presentationcommands.h>
#include <iostream>
#include <stdexcept>
using namespace pandoeditor;
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
ProjectDocument fixture(){
    ProjectDocument d({{"A","A",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x112233}},{{"countries","Countries"}});
    for(auto id:{"S","T","R"}){auto u=d.units.front();u.id=id;u.kind=std::string(id)=="R"?UnitKind::Regional:UnitKind::General;appendTerritory(d,u,staticGeometryBinding(d,d.units.front().id).geometryRef,u.kind==UnitKind::General?"A":"");auto ref=territorialRef(id);d.presentation.membership[ref]="countries";d.presentation.objectStyles[ref]={};if(u.kind==UnitKind::General)setFixtureParent(d,ref,territorialRef("A"));}
    return d;
}
int main(){try{
    auto groups=fixture();groups.presentation.webPresentation.hiddenItems={{"countries",{}},{"subunits",{"missing"}},{"labels",{}}};
    normalizePresentation(groups);
    check(groups.presentation.webPresentation.hiddenItems==std::map<std::string,std::set<std::string>>{{"countries",{}},{"subunits",{}},{"labels",{}}},"normalization preserves group presence while pruning missing IDs");
    Project p;p.replace(fixture());
    const auto visibleBefore=p.document().presentation.webPresentation;
    check(PresentationCommandProcessor::apply(p,SetBatchVisibility{{territorialRef("A")},true})==PresentationResult::NoOp,"visible item in absent group is a noop");
    check(p.document().presentation.webPresentation==visibleBefore&&!p.dirty(),"visibility noop does not create absent groups");
    auto absent=fixture();absent.presentation.webPresentation.overlayOrderPresent=false;
    check(!(absent.presentation.webPresentation==fixture().presentation.webPresentation),"absent overlay order differs from present empty order");
    Project noOverlay;noOverlay.replace(absent);
    check(PresentationCommandProcessor::apply(noOverlay,SetPresentationVisibility{"countries",true})==PresentationResult::NoOp&&!noOverlay.document().presentation.webPresentation.overlayOrderPresent,"overlay presence survives unrelated noop");
    absent.presentation.webPresentation.overlayOrder={"regions"};bool rejected=false;try{Project::validate(absent);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"absent overlay order cannot contain hidden values");
    auto apply=[&](PresentationAction a){check(PresentationCommandProcessor::apply(p,a)==PresentationResult::Applied,"presentation applied");};
    apply(SetPresentationVisibility{"subunits",false});
    check(!effectiveMapVisibility(p.document(),territorialRef("S")),"V01 hidden");
    check(p.revision()==0&&!p.canUndo()&&p.dirty(),"presentation independent");
    apply(SetScopedVisibility{"subunits",{territorialRef("S")},true});
    check(effectiveMapVisibility(p.document(),territorialRef("S"))&&!effectiveMapVisibility(p.document(),territorialRef("T")),"V03 scoped lift");
    apply(SetPresentationVisibility{"subunits",false});
    apply(SetBatchVisibility{{territorialRef("T")},true});
    check(!groupVisible(p.document().presentation.webPresentation,"subunits"),"V04 batch keeps master");
    check(PresentationCommandProcessor::apply(p,SetBatchVisibility{{territorialRef("T")},true})==PresentationResult::NoOp,"batch item noop");
    apply(SetScopedVisibility{"subunits",{territorialRef("S")},false});
    check(groupVisible(p.document().presentation.webPresentation,"subunits")&&!itemVisible(p.document().presentation.webPresentation,"subunits","T"),"scoped hide lifts");
    PresentationStyle s;s.opacity=.6;apply(PatchGroupPresentation{"countries",s});
    check(resolvedTerritorialPresentation(p.document(),territorialRef("S")).opacity==.6,"V11 neutral inheritance");
    s.opacity=.5;apply(PatchGroupPresentation{"subunits",s});
    check(resolvedTerritorialPresentation(p.document(),territorialRef("S")).opacity==.5,"V12 override not product");
    p.renameCountry("A","renamed");p.undo();check(p.canRedo(),"redo setup");
    apply(SetPresentationVisibility{"countries",false});auto revision=p.presentationRevision();
    p.redo();check(!groupVisible(p.document().presentation.webPresentation,"countries")&&p.presentationRevision()>revision,"V09 redo retains presentation");
    p.markSaved();p.renameCountry("A","third");apply(SetPresentationVisibility{"countryFlags",false});p.undo();
    check(p.dirty()&&!groupVisible(p.document().presentation.webPresentation,"countryFlags"),"undo saved content retains presentation dirty");
    CommandArguments args;args.action=SetCountryColor{"A",0xff0000};auto prepared=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"country.color",args));check(bool(prepared.preview),"prepared");
    apply(SetPresentationVisibility{"countryFlags",true});check(CommandProcessor::confirm(p,*prepared.preview).changed(),"prepared survives presentation");
    check(groupVisible(p.document().presentation.webPresentation,"countryFlags"),"prepared keeps latest presentation");
    auto d=fixture();d.presentation.webPresentation.objectStyles["territorial:entity:S"].opacity=.3;d.presentation.webPresentation.objectStyles["territorial:entity:R"].opacity=.3;d.presentation.webPresentation.objectOrder={"territorial:entity:R","territorial:entity:S"};p.replace(d);
    s.opacity=.7;apply(PatchGroupPresentation{"subunits",s});apply(PatchGroupPresentation{"regions",s});
    check(resolvedTerritorialPresentation(p.document(),territorialRef("S")).opacity==.7,"V14 subunit propagation");
    check(resolvedTerritorialPresentation(p.document(),territorialRef("R")).opacity==.3,"V15 region keeps override");
    apply(SetBatchVisibility{{territorialRef("S")},false});
    auto deletion=CommandProcessor::planTerritorial(p,DeleteTerritorialIntent{{territorialRef("S")}});check(deletion.ok(),"delete plan");
    CommandArguments remove;remove.action=ApplyTerritorialMutation{*deletion.plan,{}};
    auto deletionPreview=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"territorial.delete",remove));check(bool(deletionPreview.preview),"delete preview");
    check(CommandProcessor::confirm(p,*deletionPreview.preview).changed(),"delete commit");
    check(itemVisible(p.document().presentation.webPresentation,"subunits","S"),"V32 delete prunes visibility");
    check(p.document().presentation.webPresentation.objectOrder==std::vector<std::string>{"territorial:entity:R"},"native delete prunes order hints");
    apply(SetBatchVisibility{{territorialRef("R")},false});
    check(p.undo(),"delete undo");
    check(p.document().presentation.webPresentation.objectOrder==std::vector<std::string>{"territorial:entity:R","territorial:entity:S"},"native delete undo restores order hints");
    check(!itemVisible(p.document().presentation.webPresentation,"subunits","S")&&!itemVisible(p.document().presentation.webPresentation,"regions","R"),"delete undo restores only affected presentation");
    check(p.redo()&&!p.index().objects.count(territorialRef("S")),"delete redo");
    auto invalid=PresentationCommandProcessor::apply(p,SetBatchVisibility{{territorialRef("missing")},false});check(invalid==PresentationResult::InvalidArguments,"invalid atomic rejection");
    std::cout<<"presentation core scenarios passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
