#pragma once
#include <pandoeditor/document.h>
#include <QVariantMap>
#include <QString>

struct HydroDataInspection { bool ready=false; QString root,error,dataset,version; };
HydroDataInspection inspectHydroData(const QString& path);
