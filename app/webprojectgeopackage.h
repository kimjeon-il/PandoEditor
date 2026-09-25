#pragma once
#include "gisgeopackage.h"
#include "losslessjson.h"
#include <QByteArray>
#include <QString>
#include <map>
#include <string>

namespace pandoeditor {
struct WebProjectAsset { QString mime; QByteArray bytes; };
// Convert a validated web package into a complete native project, before the
// controller performs its atomic replacement. No live project is accessed.
QByteArray convertWebProjectGeoPackage(const GisGeoPackage& vectors,
    losslessjson::Value state,const std::map<std::string,WebProjectAsset>& assets);
}
