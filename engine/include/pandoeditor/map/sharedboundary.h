#pragma once
#include <pandoeditor/document.h>
#include <pandoeditor/territorialmutation.h>
#include <functional>
#include <memory>
#include <set>
#include <optional>

// Narrow native adapter of Pando boundary-topology.js / boundary-preparation.js
// at 53dbd3c1e84f04cf0332adc1b7a32f290b2a4f47 (unchanged at ad78780).
// Precision 7, epsilon 1e-7, actual/virtual reference and insertion ordering
// follow that source. Canonical project data is never changed by this session.
namespace sharedboundary {
using NodeKey=std::pair<double,double>;
NodeKey nodeKey(pandoeditor::Point);
std::string nodeKeyText(pandoeditor::Point);
struct Reference { std::string owner;std::size_t polygon=0,ring=0,index=0;double t=0; };
struct Node {
    NodeKey key;
    pandoeditor::Point coordinate;
    std::vector<std::string> owners;
    std::vector<Reference> refs,virtualRefs;
    std::vector<std::pair<pandoeditor::Point,pandoeditor::Point>> incidentSegments;
    bool fixed=false;
};
struct Segment {std::size_t start=0,end=0;std::vector<std::string> owners;};
using Drafts=std::map<std::string,pandoeditor::Geometry>;
class Session {
public:
    static std::string eligibility(const pandoeditor::ProjectDocument&,const pandoeditor::DocumentIndex&,const std::vector<pandoeditor::ObjectRef>&,bool checkAncestors=true);
    static std::shared_ptr<Session> prepare(const pandoeditor::ProjectDocument&,const pandoeditor::DocumentIndex&,const std::vector<pandoeditor::ObjectRef>&,const std::function<bool()>& cancelled={},const std::string& autoSeed={});
    const std::vector<Node>& nodes() const {return nodes_;}
    const std::vector<Segment>& segments() const {return segments_;}
    const Drafts& drafts() const {return drafts_;}
    const std::string& error() const {return error_;}
    bool valid() const {return error_.empty()&&!segments_.empty();}
    bool canMove(std::size_t) const;
    bool move(std::size_t,pandoeditor::Point);
    bool beginDrag(std::size_t);
    bool endDrag(bool cancel);
    bool dragging() const {return bool(dragBefore_);}
    std::vector<std::pair<pandoeditor::Point,pandoeditor::Point>> activeSegments() const;
    void resetDraft();
    bool undo();bool redo();
    bool canUndo() const {return !undo_.empty();}bool canRedo() const {return !redo_.empty();}
    std::vector<pandoeditor::DraftGeometry> changedDrafts() const;
private:
    struct State {Drafts drafts;std::vector<Node> nodes;std::vector<std::string> movedOwners;};
    State snapshot() const;
    void restore(State);
    std::vector<Node> nodes_,originalNodes_;
    std::vector<Segment> segments_;
    Drafts originals_,drafts_;
    std::vector<std::string> movedOwners_;
    std::set<std::string> selected_;
    std::vector<pandoeditor::Geometry> lockedDescendants_;
    std::vector<State> undo_,redo_;
    std::optional<State> dragBefore_;
    int dragNode_=-1;
    std::string error_;
};
}
