#include <pandoeditor/map/sharedboundary.h>
#include "territorial_fixture.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace pandoeditor;
using sharedboundary::Session;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool samePoint(Point a, Point b) {
    return std::memcmp(&a.x, &b.x, sizeof(double)) == 0 &&
           std::memcmp(&a.y, &b.y, sizeof(double)) == 0;
}
bool sameGeometry(const Geometry& a, const Geometry& b) {
    if (a.type != b.type || a.polygons.size() != b.polygons.size()) return false;
    for (std::size_t p = 0; p < a.polygons.size(); ++p) {
        if (a.polygons[p].size() != b.polygons[p].size()) return false;
        for (std::size_t r = 0; r < a.polygons[p].size(); ++r) {
            const auto& left = a.polygons[p][r];
            const auto& right = b.polygons[p][r];
            if (left.size() != right.size()) return false;
            for (std::size_t v = 0; v < left.size(); ++v)
                if (!samePoint(left[v], right[v])) return false;
        }
    }
    return true;
}
bool sameDrafts(const sharedboundary::Drafts& a, const sharedboundary::Drafts& b) {
    if (a.size() != b.size()) return false;
    for (const auto& [id, geometry] : a) {
        const auto found = b.find(id);
        if (found == b.end() || !sameGeometry(geometry, found->second)) return false;
    }
    return true;
}
Geometry polygon(Ring ring) {
    Geometry geometry;
    geometry.polygons = {{std::move(ring)}};
    return geometry;
}
Geometry box(double x, double y, double w, double h) {
    return polygon({{x,y},{x+w,y},{x+w,y+h},{x,y+h},{x,y}});
}
Geometry geometry(const ProjectDocument& document, const std::string& id) {
    return *document.geometries.get(staticGeometryBinding(document, id).geometryRef);
}
void append(ProjectDocument& document, const std::string& id, Geometry shape,
            const std::string& parent = "", bool locked = false) {
    if (document.documentId.empty()) document.documentId = "m976-boundary-engine-fixture";
    const GeometryRef ref{id, 1};
    document.geometries.insert(ref, std::move(shape));
    appendTerritory(document, {id,id,"",UnitKind::General,locked}, ref, parent);
    document.presentation.objectStyles[territorialRef(id)] = {};
}
ProjectDocument triple() {
    ProjectDocument document;
    append(document, "A", polygon({{0,0},{1,0},{1,.5},{1,1},{0,1},{0,0}}));
    append(document, "B", polygon({{1,0},{2,0},{2,1},{1,1},{1,.5},{1,0}}));
    append(document, "C", polygon({{0,1},{1,1},{2,1},{2,2},{0,2},{0,1}}));
    return document;
}
std::shared_ptr<Session> prepare(const ProjectDocument& document,
                                 std::initializer_list<const char*> ids,
                                 const std::string& seed = "") {
    std::vector<ObjectRef> owners;
    for (const auto* id : ids) owners.push_back(territorialRef(id));
    return Session::prepare(document, validateDocument(document), owners, {}, seed);
}
std::size_t nodeId(const Session& session, Point point) {
    const auto key = sharedboundary::nodeKey(point);
    const auto found = std::find_if(session.nodes().begin(), session.nodes().end(),
                                   [&](const auto& node) { return node.key == key; });
    require(found != session.nodes().end(), "expected shared node missing");
    return std::size_t(found - session.nodes().begin());
}
void quantization() {
    const auto half = sharedboundary::nodeKey({-5e-8, 5e-8});
    require(half.first == 0. && !std::signbit(half.first) && half.second == 1e-7,
            "signed half-grid must round toward positive infinity");
    require(sharedboundary::nodeKey({-15e-8, 15e-8}) == sharedboundary::NodeKey{-1e-7, 2e-7},
            "negative half-grid uses JavaScript rounding");
    require(sharedboundary::nodeKeyText({-5e-8, 1e-7}) == "0,1e-7",
            "quantized zero and exponent text must match JavaScript");
    require(sharedboundary::nodeKeyText({1e-6, 1.1}) == "0.000001,1.1",
            "fixed-format cutoff must stay exact");
}
void topologyOrder() {
    const auto document = triple();
    const auto session = prepare(document, {"C","B","A"});
    require(session->valid(), "three-owner preparation rejected");
    const std::vector<Point> expected{{1,0},{1,.5},{1,1},{0,1},{2,1}};
    require(session->nodes().size() == expected.size(), "shared node inventory changed");
    for (std::size_t i = 0; i < expected.size(); ++i)
        require(samePoint(session->nodes()[i].coordinate, expected[i]), "shared handle order changed");
    const auto& center = session->nodes()[2];
    require(center.owners == std::vector<std::string>{"A","B","C"}, "node owner insertion order changed");
    require(center.refs.size() == 3 && center.virtualRefs.empty(), "actual center references changed");
    const std::vector<std::size_t> indices{3,3,1};
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const auto& ref = center.refs[i];
        require(ref.owner == center.owners[i] && ref.polygon == 0 && ref.ring == 0 &&
                ref.index == indices[i] && ref.t == 0, "source reference identity/order changed");
    }
    const std::vector<std::pair<std::size_t,std::size_t>> endpoints{{0,1},{1,2},{2,3},{4,2}};
    const std::vector<std::vector<std::string>> owners{{"A","B"},{"A","B"},{"A","C"},{"B","C"}};
    require(session->segments().size() == endpoints.size(), "shared segment inventory changed");
    for (std::size_t i = 0; i < endpoints.size(); ++i) {
        const auto& segment = session->segments()[i];
        require(std::make_pair(segment.start,segment.end) == endpoints[i] && segment.owners == owners[i],
                "shared segment direction/owner order changed");
    }
    require(session->changedDrafts().empty() && !session->canUndo(), "preparation mutated draft history");
}
void virtualInterior() {
    auto document = triple();
    auto original = geometry(document, "A");
    original.polygons[0][0].erase(original.polygons[0][0].begin() + 2);
    original.polygons[0].push_back(box(.1,.1,.2,.2).polygons[0][0]);
    original.polygons.push_back(box(-2,0,1,1).polygons[0]);
    document.geometries.insert({"uneven-A",1}, original);
    staticGeometryBinding(document, "A").geometryRef = {"uneven-A",1};
    const auto session = prepare(document, {"C","B","A"});
    require(session->valid(), "uneven multipolygon preparation rejected");
    const auto id = nodeId(*session, {1,.5});
    const auto& node = session->nodes()[id];
    require(node.owners == std::vector<std::string>{"B","A"}, "virtual owner order changed");
    require(node.refs.size() == 1 && node.refs[0].owner == "B" && node.refs[0].index == 4,
            "virtual node actual reference changed");
    require(node.virtualRefs.size() == 1 && node.virtualRefs[0].owner == "A" &&
            node.virtualRefs[0].polygon == 0 && node.virtualRefs[0].ring == 0 &&
            node.virtualRefs[0].index == 1 && node.virtualRefs[0].t == .5,
            "interior insertion reference changed");
    require(session->move(id, {1.1,.5}), "virtual interior move rejected");
    auto expectedA = original;
    expectedA.polygons[0][0].insert(expectedA.polygons[0][0].begin() + 2, {1.1,.5});
    auto expectedB = geometry(document, "B");
    expectedB.polygons[0][0][4] = {1.1,.5};
    require(sameGeometry(session->drafts().at("A"), expectedA), "virtual insertion changed remote rings/components");
    require(sameGeometry(session->drafts().at("B"), expectedB), "actual owner fan-out changed");
    require(sameGeometry(session->drafts().at("C"), geometry(document,"C")), "unmoved selected owner changed");
    require(sameGeometry(geometry(document,"A"), original), "session changed canonical source geometry");
    const auto patches = session->changedDrafts();
    require(patches.size() == 2 && patches[0].owner == territorialRef("B") &&
            patches[1].owner == territorialRef("A"), "changed drafts lost topology owner order");
    require(session->nodes()[id].virtualRefs.empty(), "materialized virtual reference retained");
    require(session->undo() && session->nodes()[id].virtualRefs.size() == 1 &&
            sameGeometry(session->drafts().at("A"), original), "virtual insertion undo did not restore topology");
    require(session->redo() && sameGeometry(session->drafts().at("A"), expectedA), "virtual insertion redo changed geometry");
}
void groupedDragHistory() {
    const auto document = triple();
    const auto session = prepare(document, {"A","B","C"});
    require(session->valid(), "drag preparation rejected");
    const auto id = nodeId(*session, {1,1});
    const auto before = session->drafts();
    const auto originalSegments = session->nodes()[id].incidentSegments;
    require(session->beginDrag(id) && !session->beginDrag(id), "drag reentry guard changed");
    require(!session->move(id, {1,1}) && session->move(id, {1,1.1}) && session->move(id, {1,1.2}),
            "drag movement/no-op policy changed");
    require(!session->move(nodeId(*session,{1,.5}), {1.1,.5}), "drag accepted another node");
    require(session->dragging() && sameDrafts(session->drafts(), before) && session->changedDrafts().empty() &&
            !session->canUndo() && !session->undo() && !session->redo(), "detached drag mutated owner drafts/history");
    const auto active = session->activeSegments();
    require(active.size() == originalSegments.size() && !active.empty(), "incident segment fan-out changed");
    for (std::size_t i = 0; i < active.size(); ++i) {
        auto expected = originalSegments[i];
        if (samePoint(expected.first, {1,1})) expected.first = {1,1.2};
        if (samePoint(expected.second, {1,1})) expected.second = {1,1.2};
        require(samePoint(active[i].first, expected.first) && samePoint(active[i].second, expected.second),
                "active incident segment direction/order changed");
    }
    require(session->endDrag(false) && !session->dragging() && session->activeSegments().empty(),
            "drag release did not materialize");
    const auto patches = session->changedDrafts();
    require(patches.size() == 3, "release lost an actual owner");
    for (std::size_t i = 0; i < patches.size(); ++i)
        require(patches[i].owner == territorialRef(std::vector<std::string>{"A","B","C"}[i]),
                "three-owner patch order changed");
    const auto after = session->drafts();
    for (const auto& [owner, draft] : after) {
        auto expected = before.at(owner);
        for (auto& polygon : expected.polygons) for (auto& ring : polygon)
            for (auto& point : ring) if (samePoint(point,{1,1})) point = {1,1.2};
        require(sameGeometry(draft, expected), "release geometry fan-out changed");
    }
    require(session->undo() && sameDrafts(session->drafts(), before) && session->changedDrafts().empty() &&
            !session->canUndo() && session->canRedo() && !session->undo(), "one gesture was not one undo unit");
    require(session->redo() && sameDrafts(session->drafts(), after) && !session->canRedo(), "drag redo changed result");
    require(session->beginDrag(id) && session->move(id,{1,1.3}) && !session->endDrag(true) &&
            sameDrafts(session->drafts(), after) && samePoint(session->nodes()[id].coordinate,{1,1.2}),
            "cancel did not restore prior completed draft");
    require(session->undo() && sameDrafts(session->drafts(), before) && !session->canUndo(),
            "cancel added a history unit");
    require(session->redo(), "cancel discarded completed redo history");
    session->resetDraft();
    require(sameDrafts(session->drafts(),before) && session->changedDrafts().empty() &&
            !session->canUndo() && !session->canRedo() && samePoint(session->nodes()[id].coordinate,{1,1}),
            "reset did not restore drafts/topology/history");
    require(session->beginDrag(id) && !session->endDrag(false) && !session->canUndo(), "no-op drag added history");
}
void disconnectedPairs() {
    ProjectDocument document;
    append(document,"A",box(0,0,1,1)); append(document,"B",box(1,0,1,1));
    append(document,"U",box(5,0,1,1)); append(document,"V",box(6,0,1,1));
    const auto explicitPairs = prepare(document,{"V","B","U","A"});
    require(explicitPairs->valid() && explicitPairs->drafts().size() == 4 && explicitPairs->segments().size() == 2,
            "explicit disconnected adjacent pairs rejected");
    const auto isolated = prepare(document,{"A","B","U"});
    require(!isolated->valid() && isolated->error() == "BOUNDARY_ISOLATED_OWNER" &&
            isolated->nodes().empty() && isolated->segments().empty(), "isolated owner was not rejected atomically");
    const auto automatic = prepare(document,{"A","B","U","V"},"A");
    require(automatic->valid() && automatic->drafts().size() == 2 && automatic->segments().size() == 1 &&
            automatic->drafts().count("A") && automatic->drafts().count("B"), "auto-seed connected pruning changed");
}
void fixedAndLocked() {
    auto document = triple();
    auto session = prepare(document,{"A","B"});
    require(session->valid(), "two-owner preparation rejected");
    const auto fixed = nodeId(*session,{1,1});
    require(session->nodes()[fixed].fixed && !session->canMove(fixed) &&
            !session->beginDrag(fixed) && !session->move(fixed,{1,1.1}), "unselected third owner became editable");
    bool encounteredFixed = false;
    for (const auto& node : session->nodes()) {
        if (node.fixed) encounteredFixed = true;
        else require(!encounteredFixed, "editable handles no longer precede fixed handles");
    }
    append(document,"P",box(0,0,2,2));
    for (const auto* id : {"A","B","C"}) staticParentRelation(document,id).parentId = "P";
    session = prepare(document,{"A","B","C"});
    require(session->valid() && session->nodes()[nodeId(*session,{1,0})].fixed &&
            !session->nodes()[nodeId(*session,{1,1})].fixed, "same-parent exterior fixed policy changed");
    append(document,"L",box(.8,.2,.2,.6),"A",true);
    session = prepare(document,{"A","B","C"});
    const auto locked = nodeId(*session,{1,.5});
    require(session->valid() && !session->nodes()[locked].fixed && !session->canMove(locked) &&
            !session->beginDrag(locked) && !session->move(locked,{1.1,.5}) && session->changedDrafts().empty(),
            "locked descendant touch no longer blocks movement");
    const auto before = session->drafts();
    require(session->move(nodeId(*session,{1,1}),{1,1.1}) && sameGeometry(geometry(document,"L"),box(.8,.2,.2,.6)) &&
            sameGeometry(geometry(document,"P"),box(0,0,2,2)), "locked/parent source geometry changed");
    require(!sameDrafts(before,session->drafts()), "unlocked shared node failed to move");
}
void destinationOwner() {
    for (const auto delta : {4e-10, 4e-8}) {
        ProjectDocument document;
        append(document,"A",polygon({{0,0},{1,0},{1,1},{1,2},{0,2},{0,0}}));
        append(document,"B",polygon({{1,0},{2,0},{2,2},{1,2},{1+delta,1},{1,0}}));
        const auto session = prepare(document,{"A","B"});
        require(session->valid(), "near-coincident actual owner preparation rejected");
        const auto id = nodeId(*session,{1,1});
        const bool moves = delta > 1e-9;
        require(session->beginDrag(id) && session->move(id,{1+delta,1}) == moves &&
                session->endDrag(false) == moves, "near-coincident drag tolerance changed");
        const auto patches = session->changedDrafts();
        if (!moves) {
            require(patches.empty() && !session->canUndo(), "sub-tolerance drag changed drafts/history");
        } else {
            require(patches.size() == 2 && patches[0].owner == territorialRef("A") &&
                    patches[1].owner == territorialRef("B"), "unchanged destination owner lost ordered patch");
            require(!sameGeometry(session->drafts().at("A"),geometry(document,"A")) &&
                    sameGeometry(session->drafts().at("B"),geometry(document,"B")), "already-destination owner geometry changed");
            require(session->undo() && session->changedDrafts().empty() && session->redo() &&
                    session->changedDrafts().size() == 2, "destination-owner history lost its patch");
        }
    }
}
void cancellationAndInvalidCoordinates() {
    const auto document = triple();
    const std::vector<ObjectRef> owners{territorialRef("A"),territorialRef("B"),territorialRef("C")};
    const auto cancelled = Session::prepare(document,validateDocument(document),owners,[]{return true;});
    require(!cancelled->valid() && cancelled->error() == "BOUNDARY_PREPARATION_CANCELLED" &&
            cancelled->nodes().empty() && cancelled->segments().empty(), "cancelled preparation exposed topology");
    const auto session = prepare(document,{"A","B","C"});
    const auto id = nodeId(*session,{1,1});
    const auto before = session->drafts();
    require(!session->move(id,{std::numeric_limits<double>::quiet_NaN(),0}) &&
            !session->move(id,{0,std::numeric_limits<double>::infinity()}) && !session->move(id,{1,91}) &&
            sameDrafts(session->drafts(),before) && session->changedDrafts().empty() && !session->canUndo(),
            "invalid direct coordinate changed drafts/history");
    require(session->beginDrag(id) && session->move(id,{1,100}) && sameDrafts(session->drafts(),before),
            "detached invalid-latitude movement policy changed");
    require(!session->endDrag(false) && !session->valid() && session->error() == "BOUNDARY_INVALID_COORDINATE" &&
            sameDrafts(session->drafts(),before) && session->changedDrafts().empty() && !session->canUndo() &&
            samePoint(session->nodes()[id].coordinate,{1,1}), "invalid release did not restore original draft");
}
}

int main(int argc, char** argv) {
    const std::vector<std::pair<const char*,void(*)()>> tests{
        {"quantization",quantization}, {"topologyOrder",topologyOrder}, {"virtualInterior",virtualInterior},
        {"groupedDragHistory",groupedDragHistory}, {"disconnectedPairs",disconnectedPairs},
        {"fixedAndLocked",fixedAndLocked}, {"destinationOwner",destinationOwner},
        {"cancellationAndInvalidCoordinates",cancellationAndInvalidCoordinates}
    };
    int failed = 0, ran = 0;
    for (const auto& [name,test] : tests) {
        if (argc > 1 && std::string(argv[1]) != name) continue;
        ++ran;
        try { test(); std::cout << "PASS: " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL: " << name << ": " << error.what() << '\n'; }
    }
    require(ran > 0, "no matching engine boundary test");
    return failed ? 1 : 0;
}
