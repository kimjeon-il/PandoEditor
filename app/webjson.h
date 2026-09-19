#pragma once
#include "losslessjson.h"
#include <QString>
#include <QRegularExpression>
#include <algorithm>
#include <set>
#include <limits>
#include <charconv>
#include <array>

// Boundary helpers mirror the original JavaScript scalar conversions only on
// interpreted fields. Opaque JSON tokens are never converted to floating point.
namespace webjson {
using V=losslessjson::Value;
inline void require(bool ok,const std::string& error) { if(!ok) throw std::invalid_argument(error); }
inline const V& at(const V& o,const std::string& k) {
    static const V missing;
    auto it=o.object.find(k); return it==o.object.end()?missing:it->second;
}
inline bool has(const V& o,const std::string& k) { return o.kind==V::Object&&o.object.count(k); }
inline bool truth(const V& v) {
    if(v.kind==V::Null) return false;
    if(v.kind==V::Bool) return v.raw=="true";
    if(v.kind==V::String) return !v.string.empty();
    if(v.kind==V::Number) {auto n=v.raw.toDouble();return !std::isnan(n)&&n!=0;}
    return true;
}
// ECMAScript uses fixed notation for decimal exponents [-6,20] and the
// shortest round-tripping decimal. Qt's g/16 would change numeric legacy IDs.
inline std::string jsNumberString(double n) {
    if(std::isnan(n))return "NaN";
    if(std::isinf(n))return n<0?"-Infinity":"Infinity";
    if(n==0)return "0";
    const bool negative=n<0;n=std::abs(n);
    std::array<char,64> buffer{};
    auto result=std::to_chars(buffer.data(),buffer.data()+buffer.size(),n,std::chars_format::general);
    require(result.ec==std::errc{},"INVALID_NUMBER: decimal conversion");
    std::string raw(buffer.data(),result.ptr),digits;
    auto e=raw.find('e');int exponent=e==std::string::npos?0:std::stoi(raw.substr(e+1));
    auto mantissa=raw.substr(0,e);auto point=mantissa.find('.');
    int decimal=point==std::string::npos?static_cast<int>(mantissa.size()):static_cast<int>(point);
    for(char c:mantissa)if(c!='.')digits+=c;
    decimal+=exponent;
    while(digits.size()>1&&digits.front()=='0') {digits.erase(0,1);--decimal;}
    while(digits.size()>1&&digits.back()=='0')digits.pop_back();
    std::string output;
    if(decimal>0&&decimal<=21) {
        output=digits;
        if(static_cast<int>(digits.size())<=decimal)output.append(decimal-digits.size(),'0');
        else output.insert(static_cast<std::size_t>(decimal),".");
    } else if(decimal<=0&&decimal>-6)output="0."+std::string(-decimal,'0')+digits;
    else {
        output=digits.substr(0,1);if(digits.size()>1)output+="."+digits.substr(1);
        output+="e";if(decimal-1>=0)output+="+";output+=std::to_string(decimal-1);
    }
    return negative?"-"+output:output;
}
inline std::string jsString(const V& v) {
    if(v.kind==V::String)return v.string;
    if(v.kind==V::Null)return {};
    if(v.kind==V::Bool)return v.raw.toStdString();
    if(v.kind==V::Number)return jsNumberString(v.raw.toDouble());
    if(v.kind==V::Array) {std::string s;for(std::size_t i=0;i<v.array.size();++i){if(i)s+=",";s+=jsString(v.array[i]);}return s;}
    return "[object Object]";
}
inline QString jsTrim(QString s) {
    auto white=[](QChar c) {
        const auto u=c.unicode();
        return (u>=0x9&&u<=0xd)||u==0x20||u==0xa0||u==0x1680||
            (u>=0x2000&&u<=0x200a)||u==0x2028||u==0x2029||u==0x202f||u==0x205f||u==0x3000||u==0xfeff;
    };
    qsizetype first=0,last=s.size();
    while(first<last&&white(s[first]))++first;
    while(last>first&&white(s[last-1]))--last;
    return s.mid(first,last-first);
}
inline std::string text(const V& v) {return jsTrim(QString::fromStdString(jsString(v))).toStdString();}
inline const V& either(const V& a,const V& b) { return truth(a)?a:b; }
inline bool isFalse(const V& v) { return v.kind==V::Bool&&v.raw=="false"; }
inline bool isTrue(const V& v) { return v.kind==V::Bool&&v.raw=="true"; }
inline double number(const V& v,double fallback=std::numeric_limits<double>::quiet_NaN()) {
    if(v.kind==V::Null) return 0;
    if(v.kind==V::Bool) return isTrue(v)?1:0;
    if(v.kind==V::Number||v.kind==V::String) {
        const auto s=v.kind==V::String?QString::fromStdString(v.string):QString::fromUtf8(v.raw);
        if(s.trimmed().isEmpty()) return 0;
        bool ok=false;double n=s.toDouble(&ok);return ok&&std::isfinite(n)?n:fallback;
    }
    return fallback;
}
inline V obj(std::initializer_list<std::pair<const std::string,V>> values={}) {V o=V::obj();o.object=values;return o;}
inline V arr(std::initializer_list<V> values={}) {V a=V::arr();a.array=values;return a;}
inline V objectOrEmpty(const V& v) { return v.kind==V::Object?v:V::obj(); }
inline const std::vector<V>& array(const V& v,const std::string& path) {
    require(v.kind==V::Array,"INVALID_JSON: "+path+" expected array");return v.array;
}
inline const std::vector<V>& optionalArray(const V& v,const std::string& path) {
    static const std::vector<V> empty;
    if(v.kind==V::Null) return empty;
    return array(v,path);
}
inline std::string pointer(const std::string& base,const std::string& k) {
    std::string r=base+"/";for(auto c:k) r+=c=='~'?"~0":c=='/'?"~1":std::string(1,c);return r;
}
inline void merge(V& a,const V& b) {for(const auto& [k,v]:b.object)a.object[k]=v;}
inline void uniqueAppend(V& a,const V& v) {if(std::none_of(a.array.begin(),a.array.end(),[&](const V& x){return x.encode()==v.encode();}))a.array.push_back(v);}
inline bool uuid(const std::string& s) {
    static const QRegularExpression p("^[0-9a-f]{8}-[0-9a-f]{4}-[1-8][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$",QRegularExpression::CaseInsensitiveOption);
    return p.match(QString::fromStdString(s)).hasMatch();
}
}
