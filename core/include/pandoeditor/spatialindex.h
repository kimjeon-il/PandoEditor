#pragma once
#include <pandoeditor/geobounds.h>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace pandoeditor {
// Conservative geographic candidates. Exact polygon/line/point hit testing remains authoritative.
class GeoSpatialIndex {
public:
    void rebuild(const ProjectDocument&,const DocumentIndex&);
    // Replaces cell membership for the supplied objects after an atomic document commit.
    // Visibility is deliberately outside this geographic index.
    void update(const ProjectDocument&,const DocumentIndex&,
                const std::vector<ObjectRef>& removed,const std::vector<ObjectRef>& changed);
    std::vector<ObjectRef> query(const std::vector<GeoBounds>& windows) const;
    // Transitional CPU/QPainter picking treats unsplit dateline segments as long flat edges.
    std::vector<ObjectRef> queryLegacyFlat(const std::vector<GeoBounds>& windows) const;
    std::uint64_t geometryRevision() const noexcept {return geometryRevision_;}
private:
    std::map<std::pair<int,int>,std::set<ObjectRef>> cells_;
    std::map<ObjectRef,std::vector<GeoBounds>> bounds_;
    std::map<ObjectRef,GeoBounds> legacyWrappedBounds_;
    std::map<ObjectRef,GeometryRef> references_;
    std::string dataset_,datasetVersion_,datasetSource_;
    std::uint64_t signature_=0,geometryRevision_=0;
};
}
