#pragma once

#include "canonicalpacket.h"
#include <pandoeditor/document.h>
#include <string>
#include <vector>

struct BuiltinWorldRangeOwner {
    std::string sourceId;
    std::string ownerId;
    std::string geometryId;
    bool operator==(const BuiltinWorldRangeOwner& other) const {
        return sourceId==other.sourceId&&ownerId==other.ownerId&&geometryId==other.geometryId;
    }
};

struct BuiltinWorldMaterialization {
    pandoeditor::ProjectDocument document;
    std::vector<BuiltinWorldRangeOwner> ranges;
};

std::string builtinSubunitId(const std::string& sourceId);
BuiltinWorldMaterialization materializeBuiltinWorld(const CanonicalCountryStore& packet);
