#include <pandoeditor/temporal.h>
#include <iostream>
#include <optional>
#include <string>

using namespace pandoeditor;
namespace {
std::string key(const TemporalKey& k) {
    return std::to_string(k.year)+","+std::to_string(k.month)+","+std::to_string(k.day);
}
std::string bit(bool value){return value?"1":"0";}
std::optional<std::string> endpoint(const char* text) {
    return *text?std::optional<std::string>(text):std::nullopt;
}
}
int main() {
    for(const char* input:{"1945","1945-08-15","-0001","0001","+12000",
                           "-0400-02-29","0000","0","1945-2-1","12000",
                           "2023-02-29"," 1945 ","\xC2\xA0" "1945" "\xC2\xA0",""}) {
        try {
            const auto point=parseTemporal(input);
            std::cout<<"P|"<<point.canonical<<"|"<<point.precision<<"|"
                     <<key(point.startKey)<<"|"<<key(point.endKey)<<'\n';
        } catch(const std::invalid_argument&) { std::cout<<"P|ERR\n"; }
    }
    struct Case {const char* from,*to,*point;};
    for(const auto row:{Case{"1945","1945","1945-08-15"},
                        {"1945-08-15","1945-08-15","1945"},
                        {"1945-08-15","1945-08-15","1945-08-16"},
                        {"-0001","0001","0001-01-01"},
                        {"","1945","1946"}})
        std::cout<<"C|"<<bit(temporalContains(
            normalizeTemporalInterval(endpoint(row.from),endpoint(row.to)),
            parseTemporal(row.point)))<<'\n';
    for(const auto row:{Case{"1945","1945","1945-12-31"},
                        {"1945-08-15","1945-08-15","1945-08-16"}})
        std::cout<<"O|"<<bit(temporalIntervalsOverlap(
            normalizeTemporalInterval(endpoint(row.from),endpoint(row.to)),
            normalizeTemporalInterval(row.point,row.point)))<<'\n';
    const auto year=parseTemporal("1945"),day=parseTemporal("1945-08-15");
    std::cout<<"R|"<<compareTemporal(year,day)<<"|"
             <<compareTemporal(year,day,TemporalBoundary::End,TemporalBoundary::End)<<'\n';
}
