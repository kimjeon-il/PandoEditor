#include <pandoeditor/project.h>
#include "documentstate.h"

namespace pandoeditor {
const ProjectDocument& ProjectSnapshot::document() const noexcept { return state_->document; }
const DocumentIndex& ProjectSnapshot::index() const noexcept { return state_->index; }
const std::vector<Layer>& ProjectSnapshot::layers() const noexcept { return document().presentation.userLayers; }
const CountryView* ProjectSnapshot::country(const std::string& id) const {
    auto it=state_->countryIndex.find(id); return it==state_->countryIndex.end()?nullptr:&state_->countries[it->second];
}
const Layer* ProjectSnapshot::layer(const std::string& id) const {
    auto it=index().layers.find(id); return it==index().layers.end()?nullptr:&layers()[it->second];
}
bool ProjectSnapshot::matches(const Project& p) const noexcept {
    return instanceId_==p.instanceId() && revision_==p.revision() && document().documentId==p.document().documentId;
}
}
