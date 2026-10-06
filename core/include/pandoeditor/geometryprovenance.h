#pragma once
#include <pandoeditor/geometry-types.h>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace pandoeditor {
struct ProjectDocument;
struct InlineGeometryAllocation {
    ObjectRef createdFor;
    std::string geometrySha256;
    bool promoted=false;
    bool operator==(const InlineGeometryAllocation& other) const {
        return std::tie(createdFor,geometrySha256,promoted)==
               std::tie(other.createdFor,other.geometrySha256,other.promoted);
    }
};
class GeometryProvenance {
public:
    std::set<GeometryRef> originalArchive;
    std::map<GeometryRef,InlineGeometryAllocation> inlineAllocations;
    std::map<std::string,std::string> opaqueBaseline;
    bool opaqueUncertain=false;
    bool operator==(const GeometryProvenance& other) const {
        return std::tie(originalArchive,inlineAllocations,opaqueBaseline,opaqueUncertain)==
               std::tie(other.originalArchive,other.inlineAllocations,other.opaqueBaseline,other.opaqueUncertain);
    }
private:
    struct OpaqueBaselineEvidence;
    std::shared_ptr<const OpaqueBaselineEvidence> cleanEvidence_;
    friend class GeometryProvenanceCodecAccess;
    friend void validateGeometryProvenance(const ProjectDocument&);
    friend void reconcileGeometryProvenance(const ProjectDocument&,ProjectDocument&);
};
// Trusted codec/import boundary only. Call after verifying canonical slot hashes
// against opaqueBaseline (or calculating them for a new web import). Never use
// this to authorize arbitrary native edits. Uncertain input receives no evidence.
class GeometryProvenanceCodecAccess {
public:
    static void sealVerifiedOpaqueBaseline(ProjectDocument&);
};
// One owner-keyed inventory shared by core transitions and codec hashing.
std::map<std::string,std::string> geometryProvenanceOpaqueSlots(const ProjectDocument&);
// Structural enumeration: deliberately does not call final document validation.
std::map<GeometryRef,std::vector<ObjectRef>> geometryProvenanceUsers(const ProjectDocument&);
void validateGeometryProvenance(const ProjectDocument&);
// Forward edit transition only; whole-document load uses validation instead.
void reconcileGeometryProvenance(const ProjectDocument& before,ProjectDocument& candidate);
} // namespace pandoeditor
