#pragma once
#include <pandoeditor/document.h>
#include <pandoeditor/timeline-resolver.h>
#include <set>

namespace pandoeditor {
struct TimelineDocumentView {
    ProjectDocument document;
    ResolvedWorld world;
    std::set<std::string> inactiveIds;
};

// A read-only static projection for existing render and pick consumers. The
// source document and its dated records remain the only persistent state.
TimelineDocumentView timelineDocumentView(const ProjectDocument&, const std::string& month);
}
