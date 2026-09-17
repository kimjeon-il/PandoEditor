#pragma once
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QString>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// Unknown scalar tokens never pass through a double. Objects use decoded keys,
// so escaped aliases of a key are rejected as duplicates as well.
namespace losslessjson {
inline void require(bool ok, const char* error) { if (!ok) throw std::invalid_argument(error); }
struct Value {
    enum Kind { Null, Bool, Number, String, Array, Object } kind=Null;
    QByteArray raw="null";
    std::string string;
    std::vector<Value> array;
    std::map<std::string,Value> object;
    static Value obj() { Value v; v.kind=Object; return v; }
    static Value arr() { Value v; v.kind=Array; return v; }
    static Value str(const std::string& s) {
        Value v; v.kind=String; v.string=s;
        auto encoded=QJsonDocument(QJsonArray{QString::fromStdString(s)}).toJson(QJsonDocument::Compact);
        v.raw=encoded.mid(1,encoded.size()-2); return v;
    }
    static Value num(double n) {
        require(std::isfinite(n),"INVALID_JSON: nonfinite number");
        Value v; v.kind=Number;
        auto encoded=QJsonDocument(QJsonArray{n}).toJson(QJsonDocument::Compact);
        v.raw=encoded.mid(1,encoded.size()-2); return v;
    }
    static Value boolean(bool b) { Value v; v.kind=Bool; v.raw=b?"true":"false"; return v; }
    QByteArray encode(int depth=0) const {
        require(depth<=128,"LIMIT_EXCEEDED: encoded JSON nesting exceeds 128");
        if (kind!=Object && kind!=Array) return raw;
        QByteArray out=kind==Object?"{":"["; bool first=true;
        if (kind==Object) for (const auto& [key,value]:object) {
            if (!first) out+=','; first=false;
            out+=str(key).raw; out+=':'; out+=value.encode(depth+1);
        }
        else for (const auto& value:array) { if (!first) out+=','; first=false; out+=value.encode(depth+1); }
        out+=kind==Object?'}':']'; return out;
    }
};
class Parser {
    const QByteArray& data; qsizetype pos=0;
    void ws() { while (pos<data.size() && (data[pos]==' '||data[pos]=='\r'||data[pos]=='\n'||data[pos]=='\t')) ++pos; }
    bool digit() const { return pos<data.size() && data[pos]>='0' && data[pos]<='9'; }
    Value string() {
        const auto start=pos++; bool closed=false;
        while (pos<data.size()) {
            char c=data[pos++];
            if (c=='"') { closed=true; break; }
            require(static_cast<unsigned char>(c)>=32,"INVALID_JSON: control character");
            if (c=='\\') { require(pos<data.size(),"INVALID_JSON: truncated escape"); ++pos; }
        }
        require(closed,"INVALID_JSON: unterminated string");
        Value value; value.kind=Value::String; value.raw=data.mid(start,pos-start);
        QJsonParseError error;
        const auto parsed=QJsonDocument::fromJson("["+value.raw+"]",&error);
        require(error.error==QJsonParseError::NoError && parsed.isArray() && parsed.array().size()==1 && parsed.array()[0].isString(),"INVALID_JSON: invalid string");
        value.string=parsed.array()[0].toString().toStdString(); return value;
    }
    Value value(int depth) {
        require(depth<=128,"LIMIT_EXCEEDED: JSON nesting exceeds 128");
        ws(); require(pos<data.size(),"INVALID_JSON: missing value");
        if (data[pos]=='"') return string();
        if (data[pos]=='{' || data[pos]=='[') {
            const bool object=data[pos++]=='{'; auto result=object?Value::obj():Value::arr();
            const char close=object?'}':']'; ws();
            if (pos<data.size() && data[pos]==close) { ++pos; return result; }
            while (true) {
                ws();
                if (object) {
                    require(pos<data.size() && data[pos]=='"',"INVALID_JSON: expected key");
                    auto key=string().string; ws();
                    require(pos<data.size() && data[pos++]==':',"INVALID_JSON: expected colon");
                    require(result.object.find(key)==result.object.end(),"DUPLICATE_KEY: duplicate JSON key");
                    result.object.emplace(std::move(key),value(depth+1));
                } else result.array.push_back(value(depth+1));
                ws(); require(pos<data.size(),"INVALID_JSON: unclosed container");
                if (data[pos]==close) { ++pos; return result; }
                require(data[pos++]==',',"INVALID_JSON: expected comma");
            }
        }
        for (const auto& token: {QByteArray("null"),QByteArray("true"),QByteArray("false")}) {
            if (data.mid(pos,token.size())==token) {
                pos+=token.size(); Value v; v.kind=token=="null"?Value::Null:Value::Bool; v.raw=token; return v;
            }
        }
        const auto start=pos;
        if (data[pos]=='-') ++pos;
        require(digit(),"INVALID_JSON: expected number");
        if (data[pos]=='0') ++pos; else while (digit()) ++pos;
        if (pos<data.size() && data[pos]=='.') { ++pos; require(digit(),"INVALID_JSON: fraction"); while (digit()) ++pos; }
        if (pos<data.size() && (data[pos]=='e'||data[pos]=='E')) {
            ++pos; if (pos<data.size() && (data[pos]=='+'||data[pos]=='-')) ++pos;
            require(digit(),"INVALID_JSON: exponent"); while (digit()) ++pos;
        }
        Value v; v.kind=Value::Number; v.raw=data.mid(start,pos-start); return v;
    }
public:
    explicit Parser(const QByteArray& bytes):data(bytes) {}
    Value parse() {
        require(data.size()<=256ll*1024*1024,"LIMIT_EXCEEDED: JSON exceeds 256 MiB");
        // Accept a UTF-8 BOM, as Qt's previous JSON reader did.
        if (data.startsWith("\xef\xbb\xbf")) pos=3;
        auto result=value(0); ws(); require(pos==data.size(),"INVALID_JSON: trailing data"); return result;
    }
};
inline Value parse(const QByteArray& bytes) { return Parser(bytes).parse(); }
}
