#include <pandoeditor/map/labelengine.h>
#include <pandoeditor/map/projectionengine.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include <algorithm>
namespace {
constexpr double pi=3.141592653589793238462643383279502884;
MapViewState view(){MapViewState v;v.viewportWidth=800;v.viewportHeight=600;v.scale=180/pi;v.translateX=400;v.translateY=300;return v;}
MapLabelSource source(const char* id,double x,double y,double width=20,double height=19){MapLabelSource s;s.ref={"placeBuiltin",id};s.text=id;s.geographic=unprojectFlat(x,y,view());s.width=width;s.height=height;s.priority=id[0]=='a'?90:70;s.collisionGroup="place";return s;}
std::vector<std::string> layout(std::vector<MapLabelSource> sources,double padding=3,std::set<pandoeditor::ObjectRef> selected={}){MapLabelEngine engine;engine.setBuiltinSources(std::move(sources),1);MapLabelLayoutOptions o;o.zoom=3;o.viewportWidth=800;o.viewportHeight=600;o.collisionPadding=padding;o.maxCandidates=2048;o.maxPlaced=2048;std::vector<std::string> result;for(const auto& p:engine.layout(view(),o,selected))result.push_back(p.ref.id);return result;}
void expect(const std::vector<std::string>& actual,std::initializer_list<const char*> expected){std::vector<std::string> wanted;for(const auto* id:expected)wanted.emplace_back(id);if(actual!=wanted)throw std::runtime_error("ordered visible IDs differ from independent fixed-Web literal");}
std::vector<MapLabelSource> many(std::size_t count=2050){std::vector<MapLabelSource> result;for(std::size_t i=0;i<count;++i){std::ostringstream id;id<<"row"<<std::setfill('0')<<std::setw(4)<<i;auto s=source(id.str().c_str(),400,300);s.priority=90;result.push_back(s);}return result;}
MapLabelLayoutOptions largeOptions(){MapLabelLayoutOptions o;o.zoom=3;o.viewportWidth=800;o.viewportHeight=600;o.maxCandidates=2048;o.maxPlaced=4096;return o;}
void require(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
void capEnds(const std::vector<MapLabelPlacement>& placed){require(placed.size()==2048,"fixed Web hard cap is 2048");require(placed.front().ref.id=="row0000"&&placed.back().ref.id=="row2047","canonical first and last capped source IDs");}
}
int main(){int passed=0,failed=0;const auto test=[&](const char* name,const std::function<void()>& body){try{body();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    test("reduced quality keeps selected and pinned outside background limit",[]{auto rows=many(60);for(std::size_t i=0;i<rows.size();++i){rows[i].collisionGroup=rows[i].ref.id;rows[i].priority=double(i);}rows[0].pinned=true;MapLabelEngine e;e.setBuiltinSources(rows,1);auto o=largeOptions();o.labelDensity=.52;const auto placed=e.layout(view(),o,{{"placeBuiltin","row0001"}});require(placed.size()==44&&e.placedRefs().count({"placeBuiltin","row0000"})&&e.placedRefs().count({"placeBuiltin","row0001"}),"Web minimum 42 background plus two protected labels");});
    test("full quality prefilter precedes layout bounds",[]{auto rows=many(2048);for(auto& s:rows)s.width=2000;auto lower=source("lower",400,300);lower.priority=1;rows.push_back(lower);MapLabelEngine e;e.setBuiltinSources(rows,1);auto o=largeOptions();o.labelDensity=1;require(e.layout(view(),o).empty(),"Web app takes 2048 background before layout excludes oversized boxes");});
    test("quality priority ties preserve original candidate order",[]{auto rows=many(60);for(std::size_t i=0;i<rows.size();++i){rows[i].collisionGroup=rows[i].ref.id;rows[i].priority=70;}std::reverse(rows.begin(),rows.end());MapLabelEngine e;e.setBuiltinSources(rows,1);auto o=largeOptions();o.labelDensity=.52;const auto placed=e.layout(view(),o);require(placed.size()==42&&placed.front().ref.id=="row0018"&&placed.back().ref.id=="row0059","Web stable priority prefilter then canonical layout key sorting");});
    // Fixed ebc label-layout.js uses raw boxes, one 3 CSS-pixel gap, inclusive
    // collision boundaries, padded grid queries and unpadded grid insertion.
    // Fixed boxes isolate mechanics; actual Qt font dimensions differ (REPORT).
    test("horizontal four-pixel gap accepts both",[]{expect(layout({source("a",400,300),source("b",424,300)}),{"a","b"});});
    test("horizontal two-pixel gap rejects lower priority",[]{expect(layout({source("a",400,300),source("b",422,300)}),{"a"});});
    test("vertical four-pixel gap accepts both",[]{expect(layout({source("a",400,300),source("b",400,323)}),{"a","b"});});
    test("vertical two-pixel gap rejects lower priority",[]{expect(layout({source("a",400,300),source("b",400,321)}),{"a"});});
    test("four-pixel gap across screen-grid boundary accepts both",[]{expect(layout({source("a",244,300),source("b",268,300)}),{"a","b"});});
    test("two-pixel gap across screen-grid boundary rejects",[]{expect(layout({source("a",244,300),source("b",266,300)}),{"a"});});
    test("exact three-pixel gap still collides",[]{expect(layout({source("a",400,300),source("b",423,300)}),{"a"});});
    test("zero-padding touching boxes still collide",[]{expect(layout({source("a",400,300),source("b",420,300)},0),{"a"});});
    test("different collision groups coexist",[]{auto a=source("a",400,300),b=source("b",400,300);b.collisionGroup="country";expect(layout({a,b}),{"a","b"});});
    test("selected lower priority wins shared collision",[]{expect(layout({source("a",400,300),source("b",400,300)},3,{{"placeBuiltin","b"}}),{"b"});});
    test("pinned lower priority wins shared collision",[]{auto a=source("a",400,300),b=source("b",400,300);b.pinned=true;expect(layout({a,b}),{"b"});});
    test("two selected labels bypass collision",[]{expect(layout({source("a",400,300),source("b",400,300)},3,{{"placeBuiltin","a"},{"placeBuiltin","b"}}),{"a","b"});});
    test("zoom-hidden candidates cannot starve visible source",[]{auto rows=many();for(auto& s:rows)s.minZoom=4;auto visible=source("visible",400,300);visible.priority=10;rows.push_back(visible);expect(layout(rows),{"visible"});});
    test("bounds-hidden spatial cells cannot starve builtin source",[]{auto rows=many();for(auto& s:rows){s.ref.domain="label";s.geographic={45,0};}MapLabelEngine e;e.setSources(rows,17);auto visible=source("visible",400,300);visible.priority=10;e.setBuiltinSources({visible},1);auto v=view();v.scale*=10;const auto result=e.layout(v,largeOptions());require(result.size()==1&&result[0].ref.id=="visible","onscreen source survives high-priority offscreen spatial candidates");require(e.stats().sourceRevision==17&&e.stats().sourceCount==2050,"document source identity is preserved");});
    test("2050 selected sources still obey final hard cap",[]{auto rows=many();std::set<pandoeditor::ObjectRef> selected;for(const auto& s:rows)selected.insert(s.ref);MapLabelEngine e;e.setBuiltinSources(rows,1);capEnds(e.layout(view(),largeOptions(),selected));});
    test("2050 pinned sources still obey final hard cap",[]{auto rows=many();for(auto& s:rows)s.pinned=true;MapLabelEngine e;e.setBuiltinSources(rows,1);capEnds(e.layout(view(),largeOptions()));});
    test("larger caller budget cannot exceed fixed Web hard cap",[]{auto rows=many();for(auto& s:rows)s.collisionGroup=s.ref.id;MapLabelEngine e;e.setBuiltinSources(rows,1);auto o=largeOptions();o.maxCandidates=4096;capEnds(e.layout(view(),o));});
    test("selected zoom-hidden sources cannot starve visible source",[]{auto rows=many();std::set<pandoeditor::ObjectRef> selected;for(auto& s:rows){s.minZoom=4;selected.insert(s.ref);}auto visible=source("visible",400,300);visible.priority=10;rows.push_back(visible);expect(layout(rows,3,selected),{"visible"});});
    test("pinned back-globe sources cannot starve visible source",[]{auto rows=many();for(auto& s:rows){s.geographic={180,0};s.pinned=true;}auto visible=source("visible",400,300);visible.priority=10;rows.push_back(visible);MapLabelEngine e;e.setBuiltinSources(rows,1);auto v=view();v.mode=ProjectionMode::Globe;const auto placed=e.layout(v,largeOptions());require(placed.size()==1&&placed[0].ref.id=="visible","hemisphere filtering precedes the forced candidate cap");});
    test("document and builtin sources share one final cap",[]{auto rows=many(2200);std::vector<MapLabelSource> document,builtin;for(std::size_t i=0;i<rows.size();++i){auto s=rows[i];s.priority=5000-double(i);s.collisionGroup=s.ref.id;if(i<1200){s.ref.domain="label";document.push_back(s);}else builtin.push_back(s);}MapLabelEngine e;e.setSources(document,17);e.setBuiltinSources(builtin,1);auto o=largeOptions();o.maxCandidates=4096;const auto placed=e.layout(view(),o);require(placed.size()==2048&&placed.front().ref.id=="row0000"&&placed.back().ref.id=="row2047","shared canonical ordered cap");require(e.stats().sourceCount==1200&&e.stats().sourceRevision==17,"builtin layout does not rebuild document source storage");});
    test("builtin and UUID use the same fixed Web label key prefix",[]{auto builtin=source("builtin:place:synthetic:capital",400,300),document=source("cccccccc-cccc-4ccc-8ccc-cccccccccccc",400,300);builtin.priority=document.priority=90;document.ref.domain="label";MapLabelEngine e;e.setSources({document},17);e.setBuiltinSources({builtin},1);const auto placed=e.layout(view(),largeOptions());require(placed.size()==1&&placed[0].ref.id==builtin.ref.id,"fixed Web label:builtin key wins equal-priority UUID c key");});
    std::cout<<"place label collision: processed="<<passed+failed<<" passed="<<passed<<" failed="<<failed<<" skip=0\n";return failed?1:0;
}
