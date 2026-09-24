#pragma once
#include "hydromanifest.h"
#include <QByteArray>

bool verifyHydroAsset(const HydroAssetSpec& asset,QString& error);
QByteArray readHydroAsset(const HydroAssetSpec& asset,bool gzip,QString& error,
                          qsizetype maxDecoded=64*1024*1024);
