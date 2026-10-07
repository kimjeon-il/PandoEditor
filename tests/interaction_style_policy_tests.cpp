#include <pandoeditor/map/interactionstylepolicy.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <functional>

// Literal contract tests, independently transcribed from fixed ebc Web;
// expected values do not call the policy under test.
int main() {
 using namespace mapstyle;
 auto equal=[](double a,double b){if(std::abs(a-b)>1e-12)throw std::runtime_error("fixed Web literal mismatch");};
 Options options;FrameContext frame;frame.width=1920;frame.height=929;frame.cssScale=1000;
 const std::vector<std::pair<const char*,std::function<void()>>> tests{
  {"primary",[&]{auto s=interactionStroke(InteractionRole::Primary,options,frame);equal(s.width,2.5);equal(s.alpha,1);equal(s.fillAlpha,.105);}},
  {"secondary",[&]{auto s=interactionStroke(InteractionRole::Secondary,options,frame);equal(s.width,1.5);equal(s.alpha,.72);equal(s.fillAlpha,.063);}},
  {"hover",[&]{auto s=interactionStroke(InteractionRole::Hover,options,frame);equal(s.width,1.5);equal(s.alpha,.85);equal(s.fillAlpha,.035);}},
  {"candidate",[&]{auto s=interactionStroke(InteractionRole::Candidate,options,frame);equal(s.width,1);equal(s.alpha,.45);equal(s.fillAlpha,0);}},
  {"role aliases",[&]{for(auto r:{InteractionRole::Reference,InteractionRole::UnchosenResult})equal(interactionStroke(r,options,frame).alpha,.45);for(auto r:{InteractionRole::SelectedProvider,InteractionRole::SelectedComponent})equal(interactionStroke(r,options,frame).alpha,.72);for(auto r:{InteractionRole::EditTarget,InteractionRole::ChosenResult})equal(interactionStroke(r,options,frame).width,2.5);}},
  {"outline hidden retains fill and hover",[&]{auto o=options;o.outlineVisible=false;auto p=interactionStroke(InteractionRole::Primary,o,frame);equal(p.width,0);equal(p.alpha,0);equal(p.fillAlpha,.105);equal(interactionStroke(InteractionRole::Hover,o,frame).alpha,.85);}},
  {"direct manipulation overrides hidden outline",[&]{auto o=options;o.outlineVisible=false;o.directManipulation=true;equal(interactionStroke(InteractionRole::Primary,o,frame).width,2.5);equal(interactionStroke(InteractionRole::Secondary,o,frame).alpha,.72);}},
  {"light fill and accent",[&]{auto o=options;o.dark=false;auto p=interactionStroke(InteractionRole::Primary,o,frame);equal(p.fillAlpha,.084);if(p.color!=0x315e9d)throw std::runtime_error("light accent");equal(interactionStroke(InteractionRole::Secondary,o,frame).fillAlpha,.049);equal(interactionStroke(InteractionRole::Hover,o,frame).fillAlpha,.028);}},
  {"dark accent and supplied color",[&]{if(interactionStroke(InteractionRole::Primary,options,frame).color!=0xcda95d)throw std::runtime_error("dark accent");auto o=options;o.selectionColor=0x123456;if(interactionStroke(InteractionRole::Hover,o,frame).color!=0x123456)throw std::runtime_error("supplied color");}},
  {"fill clamp and nonfinite fallback",[&]{auto o=options;o.fillStrength=2;equal(interactionStroke(InteractionRole::Primary,o,frame).fillAlpha,.3);o.fillStrength=-1;equal(interactionStroke(InteractionRole::Primary,o,frame).fillAlpha,0);o.fillStrength=std::numeric_limits<double>::infinity();equal(interactionStroke(InteractionRole::Primary,o,frame).fillAlpha,.105);}},
  {"shared boundary dash",[&]{auto o=options;o.sharedBoundary=true;auto p=interactionStroke(InteractionRole::Primary,o,frame);equal(p.dashOn,6);equal(p.dashOff,3);equal(interactionStroke(InteractionRole::Candidate,o,frame).dashOn,0);}},
  {"flat distant stroke floor",[&]{auto f=frame;f.cssScale=100;equal(interactionStrokeScale(f),.45);equal(interactionStroke(InteractionRole::Primary,options,f).width,1.125);}},
  {"flat interpolation",[&]{auto f=frame;f.cssScale=(1920/(2*3.14159265358979323846))*.86;equal(interactionStrokeScale(f),.7);}},
  {"globe uses content short dimension",[&]{auto f=frame;f.flat=false;f.cssScale=929*.455*.86;equal(interactionStrokeScale(f),.7);}},
  {"safe inset changes baseline",[&]{auto f=frame;f.left=100;f.right=200;f.cssScale=(1620/(2*3.14159265358979323846))*.86;equal(interactionStrokeScale(f),.7);}},
  {"invalid viewport defaults to unity",[&]{auto f=frame;f.width=0;equal(interactionStrokeScale(f),1);f=frame;f.cssScale=-1;equal(interactionStrokeScale(f),1);}},
  {"small globe baseline sixty",[&]{auto f=frame;f.flat=false;f.width=10;f.height=10;f.cssScale=60;equal(interactionStrokeScale(f),1);}},
  {"base widths and dashes",[&]{equal(baseStroke(BaseRole::Country).width,.72);equal(baseStroke(BaseRole::CountryInternal).width,1.1);equal(baseStroke(BaseRole::Subunit).width,2);equal(baseStroke(BaseRole::SubunitInternal).width,1.1);equal(baseStroke(BaseRole::SubunitInternal).dashOn,3);equal(baseStroke(BaseRole::SubunitInternal).dashOff,2);equal(baseStroke(BaseRole::Region).width,1.5);equal(baseStroke(BaseRole::Region).dashOn,7);equal(baseStroke(BaseRole::Region).dashOff,3);}},
  {"base width multiplier zero fallback and half floor",[&]{equal(baseStroke(BaseRole::Country,0).width,.72);equal(baseStroke(BaseRole::Country,-2).width,.36);}},
  {"unknown roles refuse",[&]{bool refused=false;try{interactionStroke(static_cast<InteractionRole>(100),options,frame);}catch(const std::invalid_argument&){refused=true;}if(!refused)throw std::runtime_error("unknown role accepted");}}
 };
 int pass=0,fail=0;for(const auto& t:tests)try{t.second();++pass;std::cout<<"PASS "<<t.first<<'\n';}catch(const std::exception& e){++fail;std::cout<<"FAIL "<<t.first<<": "<<e.what()<<'\n';}std::cout<<"processed="<<tests.size()<<" pass="<<pass<<" fail="<<fail<<" skip=0\n";return fail?1:0;
}
