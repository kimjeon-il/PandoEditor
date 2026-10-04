#include <pandoeditor/temporal.h>
#include <pandoeditor/document.h>
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

    // Month precision retains its source precision, never inventing a day.
    const auto month=parseTemporal(" \xC2\xA0" "1914-07" "\xC2\xA0 ");
    assert(month.text=="1914-07" && month.canonical=="1914-07" && month.precision=="month");
    assert(month.year==1914 && month.month==7 && !month.day);
    assert((month.startKey==TemporalKey{1914,7,1}));
    assert((month.endKey==TemporalKey{1914,7,31}));
    assert(month.start==19140701 && month.end==19140731);
    assert(normalizeTemporal(std::optional<std::string>{" 1914-07 "})=="1914-07");
    const int lastDays[]={31,28,31,30,31,30,31,31,30,31,30,31};
    for(int m=1;m<=12;++m) {
        const auto value=parseTemporal("1914-"+std::string(m<10?"0":"")+std::to_string(m));
        assert((value.startKey==TemporalKey{1914,m,1}));
        assert((value.endKey==TemporalKey{1914,m,lastDays[m-1]}));
        assert(value.start==19140000+m*100+1);
        assert(value.end==19140000+m*100+lastDays[m-1]);
    }
    struct February {const char* input;int year,lastDay;};
    for(const auto row:{February{"1915-02",1915,28},{"1916-02",1916,29},
            {"1900-02",1900,28},{"2000-02",2000,29},{"2100-02",2100,28},
            {"-0400-02",-400,29},{"-0100-02",-100,28},{"-0001-02",-1,28},
            {"+12000-02",12000,29}}) {
        const auto value=parseTemporal(row.input);
        assert((value.endKey==TemporalKey{row.year,2,row.lastDay}));
        assert(value.end==static_cast<std::int64_t>(row.year)*10000+200+row.lastDay);
    }
    for(const char* input:{"-0001-12","0001-01","+1914-07","+12000-01","+999999-12","-999999-01"})
        assert(parseTemporal(input).canonical==input);
    assert(compareTemporal(parseTemporal("-0002-12"),parseTemporal("-0001-01"))<0);
    assert(compareTemporal(parseTemporal("-0001-12"),parseTemporal("0001-01"),TemporalBoundary::End)<0);
    for(const char* input:{"1914-00","1914-13","1914-7","1914-","1914-07-","1914-07-1",
            "1914-07-00","1914-04-31","1915-02-29","1914-07-15T00:00:00Z",
            "0000-07","-0000-07","+0000-07","12000-07","+1000000-01","--0001-07"})
        invalid([&]{parseTemporal(input);});
    assert(!whole.month && !whole.day);
    assert(!normalizeTemporal(std::optional<std::string>{" "}));
    invalid([]{parseTemporal("");});
    const auto leapDay=parseTemporal("1916-02-29");
    assert(leapDay.precision=="date" && leapDay.month==2 && leapDay.day==29);
    assert(leapDay.startKey==leapDay.endKey);
    assert(compareTemporal(month,parseTemporal("1914-07-01"))==0);
    assert(compareTemporal(month,parseTemporal("1914-07-31"),TemporalBoundary::End)==0);
    assert(compareTemporal(parseTemporal("1914"),parseTemporal("1914-01"))==0);
    assert(compareTemporal(parseTemporal("1914"),parseTemporal("1914-12"),TemporalBoundary::End,TemporalBoundary::End)==0);
    assert(compareTemporal(month,parseTemporal("1914-08"),TemporalBoundary::End)<0);

    const auto july=normalizeTemporalInterval("1914-07","1914-07");
    assert(july.validFrom=="1914-07" && july.validTo=="1914-07");
    struct Contains {const char* date;bool expected;};
    for(const auto row:{Contains{"1914-06-30",false},{"1914-07-01",true},
            {"1914-07-15",true},{"1914-07-31",true},{"1914-08-01",false}})
        assert(temporalContains(july,parseTemporal(row.date))==row.expected);
    TemporalInterval raw;raw.validFrom="1914-07";raw.validTo="1914-07";
    assert(temporalContains(raw,parseTemporal("1914-07-31")));
    const auto bounds=temporalBounds(Validity{"1914-07","1914-07"});
    assert(bounds.first==19140701 && bounds.second==19140731);
    const auto before=normalizeTemporalInterval(std::nullopt,"1914-06");
    const auto after=normalizeTemporalInterval("1914-07",std::nullopt);
    assert(temporalContains(before,parseTemporal("1914-06-30")));
    assert(!temporalContains(before,parseTemporal("1914-07-01")));
    assert(temporalContains(after,parseTemporal("1914-07-01")));
    assert(!temporalIntervalsOverlap(before,after));
    assert(!temporalIntervalsOverlap(normalizeTemporalInterval(std::nullopt,"-0001-12"),
                                     normalizeTemporalInterval("0001-01",std::nullopt)));
    assert(temporalContains(normalizeTemporalInterval(std::nullopt,std::nullopt),parseTemporal("+999999-12")));
    const auto lastDay=normalizeTemporalInterval("1914-07-31","1914-07-31");
    const auto august=normalizeTemporalInterval("1914-08","1914-08");
    assert(temporalIntervalsOverlap(july,lastDay) && temporalIntervalsOverlap(lastDay,july));
    assert(!temporalIntervalsOverlap(july,august) && !temporalIntervalsOverlap(august,july));
    assert(temporalIntervalsOverlap(july,normalizeTemporalInterval("1914","1914")));
    struct Range {const char* from;const char* to;};
    for(const auto row:{Range{"1914-08","1914-07"},{"1914-08-01","1914-07"},
            {"1914-07","1914-06-30"},{"0001-01","-0001-12"}})
        invalid([&]{normalizeTemporalInterval(row.from,row.to);});
    normalizeTemporalInterval("1914-07-31","1914-07");
    normalizeTemporalInterval("1914-07","1914-07-01");
    // Interval containment is not the future timeline's month-end resolution.
    const auto endedMidMonth=normalizeTemporalInterval(std::nullopt,"1914-07-15");
    assert(temporalContains(endedMidMonth,month));
    assert(!temporalContains(endedMidMonth,parseTemporal("1914-07-31")));
    assert(temporalContains(normalizeTemporalInterval("1914-07-15",std::nullopt),parseTemporal("1914-07-31")));
}
