#include <pandoeditor/timeline-records.h>
#include <algorithm>
#include <iostream>
#include <sstream>

using namespace pandoeditor;
struct TimelineFixture {
    std::string name, expected;
    TimelineRecords input;
    std::vector<TimelineEntityIdentity> entities;
    std::vector<GeometryRef> geometryRefs;
};
#include "timeline-records-fixtures.h"

namespace {
std::string fingerprint(const TimelineRecords& value) {
    std::ostringstream out;
    const auto text = [&](const std::string& s) { out << s.size() << ':' << s; };
    const auto date = [&](const std::optional<std::string>& s) {
        out << (s ? 'S' : 'N'); if(s) text(*s);
    };
    const auto common = [&](const auto& r) { text(r.id); text(r.entityId); date(r.validity.from); date(r.validity.to); };
    out << value.schemaVersion << '/' << value.lifetimes.size() << '/';
    for(const auto& r:value.lifetimes) common(r);
    out << '/' << value.geometryBindings.size() << '/';
    for(const auto& r:value.geometryBindings) { common(r); text(r.geometryRef.id); out << r.geometryRef.version << '/'; }
    out << '/' << value.parentRelations.size() << '/';
    for(const auto& r:value.parentRelations) { common(r); text(r.parentId); text(r.coverageMode); }
    return out.str();
}
}
int main() {
    auto fixtures=timelineFixtures();
    int failures=0;
    const auto check=[&](bool ok,const std::string& message) {
        if(!ok) { ++failures; std::cerr << message << '\n'; }
    };
    check(!fixtures.empty(),"Generated fixtures must not be empty.");
    for(auto& fixture:fixtures) {
        const auto before=fingerprint(fixture.input);
        TimelineValidationContext context{fixture.entities,[&](const GeometryRef& ref) {
            return std::find(fixture.geometryRefs.begin(),fixture.geometryRefs.end(),ref)!=fixture.geometryRefs.end();
        }};
        std::string actual="OK";
        try {
            const auto normalized=normalizeTimelineRecords(fixture.input,context);
            auto expected=fixture.input;
            const auto normalizeDates=[](auto& records) {
                for(auto& record:records) {
                    if(record.validity.from) record.validity.from=parseTemporal(*record.validity.from).canonical;
                    if(record.validity.to) record.validity.to=parseTemporal(*record.validity.to).canonical;
                }
            };
            normalizeDates(expected.lifetimes); normalizeDates(expected.geometryBindings); normalizeDates(expected.parentRelations);
            check(fingerprint(normalized)==fingerprint(expected),fixture.name+": record content changed beyond endpoint normalization");
            check(fingerprint(normalizeTimelineRecords(normalized,context))==fingerprint(normalized),fixture.name+": normalization is not idempotent");
            if(fixture.name=="normalized endpoints retain precision") {
                check(normalized.lifetimes.at(1).validity.from=="1914-07",fixture.name+": month precision lost");
                check(normalized.lifetimes.at(1).validity.to=="1915-02-03",fixture.name+": date precision lost");
            }
            auto reversed=fixture.input;
            std::reverse(reversed.lifetimes.begin(),reversed.lifetimes.end());
            std::reverse(reversed.geometryBindings.begin(),reversed.geometryBindings.end());
            std::reverse(reversed.parentRelations.begin(),reversed.parentRelations.end());
            normalizeTimelineRecords(reversed,context);
        } catch(const TimelineError& error) { actual=error.code; }
        catch(const std::exception& error) { actual="UNEXPECTED"; std::cerr << fixture.name << ": " << error.what() << '\n'; }
        check(fingerprint(fixture.input)==before,fixture.name+": input mutated");
        check(actual==fixture.expected,fixture.name+": expected "+fixture.expected+", got "+actual);
        std::cout << fixture.name << '\t' << actual << '\n';
    }
    if(!fixtures.empty()) {
        const auto& row=fixtures.front();
        try { normalizeTimelineRecords(row.input,{row.entities,{}}); check(false,"Missing geometry dependency was accepted."); }
        catch(const TimelineError& e) { check(e.code=="TIMELINE_CONTEXT","Wrong missing-context error."); }
        const auto failure=[](const GeometryRef&)->bool { throw std::runtime_error("repository unavailable"); };
        try { normalizeTimelineRecords(row.input,{row.entities,failure}); check(false,"Repository failure was swallowed."); }
        catch(const TimelineError&) { check(false,"Repository failure became a validation error."); }
        catch(const std::runtime_error& e) { check(std::string(e.what())=="repository unavailable","Repository error changed."); }
    }
    std::cerr << fixtures.size() << " semantic fixtures; " << failures << " failures\n";
    return failures ? 1 : 0;
}
