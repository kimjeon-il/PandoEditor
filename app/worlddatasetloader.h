#pragma once
#include "worlddataset.h"
#include "../renderer/canonicalpacket.h"
#include "../renderer/countrymesh.h"
#include "../renderer/mapprojection.h"
#include "../renderer/renderscene.h"
#include <pandoeditor/document.h>
#include <memory>

struct WorldPreviewResult {
    std::shared_ptr<const WorldBaseFrame> frame;
    std::shared_ptr<const MapProjection> projection;
};
struct WorldCanonicalResult {
    std::shared_ptr<const pandoeditor::ProjectDocument> document;
    std::shared_ptr<const MapProjection> projection;
    std::vector<WorldBaseRange> ranges;
    QString hydroAvailability;
};

class WorldDatasetLoader final {
public:
    static WorldPreviewResult preview(const QString& root);
    static WorldCanonicalResult canonical(const QString& root);
    static std::shared_ptr<const CountryBaseMesh> canonicalMesh(const QString& root);
};
