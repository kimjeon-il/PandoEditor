#pragma once
#include "hydromanifest.h"
#include <QByteArray>
#include <QDateTime>
#include <utility>

class HydroShardReader {
public:
    explicit HydroShardReader(HydroAssetSpec asset):asset_(std::move(asset)){}
    QByteArray readPack(quint32 offset,quint32 length,QString& error);
private:
    HydroAssetSpec asset_;
    QDateTime verifiedModified_;
    QString verifiedCanonical_;
    bool verified_=false;
};
