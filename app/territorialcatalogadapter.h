#pragma once
#include "territoriallibrarycatalog.h"
#include <pandoeditor/historicalinstantiation.h>
#include <pandoeditor/project.h>
#include <QVariantMap>

namespace pandoeditor {
// A value adapter over the one verified v2 catalog. Called on the geometry
// worker with an immutable project snapshot, never with an EditorController.
Geometry territorialCatalogGeometry(const QJsonObject&);
std::vector<HistoricalAddition> territorialCatalogSelections(
    const TerritorialLibraryCatalog&,const ProjectSnapshot&,const QStringList& roots,
    const QString& referenceDate,const QString& childDepth,const QVariantMap& ownership,
    const QString& selectedId={},const QString& selectedVersionId={});
}
