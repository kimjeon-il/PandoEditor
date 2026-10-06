#pragma once
#include <pandoeditor/project.h>
#include <QByteArray>

namespace projectcodec {
inline constexpr int ProjectVersion = 10;
inline constexpr int TerritorialIdentityVersion = 6;
// The upload and persistence boundaries share the same non-rasterizing SVG
// and bounded raster checks; throws before either publishes invalid image data.
void validateFlagDataUrl(const std::string& value);
pandoeditor::ProjectDocument decode(const QByteArray& data);
pandoeditor::ProjectDocument decodeWeb(const QByteArray& data);
QByteArray encode(const pandoeditor::Project& project);
QByteArray encode(const pandoeditor::ProjectSnapshot& snapshot);
QByteArray encodeWeb(const pandoeditor::ProjectSnapshot& snapshot);
}
