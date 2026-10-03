#pragma once
#include <pandoeditor/project.h>
#include <QByteArray>
#include <QString>

namespace pandoeditor {
// A full Qt project stored alongside GIS vector tables. The native v8 state
// is authoritative; embedded country flags live in the package asset table.
QByteArray exportProjectGeoPackage(const Project& project);
QByteArray readProjectGeoPackage(const QString& filePath);
}
