#include <pandoeditor/temporal.h>
#include <cassert>
#include <stdexcept>
#include <string>

using namespace pandoeditor;
namespace {
template<class F> void invalid(F&& f) {
    bool rejected=false;
    try { f(); } catch(const std::invalid_argument&) { rejected=true; }
    assert(rejected);
}
}
int main() {
    const auto whole=parseTemporal(" 1945 ");
    assert(parseTemporal("\xC2\xA0" "1945" "\xC2\xA0").canonical=="1945");
    assert(whole.canonical=="1945" && whole.precision=="year");
    assert((whole.startKey==TemporalKey{1945,1,1}));
    assert((whole.endKey==TemporalKey{1945,12,31}));
    assert(whole.start==19450101 && whole.end==19451231);
    const auto day=parseTemporal("1945-08-15");
    assert(day.precision=="date" && day.year==1945 && day.month==8 && day.day==15);
    assert(compareTemporal(whole,day,TemporalBoundary::Start,TemporalBoundary::Start)<0);
    assert(compareTemporal(whole,day,TemporalBoundary::End,TemporalBoundary::End)>0);
    assert(parseTemporal("-0001").endKey<parseTemporal("0001").startKey);
    assert(parseTemporal("+12000-01-01").canonical=="+12000-01-01");
    assert(parseTemporal("-0400-02-29").day==29);
    invalid([]{parseTemporal("0");});
    invalid([]{parseTemporal("0000");});
    invalid([]{parseTemporal("1945-2-1");});
    invalid([]{parseTemporal("12000");});
    invalid([]{parseTemporal("1900-02-29");});
    invalid([]{parseTemporal("1945-13-01");});
    assert(normalizeTemporal(std::optional<std::string>{" +12000 "})=="+12000");
    assert(!normalizeTemporal(std::nullopt));

    const auto year=normalizeTemporalInterval("1945","1945");
    assert(temporalContains(year,parseTemporal("1945-12-31")));
    assert(temporalContains(year,parseTemporal("1945-08-15")));
    assert(!temporalContains(year,parseTemporal("1946")));
    const auto onDay=normalizeTemporalInterval("1945-08-15","1945-08-15");
    assert(temporalIntervalsOverlap(year,onDay));
    assert(!temporalIntervalsOverlap(onDay,normalizeTemporalInterval("1945-08-16",std::nullopt)));
    assert(temporalContains(normalizeTemporalInterval(std::nullopt,std::nullopt),parseTemporal("-0001")));
    invalid([]{normalizeTemporalInterval("1946","1945");});
}
