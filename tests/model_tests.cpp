#include <pandoeditor/project.h>
#include <iostream>
#include <stdexcept>
#include <functional>
using namespace pandoeditor;
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
void rejects(const std::function<void()>& f,const char* message) {
    try { f(); } catch(const std::invalid_argument&) { return; }
    throw std::runtime_error(message);
}
ProjectDocument fixture() {
    ProjectDocument d({{"A","Country",{{{{0,0},{10,0},{10,10},{0,10},{0,0}}}},0x123456}},{{"countries","국가"},{"other","기타"}});
    auto add=[&](std::string id,UnitKind kind) {
        d.units.push_back({id,id,"note",kind,d.units.front().geometry});
        d.presentation.membership[territorialRef(id)]="other";
        d.presentation.objectStyles[territorialRef(id)]={0xabcdef,0.75};
    };
    add("S",UnitKind::Subunit); add("N",UnitKind::Subunit); add("R",UnitKind::Region);
    d.relations.push_back({"base-s",territorialRef("S"),territorialRef("A"),territorialRef("A")});
    d.relations.push_back({"base-n",territorialRef("N"),territorialRef("S"),territorialRef("A")});
    d.relations.push_back({"dated-n",territorialRef("N"),territorialRef("A"),territorialRef("A"),true,{{"1900"},{"1901"}}});
    return d;
}
void referencedEmptyLayerRemovalIsAtomic() {
    for(const auto& status:{std::string("unsupported"),std::string("migrationArchive")}) {
        auto d=fixture(); d.presentation.userLayers.push_back({"empty","Empty"});
        PreservedExtension extension;
        extension.id="layer-dependency"; extension.payload="null"; extension.status=status;
        extension.dependencyKnowledge="known"; extension.dependencies={{"userLayer","empty"}};
        extension.forbiddenEffects={"color"}; d.extensions.push_back(extension);
        Project p; p.replace(d);
        check(p.renameCountry("A","Undo history"),"set up history");
        check(p.setMemo("A","Redo history"),"set up redo"); check(p.undo(),"set up cursor");
        p.markSaved();
        const auto geometry=p.document().geometries.get(p.document().units[0].geometry);
        const auto view=&p.country("A")->name;
        check(!p.removeLayer("empty"),"referenced empty layer deletion must reject");
        check(p.layer("empty")&&p.layers().size()==3,"failed deletion retains layers");
        validateDocument(p.document());
        check(p.canUndo()&&p.canRedo()&&!p.dirty(),"failed deletion retains history and saved state");
        check(p.document().geometries.get(p.document().units[0].geometry)==geometry,"failed deletion shares geometry");
        check(&p.country("A")->name==view,"failed deletion preserves canonical views");
        check(p.redo()&&p.country("A")->memo=="Redo history","redo after rejected removal");
        check(p.undo()&&p.country("A")->memo.empty(),"undo after rejected removal");
    }
}
void calendarTransitionsAreContinuous() {
    for(const auto& transition:std::vector<std::pair<std::string,std::string>>{
            {"1900","1901"},{"1900-02-28","1900-03-01"},
            {"2000-02-28","2000-02-29"},{"2000-02-29","2000-03-01"},
            {"2024-04-30","2024-05-01"},{"-0001-12-31","0001-01-01"},
            {"-0004-02-29","-0004-03-01"}}) {
        ProjectDocument d({{"A","Old country",{{{{0,0},{10,0},{10,10},{0,0}}}},0x123456}},{{"countries","Countries"}});
        d.units[0].validity.to=transition.first;
        d.units.push_back({"B","New country","",UnitKind::Country,d.units[0].geometry,false,{transition.second,{}}});
        d.units.push_back({"S","Continuous subunit","",UnitKind::Subunit,d.units[0].geometry});
        for(const auto& id:{"B","S"}) {
            d.presentation.membership[territorialRef(id)]="countries";
            d.presentation.objectStyles[territorialRef(id)]={0xabcdef,1};
        }
        d.relations.push_back({"old",territorialRef("S"),territorialRef("A"),territorialRef("A"),true,{{},transition.first}});
        d.relations.push_back({"new",territorialRef("S"),territorialRef("B"),territorialRef("B"),true,{transition.second,{}}});
        validateDocument(d);
        check(effectiveRelation(d,"S",parseTemporal(transition.first).end)->parent->id=="A","last old calendar day");
        check(effectiveRelation(d,"S",parseTemporal(transition.second).start)->parent->id=="B","first new calendar day");
        if(transition.first=="1900") {
            d.units[1].validity.from="1901-01-02";
            d.relations[1].validity.from="1901-01-02";
            rejects([&]{validateDocument(d);},"actual uncovered calendar day must still reject");
        }
    }
}
int main() {
    bool regressionsPassed=true;
    try { referencedEmptyLayerRemovalIsAtomic(); }
    catch(const std::exception& e) { std::cerr<<"Layer atomicity regression: "<<e.what()<<'\n'; regressionsPassed=false; }
    try { calendarTransitionsAreContinuous(); }
    catch(const std::exception& e) { std::cerr<<"Calendar transition regression: "<<e.what()<<'\n'; regressionsPassed=false; }
    if(!regressionsPassed) return 1;
    try {
        auto d=fixture(); auto index=validateDocument(d);
        LibraryOrigin origin; origin.libraryId="historical-country:example";
        origin.geometryVersionId="1918";origin.referenceDate="1918";
        origin.sourceId="archive";origin.sourceVersion="pilot-2";
        origin.certainty="medium";origin.datePrecision="year";
        d.units.front().libraryOrigin=origin;
        validateDocument(d);
        check(d.units.front().libraryOrigin->libraryId==origin.libraryId,"typed historical provenance");
        auto invalidOrigin=d;invalidOrigin.units.front().libraryOrigin->libraryId.clear();
        rejects([&]{validateDocument(invalidOrigin);},"empty historical library ID");
        check(index.objects.size()==4,"all units indexed");
        check(index.geometryUsers.at(d.units[0].geometry).size()==4,"shared geometry reverse index");
        check(index.relationsByUnit.at(territorialRef("N")).size()==2,"unit relationship index");
        check(index.children.at(territorialRef("S")).front().id=="N","reverse parent index");
        check(index.dependents.at({"userLayer","other"}).size()==3,"layer reverse index");
        check(effectiveRelation(d,"N",parseTemporal("1900").start)->parent->id=="A","dated replaces base");
        check(effectiveRelation(d,"N",parseTemporal("1902").start)->parent->id=="S","return to base");
        check(effectiveRelationAt(d,"N","1900")->parent->id=="A","date-aware relationship at year precision");
        check(effectiveRelationAt(d,"N","1901-12-31")->parent->id=="A","date-aware inclusive end");
        check(effectiveRelationAt(d,"N","1902-01-01")->parent->id=="S","date-aware return to base");
        rejects([&]{effectiveRelationAt(d,"N","0000");},"date-aware relationship rejects year zero");
        check(parseTemporal("-0044").start==-439899,"BCE ordering");
        check(parseTemporal("-0001-12-31").end<parseTemporal("0001-01-01").start,"no year zero gap");
        check(parseTemporal("+010000-02-29").precision=="date","extended leap date");
        check(parseTemporal("1900").end==19001231,"year inclusive end");
        for(auto bad:{"0000","-0000","10000","2023-02-29","2024-13-01","2024-01-00","+123"})
            rejects([&]{parseTemporal(bad);},"invalid date accepted");
        rejects([&]{temporalBounds({{"1901"},{"1900"}});},"reversed dates");
        auto bad=d; bad.units.push_back(bad.units[0]); rejects([&]{validateDocument(bad);},"duplicate units");
        bad=d; bad.relations[0].parent=territorialRef("missing"); rejects([&]{validateDocument(bad);},"dangling parent");
        bad=d; bad.relations[0].sovereign=territorialRef("R"); rejects([&]{validateDocument(bad);},"noncountry sovereign");
        bad=d; bad.relations[0].parent=territorialRef("N"); rejects([&]{validateDocument(bad);},"parent cycle");
        bad=d; bad.relations.push_back({"overlap",territorialRef("N"),territorialRef("S"),territorialRef("A"),true,{{"1901-12-31"},{"1902"}}});
        rejects([&]{validateDocument(bad);},"inclusive overlap");
        bad.relations.back().validity.from="1902-01-01"; validateDocument(bad);
        bad=d; bad.units[0].validity.from="1900"; rejects([&]{validateDocument(bad);},"inactive parent");
        bad=d; bad.units[0].geometry.version=2; rejects([&]{validateDocument(bad);},"missing geometry version");
        auto g=d.geometries.get(d.units[0].geometry);
        auto copy=d; check(copy.geometries.get(copy.units[0].geometry)==g,"snapshot shares immutable geometry");
        rejects([&]{copy.geometries.insert(copy.units[0].geometry,*g);},"duplicate geometry version");
        auto invalidGeometry=*g; invalidGeometry.polygons[0][0].pop_back();
        rejects([&]{copy.geometries.insert({"bad",1},invalidGeometry);},"open polygon");
        Project project; project.replace(d);
        check(project.countries().size()==1,"country adapter excludes other kinds");
        const auto address=project.country("A")->polygons.data();
        check(project.renameCountry("A","Renamed") && project.setMemo("A","Changed") && project.setColor("A",0xff0000),"property edits");
        check(project.country("A")->polygons.data()==address,"property edits share geometry");
        check(project.document().units[0].name=="Renamed","single canonical ownership");
        project.markSaved(); check(project.undo() && project.dirty(),"undo after save dirty");
        check(project.redo() && !project.dirty(),"redo save clean");
        bad=d; bad.units.push_back(bad.units[0]); rejects([&]{project.replace(bad);},"replace rejects");
        check(project.country("A")->name=="Renamed" && project.canUndo() && !project.dirty(),"failed replace atomic");
        check(project.country("A")->polygons.data()==address,"failed replace geometry stable");
        check(!project.removeLayer("other"),"noncountry membership prevents deletion");
        auto locked=d; locked.units[0].locked=true; project.replace(locked);
        check(!project.editable("A")&&!project.renameCountry("A","Blocked"),"unit lock authoritative");
        auto retained=d; retained.extensions.push_back({"unknown","pandoeditor-project",2,"/future","{\"number\":9007199254740993}"});
        project.replace(retained); check(project.renameCountry("A","Safe"),"unknown data allows safe name edit");
        check(!project.moveCountry("A","other")&&!project.removeLayer("other"),"unknown data guards structure");
        retained.extensions[0].dependencyKnowledge="known";
        retained.extensions[0].dependencies={territorialRef("A")};
        retained.extensions[0].forbiddenEffects={"notes"};
        project.replace(retained); check(!project.setMemo("A","Blocked")&&project.renameCountry("A","Allowed"),"known effect guard");
        std::cout<<"Model, temporal, sharing, migration guard tests passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
