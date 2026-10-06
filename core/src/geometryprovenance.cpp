#include <pandoeditor/document.h>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace pandoeditor {
struct GeometryProvenance::OpaqueBaselineEvidence {
    const std::map<std::string,std::string> baseline;
    const std::map<std::string,std::string> slots;
    OpaqueBaselineEvidence(std::map<std::string,std::string> hashes,
                           std::map<std::string,std::string> values)
        : baseline(std::move(hashes)),slots(std::move(values)) {}
};
namespace {
void require(bool condition,const char* detail) {
    if(!condition)throw std::invalid_argument(std::string("INVALID_GEOMETRY_PROVENANCE: ")+detail);
}
bool digest(const std::string& value) {
    return value.size()==64 && std::all_of(value.begin(),value.end(),[](char c){
        return (c>='0'&&c<='9')||(c>='a'&&c<='f');
    });
}
std::string escaped(const std::string& id) {
    std::string result;
    for(const auto c:id) {if(c=='~')result+="~0";else if(c=='/')result+="~1";else result+=c;}
    return result;
}
bool nonemptyPayload(const std::string& value) {
    // JSON parsing/canonicalization belongs to the codec. Exact default/null
    // values cannot introduce a reference; other lexical forms are conservative.
    return !value.empty()&&value!="{}"&&value!="[]"&&value!="null";
}
const char* webDomain(const std::string& domain) {
    if(domain=="label")return "label";
    if(domain=="hydro")return "hydroEdits";
    if(domain=="generic")return "genericFeatures";
    if(domain=="distributionEntry")return "distributionEntry";
    return nullptr;
}
bool opaquePath(const std::string& path) {
    if(path=="/exchangeMetadata")return true;
    if(path.empty()||path.front()!='/')return false;
    std::vector<std::string> parts;
    std::size_t begin=1;
    while(begin<=path.size()) {
        const auto end=path.find('/',begin);
        parts.push_back(path.substr(begin,end==std::string::npos?end:end-begin));
        if(end==std::string::npos)break;
        begin=end+1;
    }
    if(parts.size()<3||parts[1].empty())return false;
    for(std::size_t i=0;i<parts[1].size();++i)if(parts[1][i]=='~') {
        if(++i==parts[1].size()||(parts[1][i]!='0'&&parts[1][i]!='1'))return false;
    }
    if(parts.size()==4)
        return (parts[0]=="labels"||parts[0]=="hydro"||parts[0]=="genericFeatures")&&
               parts[2]=="source"&&parts[3]=="details";
    if(parts.size()!=3)return false;
    if(parts[0]=="extensions")return parts[2]=="payload"||parts[2]=="envelopeExtras";
    return (parts[0]=="units"||parts[0]=="distributionLayers"||parts[0]=="distributionEntries")&&
           parts[2]=="metadata";
}
bool allocationGeometry(const Geometry& shape,const std::string& domain) {
    const bool polygon=shape.type=="Polygon"||shape.type=="MultiPolygon";
    if(domain=="label")return shape.type=="Point";
    if(domain=="distributionEntry")return polygon;
    if(domain=="hydro")return polygon||shape.type=="LineString"||shape.type=="MultiLineString";
    return domain=="generic";
}
bool hasUser(const std::map<GeometryRef,std::vector<ObjectRef>>& users,
             const GeometryRef& geometry,const ObjectRef& owner) {
    const auto found=users.find(geometry);
    return found!=users.end() && std::find(found->second.begin(),found->second.end(),owner)!=found->second.end();
}
}
std::map<std::string,std::string> geometryProvenanceOpaqueSlots(const ProjectDocument& d) {
    std::map<std::string,std::string> slots;
    const auto add=[&](const std::string& path,const std::string& value) {
        if(nonemptyPayload(value))require(slots.emplace(path,value).second,"duplicate opaque owner");
    };
    add("/exchangeMetadata",d.exchangeMetadata);
    for(const auto& row:d.units)add("/units/"+escaped(row.id)+"/metadata",row.metadata);
    for(const auto& row:d.labels)add("/labels/"+escaped(row.id)+"/source/details",row.source.details);
    for(const auto& row:d.hydro)add("/hydro/"+escaped(row.id)+"/source/details",row.source.details);
    for(const auto& row:d.genericFeatures)add("/genericFeatures/"+escaped(row.id)+"/source/details",row.source.details);
    for(const auto& row:d.distributionLayers)add("/distributionLayers/"+escaped(row.id)+"/metadata",row.metadata);
    for(const auto& row:d.distributionEntries)add("/distributionEntries/"+escaped(row.id)+"/metadata",row.metadata);
    for(const auto& row:d.extensions) {
        add("/extensions/"+escaped(row.id)+"/payload",row.payload);
        add("/extensions/"+escaped(row.id)+"/envelopeExtras",row.envelopeExtras);
    }
    return slots;
}
std::map<GeometryRef,std::vector<ObjectRef>> geometryProvenanceUsers(const ProjectDocument& d) {
    std::map<GeometryRef,std::vector<ObjectRef>> result;
    for(const auto& row:d.labels)result[row.geometry].push_back({"label",row.id});
    for(const auto& row:d.hydro)result[row.geometry].push_back({"hydro",row.id});
    for(const auto& row:d.genericFeatures)result[row.geometry].push_back({"generic",row.id});
    for(const auto& row:d.distributionEntries)if(row.geometry)result[*row.geometry].push_back({"distributionEntry",row.id});
    // Enumerate every historical record, without using static-view accessors or
    // ownership-aware validation of the not-yet-reconciled candidate.
    for(const auto& row:d.timelineRecords.geometryBindings)result[row.geometryRef].push_back(territorialRef(row.entityId));
    return result;
}
void GeometryProvenanceCodecAccess::sealVerifiedOpaqueBaseline(ProjectDocument& document) {
    auto& provenance=document.geometryProvenance;
    provenance.cleanEvidence_.reset();
    if(provenance.opaqueUncertain)return;
    provenance.cleanEvidence_=std::make_shared<const GeometryProvenance::OpaqueBaselineEvidence>(
        provenance.opaqueBaseline,geometryProvenanceOpaqueSlots(document));
}
void validateGeometryProvenance(const ProjectDocument& document) {
    const auto& provenance=document.geometryProvenance;
    for(const auto& ref:provenance.originalArchive)
        require(bool(document.geometries.get(ref)),"missing original geometry");
    const auto users=geometryProvenanceUsers(document);
    bool unpromoted=false;
    for(const auto& [ref,allocation]:provenance.inlineAllocations) {
        require(!provenance.originalArchive.count(ref),"overlapping original allocation");
        const auto shape=document.geometries.get(ref);
        require(bool(shape),"missing allocated geometry");
        const auto domain=webDomain(allocation.createdFor.domain);
        require(domain&&!allocation.createdFor.id.empty(),"invalid allocation creator");
        require(ref.version==1&&ref.id==std::string("web-")+domain+":"+allocation.createdFor.id,
                "invalid allocation reference");
        require(digest(allocation.geometrySha256),"invalid geometry digest");
        require(allocationGeometry(*shape,allocation.createdFor.domain),"invalid allocation geometry");
        if(allocation.promoted)continue;
        unpromoted=true;
        const auto found=users.find(ref);
        if(found!=users.end())for(const auto& owner:found->second)
            require(owner==allocation.createdFor,"unpromoted geometry user");
    }
    for(const auto& [path,hash]:provenance.opaqueBaseline) {
        require(opaquePath(path),"invalid opaque path");
        require(digest(hash),"invalid opaque digest");
    }
    if(!unpromoted||provenance.opaqueUncertain)return;
    require(bool(provenance.cleanEvidence_),"missing clean evidence");
    const auto& evidence=*provenance.cleanEvidence_;
    require(provenance.opaqueBaseline==evidence.baseline,"opaque baseline changed");
    for(const auto& [path,value]:geometryProvenanceOpaqueSlots(document)) {
        const auto previous=evidence.slots.find(path);
        require(previous!=evidence.slots.end()&&previous->second==value,"opaque slot changed");
        require(evidence.baseline.count(path),"missing opaque baseline");
    }
}
void reconcileGeometryProvenance(const ProjectDocument& before,ProjectDocument& candidate) {
    // Native edit commands cannot create import provenance, change its hashes,
    // erase creation history, demote, or reset uncertainty. Only the trusted
    // import/load boundary initializes that ledger; Undo restores whole states.
    candidate.geometryProvenance=before.geometryProvenance;
    auto& provenance=candidate.geometryProvenance;
    const auto oldUsers=geometryProvenanceUsers(before),newUsers=geometryProvenanceUsers(candidate);
    for(auto& [ref,allocation]:provenance.inlineAllocations) {
        if(allocation.promoted)continue;
        const auto found=newUsers.find(ref);
        if(found==newUsers.end())continue;
        const bool adopted=std::any_of(found->second.begin(),found->second.end(),[&](const auto& owner){
            return owner!=allocation.createdFor;
        });
        const bool readopted=hasUser(newUsers,ref,allocation.createdFor)&&!hasUser(oldUsers,ref,allocation.createdFor);
        allocation.promoted=adopted||readopted;
    }
    if(!provenance.opaqueUncertain) {
        const auto oldSlots=geometryProvenanceOpaqueSlots(before);
        for(const auto& [path,value]:geometryProvenanceOpaqueSlots(candidate)) {
            const auto previous=oldSlots.find(path);
            if(previous==oldSlots.end()||previous->second!=value) {
                provenance.opaqueUncertain=true;
                break;
            }
        }
    }
    if(provenance.opaqueUncertain)provenance.cleanEvidence_.reset();
}
} // namespace pandoeditor
