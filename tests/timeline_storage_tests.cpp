#include <pandoeditor/timeline-storage.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

using namespace pandoeditor;
struct StorageFixture {
    std::string name, expected;
    TimelineStorageSnapshot input;
    std::vector<TimelineEntityIdentity> entities;
};
#include "timeline-storage-fixtures.h"

namespace {
// Test output only: production JSON decoding/encoding remains with the project codec.
void quoted(std::ostream& out,const std::string& text) {
    out << '"';
    for(const unsigned char c:text) {
        if(c=='"'||c=='\\') out << '\\' << char(c);
        else if(c<32) out << "\\u00" << "0123456789abcdef"[c>>4] << "0123456789abcdef"[c&15];
        else out << char(c);
    }
    out << '"';
}
void date(std::ostream& out,const std::optional<std::string>& value) {
    if(value) quoted(out,*value); else out << "null";
}
void reference(std::ostream& out,const GeometryRef& value) {
    out << "{\"id\":";quoted(out,value.id);out << ",\"version\":" << value.version << '}';
}
void coordinates(std::ostream& out,Point value) { out << '[' << value.x << ',' << value.y << ']'; }
template<class T> void coordinates(std::ostream& out,const std::vector<T>& values) {
    out << '[';bool first=true;
    for(const auto& value:values) {if(!first)out << ',';first=false;coordinates(out,value);}
    out << ']';
}
void geometry(std::ostream& out,const Geometry& value) {
    out << "{\"type\":";quoted(out,value.type);out << ",\"coordinates\":";
    if(value.type=="Point") coordinates(out,value.points.at(0));
    else if(value.type=="MultiPoint") coordinates(out,value.points);
    else if(value.type=="LineString") coordinates(out,value.lines.at(0));
    else if(value.type=="MultiLineString") coordinates(out,value.lines);
    else if(value.type=="Polygon") coordinates(out,value.polygons.at(0));
    else coordinates(out,value.polygons);
    out << '}';
}
template<class T> void common(std::ostream& out,const T& value) {
    out << "{\"id\":";quoted(out,value.id);out << ",\"entityId\":";quoted(out,value.entityId);
    out << ",\"validFrom\":";date(out,value.validity.from);out << ",\"validTo\":";date(out,value.validity.to);
}
std::string json(const TimelineStorageSnapshot& value) {
    std::ostringstream out;out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "{\"schemaVersion\":" << value.schemaVersion << ",\"records\":{\"schemaVersion\":" << value.records.schemaVersion;
    out << ",\"lifetimes\":[";bool first=true;
    for(const auto& row:value.records.lifetimes) {if(!first)out << ',';first=false;common(out,row);out << '}';}
    out << "],\"geometryBindings\":[";first=true;
    for(const auto& row:value.records.geometryBindings) {if(!first)out << ',';first=false;common(out,row);out << ",\"geometryRef\":";reference(out,row.geometryRef);out << '}';}
    out << "],\"parentRelations\":[";first=true;
    for(const auto& row:value.records.parentRelations) {if(!first)out << ',';first=false;common(out,row);out << ",\"parentId\":";quoted(out,row.parentId);out << ",\"coverageMode\":";quoted(out,row.coverageMode);out << '}';}
    out << "]},\"geometries\":[";first=true;
    for(const auto& row:value.geometries) {
        if(!first)out << ',';
        first=false;out << "{\"id\":";quoted(out,row.ref.id);out << ",\"version\":" << row.ref.version << ",\"geojson\":";
        if(row.geometry)geometry(out,*row.geometry);else out << "null";
        out << '}';
    }
    out << "]}";return out.str();
}
}
int main() {
    const auto fixtures=storageFixtures();int failures=0;
    const auto check=[&](bool ok,const std::string& message) {if(!ok){++failures;std::cerr << message << '\n';}};
    check(!fixtures.empty(),"No storage fixtures generated.");
    for(const auto& fixture:fixtures) {
        const auto before=json(fixture.input);std::string actual="OK", result="null";
        try {
            const auto restored=restoreTimelineStorage(fixture.input,fixture.entities);
            const auto snapshot=snapshotTimelineStorage(restored.records,restored.geometries,fixture.entities);
            check(snapshot.geometries.size()==fixture.input.geometries.size(),fixture.name+": versions were pruned");
            result=json(snapshot);
            const auto reopened=restoreTimelineStorage(snapshot,fixture.entities);
            check(json(snapshotTimelineStorage(reopened.records,reopened.geometries,fixture.entities))==result,fixture.name+": checkpoint round trip changed data");
            for(const auto& row:snapshot.geometries) {
                check(row.geometry==restored.geometries.get(row.ref),fixture.name+": immutable coordinates copied during snapshot");
                const auto copy=reopened.geometries.get(row.ref);
                check(copy && copy!=row.geometry,fixture.name+": restore retained an input coordinate allocation");
            }
        } catch(const TimelineError& error) {actual=error.code;}
        catch(const std::invalid_argument& error) {const std::string message=error.what();actual=message.substr(0,message.find(':'));}
        catch(const std::exception& error) {actual="UNEXPECTED";std::cerr << fixture.name << ": " << error.what() << '\n';}
        check(json(fixture.input)==before,fixture.name+": input changed");
        check(actual==fixture.expected,fixture.name+": expected "+fixture.expected+", got "+actual);
        std::cout << "{\"name\":";quoted(std::cout,fixture.name);std::cout << ",\"verdict\":";quoted(std::cout,actual);std::cout << ",\"snapshot\":" << result << "}\n";
    }
    try {
        const auto& first=fixtures.at(0);auto current=restoreTimelineStorage(first.input,first.entities);
        const auto before=snapshotTimelineStorage(current.records,current.geometries,first.entities);
        const auto unchanged=json(before);auto bad=before;
        // Explicitly erase the past polygon version, regardless of sorted registry order.
        for(auto it=bad.geometries.begin();it!=bad.geometries.end();++it) if(it->ref==GeometryRef{"shape",1}) {bad.geometries.erase(it);break;}
        bool rejected=false;
        try {current=restoreTimelineStorage(bad,first.entities);}catch(const TimelineError& e){rejected=e.code=="TIMELINE_GEOMETRY";}
        check(rejected,"Missing old shape was accepted.");
        check(json(snapshotTimelineStorage(current.records,current.geometries,first.entities))==unchanged,"Failed restore changed current storage.");
        current.geometries.insert({"shape",3},*current.geometries.get({"shape",1}));
        check(before.geometries.size()==4 && current.geometries.versions().size()==5,"Checkpoint followed later insert.");
        current=restoreTimelineStorage(before,first.entities);
        check(!current.geometries.get({"shape",3}),"Restoring old checkpoint retained a later version.");
        bad=before;bad.geometries.front().geometry.reset();rejected=false;
        try {restoreTimelineStorage(bad,first.entities);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Null geometry allocation was accepted.");
    } catch(const std::exception& e) {check(false,std::string("Storage isolation checks: ")+e.what());}
    std::cerr << fixtures.size() << " storage fixtures; " << failures << " failures\n";
    return failures?1:0;
}
