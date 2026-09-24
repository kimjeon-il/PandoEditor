#include <pandoeditor/temporal.h>
#include <pandoeditor/document.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <regex>
#include <stdexcept>

namespace pandoeditor {
namespace {
constexpr auto infinity=std::numeric_limits<std::int64_t>::max()/2;
std::string trim(const std::string& source) {
    static const std::array<const char*,17> unicodeSpace{
        u8"\u00a0",u8"\u1680",u8"\u2000",u8"\u2001",u8"\u2002",u8"\u2003",
        u8"\u2004",u8"\u2005",u8"\u2006",u8"\u2007",u8"\u2008",u8"\u2009",
        u8"\u200a",u8"\u2028",u8"\u2029",u8"\u202f",u8"\u205f"};
    std::size_t first=0,last=source.size();
    auto trimUnicode=[&](bool front){
        for(const auto* token:unicodeSpace) {
            const auto length=std::char_traits<char>::length(token);
            if(last-first<length)continue;
            const auto offset=front?first:last-length;
            if(source.compare(offset,length,token)==0){if(front)first+=length;else last-=length;return true;}
        }
        for(const auto* token:{u8"\u3000",u8"\ufeff"}) {
            const auto length=std::char_traits<char>::length(token);
            if(last-first<length)continue;
            const auto offset=front?first:last-length;
            if(source.compare(offset,length,token)==0){if(front)first+=length;else last-=length;return true;}
        }
        return false;
    };
    while(first<last) {
        if(std::isspace(static_cast<unsigned char>(source[first])))++first;
        else if(!trimUnicode(true))break;
    }
    while(last>first) {
        if(std::isspace(static_cast<unsigned char>(source[last-1])))--last;
        else if(!trimUnicode(false))break;
    }
    return source.substr(first,last-first);
}
int monthDays(int year,int month) {
    const auto magnitude=std::abs(year);
    return month==2 ? ((magnitude%4==0&&(magnitude%100!=0||magnitude%400==0))?29:28)
                    : ((month==4||month==6||month==9||month==11)?30:31);
}
std::optional<TemporalValue> optionalPoint(const std::optional<std::string>& input) {
    if(!input||trim(*input).empty())return std::nullopt;
    return parseTemporal(*input);
}
const TemporalKey& boundary(const TemporalValue& point,TemporalBoundary which) {
    return which==TemporalBoundary::End?point.endKey:point.startKey;
}
}
TemporalValue parseTemporal(const std::string& input) {
    static const std::regex pattern("^([+-]?)([0-9]{4,6})(?:-([0-9]{2})-([0-9]{2}))?$");
    const auto source=trim(input);
    std::smatch match;
    if(!std::regex_match(source,match,pattern))
        throw std::invalid_argument("INVALID_DATE: format");
    const auto sign=match[1].str(),digits=match[2].str();
    if(sign.empty()&&digits.size()!=4)
        throw std::invalid_argument("INVALID_DATE: extended year needs sign");
    const auto magnitude=std::stoi(digits);
    const auto year=sign=="-"?-magnitude:magnitude;
    if(year==0)throw std::invalid_argument("INVALID_DATE: year zero");
    const auto dated=match[3].matched;
    const int month=dated?std::stoi(match[3].str()):1;
    const int day=dated?std::stoi(match[4].str()):1;
    if(month<1||month>12)throw std::invalid_argument("INVALID_DATE: month");
    if(day<1||day>monthDays(year,month))throw std::invalid_argument("INVALID_DATE: day");
    const auto yearKey=static_cast<std::int64_t>(year)*10000;
    TemporalValue point;
    point.text=source;
    point.precision=dated?"date":"year";
    point.canonical=(year<0?"-":sign=="+"?"+":"")+digits+
                    (dated?"-"+match[3].str()+"-"+match[4].str():"");
    point.year=year;
    if(dated){point.month=month;point.day=day;}
    point.startKey={year,month,day};
    point.endKey=dated?point.startKey:TemporalKey{year,12,31};
    point.start=yearKey+month*100+day;
    point.end=dated?point.start:yearKey+1231;
    return point;
}
std::optional<std::string> normalizeTemporal(const std::optional<std::string>& input) {
    auto parsed=optionalPoint(input);
    return parsed?std::optional<std::string>(parsed->canonical):std::nullopt;
}
int compareTemporal(const TemporalValue& left,const TemporalValue& right,
                    TemporalBoundary leftBoundary,TemporalBoundary rightBoundary) {
    const auto& a=boundary(left,leftBoundary);
    const auto& b=boundary(right,rightBoundary);
    return a==b?0:(a<b?-1:1);
}
TemporalInterval normalizeTemporalInterval(const std::optional<std::string>& from,
                                           const std::optional<std::string>& to) {
    TemporalInterval interval;
    interval.start=optionalPoint(from);
    interval.end=optionalPoint(to);
    if(interval.start&&interval.end &&
       compareTemporal(*interval.start,*interval.end,TemporalBoundary::Start,TemporalBoundary::End)>0)
        throw std::invalid_argument("INVALID_DATE: reversed interval");
    if(interval.start)interval.validFrom=interval.start->canonical;
    if(interval.end)interval.validTo=interval.end->canonical;
    return interval;
}
bool temporalContains(const TemporalInterval& interval,const TemporalValue& point) {
    const auto start=interval.start?interval.start:optionalPoint(interval.validFrom);
    const auto end=interval.end?interval.end:optionalPoint(interval.validTo);
    return (!start||compareTemporal(*start,point,TemporalBoundary::Start,TemporalBoundary::End)<=0)
        && (!end||compareTemporal(*end,point,TemporalBoundary::End,TemporalBoundary::Start)>=0);
}
bool temporalIntervalsOverlap(const TemporalInterval& left,const TemporalInterval& right) {
    const auto leftStart=left.start?left.start:optionalPoint(left.validFrom);
    const auto leftEnd=left.end?left.end:optionalPoint(left.validTo);
    const auto rightStart=right.start?right.start:optionalPoint(right.validFrom);
    const auto rightEnd=right.end?right.end:optionalPoint(right.validTo);
    if(leftEnd&&rightStart &&
       compareTemporal(*leftEnd,*rightStart,TemporalBoundary::End,TemporalBoundary::Start)<0)return false;
    if(rightEnd&&leftStart &&
       compareTemporal(*rightEnd,*leftStart,TemporalBoundary::End,TemporalBoundary::Start)<0)return false;
    return true;
}
std::pair<std::int64_t,std::int64_t> temporalBounds(const Validity& validity) {
    const auto interval=normalizeTemporalInterval(validity.from,validity.to);
    return {interval.start?interval.start->start:-infinity,
            interval.end?interval.end->end:infinity};
}
}
