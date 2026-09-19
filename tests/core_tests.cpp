#include <pandoeditor/project.h>
#include <iostream>
#include <stdexcept>

using namespace pandoeditor;
void check(bool value) { if (!value) throw std::runtime_error("check failed"); }
int main()
{
    try {
        Ring outer{{0,0},{10,0},{10,10},{0,10},{0,0}};
        Ring hole{{3,3},{7,3},{7,7},{3,7},{3,3}};
        Ring island{{20,0},{22,0},{22,2},{20,2},{20,0}};
        std::vector<Country> countries{{"A","Alpha",{{outer,hole},{island}},0x123456},
                                       {"B","Beta",{{{{10,0},{12,0},{12,10},{10,10},{10,0}}}},0xabcdef}};
        Project p;
        p.replace(countries);
        check(p.pick({1,1}) == "A");
        check(p.pick({5,5}).empty());
        check(p.pick({21,1}) == "A");
        check(p.pick({10,1}) == "A"); // first document country wins shared edges
        check(p.pick({3,5}) == "A"); // hole boundary belongs to polygon
        check(p.pick({-1,0}).empty());
        check(!p.dirty());
        check(!p.setColor("A",0x123456));
        check(!p.canUndo());
        check(p.setColor("A",0xff0000));
        check(p.dirty());
        p.markSaved();
        check(!p.dirty());
        check(p.undo() && p.dirty());
        check(p.redo() && !p.dirty());
        check(p.undo());
        check(p.setColor("A",0x00ff00));
        check(!p.canRedo() && p.dirty());
        auto broken = countries;
        broken[1].id = "A";
        bool rejected = false;
        try { p.replace(broken); } catch (const std::exception&) { rejected = true; }
        check(rejected && p.countries()[0].color == 0x00ff00);
        broken = countries;
        broken[0].polygons[0][0][0].x = 181;
        rejected = false;
        try { Project::validate(broken); } catch (const std::exception&) { rejected = true; }
        check(rejected);
        p.replace(countries);
        check(!p.dirty() && !p.canUndo() && !p.canRedo());
        const auto geometryAddress=p.countries()[0].polygons.data();
        check(p.renameCountry("A","  이름  "));
        check(p.country("A")->name=="이름");
        check(p.renameCountry("A"," \t ")); // web empty-name override falls back to the base name
        check(p.undo() && p.country("A")->name=="이름");
        check(p.setMemo("A","메모"));
        check(p.setCountryOpacity("A",0.25));
        check(!p.setCountryOpacity("A",1.1));
        check(p.countries()[0].polygons.data()==geometryAddress);
        p.markSaved();
        check(p.setMemo("A","변경") && p.dirty());
        check(p.undo() && !p.dirty());
        check(p.addLayer("upper","위"));
        check(!p.addLayer("upper","중복"));
        check(p.moveCountry("A","upper"));
        check(!p.removeLayer("upper"));
        check(p.setLayerLocked("upper",true));
        check(!p.editable("A") && p.pick({1,1}).empty());
        check(!p.setColor("A",0) && !p.moveCountry("A","countries"));
        check(!p.moveCountry("B","upper"));
        check(p.setLayerLocked("upper",false));
        check(p.setLayerVisible("upper",false));
        check(p.pick({1,1}).empty());
        check(p.setLayerVisible("upper",true));
        check(p.setCountryOpacity("A",0));
        check(p.pick({1,1}).empty());
        check(p.setCountryOpacity("A",1));
        check(p.pick({1,1})=="A");
        check(p.setLayerOpacity("upper",0));
        check(p.pick({1,1}).empty());
        check(p.undo() && p.pick({1,1})=="A");
        check(p.moveCountry("A","countries"));
        check(p.removeLayer("upper"));
        check(p.undo() && p.layer("upper"));
        check(p.undo() && p.country("A")->layerId=="upper");
        check(p.redo() && p.country("A")->layerId=="countries");
        check(p.redo() && !p.layer("upper"));
        check(!p.removeLayer("countries"));
        check(p.renameLayer("countries","  기본  "));
        check(p.layer("countries")->name=="기본");
        auto overlap=countries;
        overlap[1].polygons=overlap[0].polygons;
        p.replace(overlap);
        check(p.addLayer("top","Top") && p.moveCountry("B","top"));
        check(p.pick({1,1})=="B");
        check(p.moveLayer("top",-1) && p.pick({1,1})=="A");
        p.markSaved();
        check(p.moveLayer("top",1) && p.dirty());
        check(p.undo() && !p.dirty());
        check(p.setMemo("B","branch") && !p.canRedo());
        auto invalid=p.document(); invalid.presentation.membership.at(territorialRef("A"))="missing";
        rejected=false;
        try { p.replace(invalid); } catch(const std::exception&) { rejected=true; }
        check(rejected && p.country("A")->layerId=="countries");
        std::cout << "Core geometry and history tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what(); return 1; }
}
