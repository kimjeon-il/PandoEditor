#pragma once
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>
#include <QtGlobal>

struct HydroMetadataRecord {
    quint32 fid=0,logicalFid=0;
    QString awId,name,layerId,category,systemId,role,source,sourceId;
    QVector<double> bounds;
};
using HydroMetadata = QHash<quint32,HydroMetadataRecord>;

bool parseHydroCoreMetadata(const QByteArray& bytes,int expectedCount,
                            HydroMetadata& output,QString& error);
bool mergeHydroDetailMetadata(const QByteArray& bytes,HydroMetadata& output,QString& error);
