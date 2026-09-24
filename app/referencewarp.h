#pragma once

#include <QPointF>
#include <QString>
#include <QVector>

enum class ReferenceWarpMode { Auto, Similarity, Affine, Projective, ThinPlateSpline };

struct ReferenceControlPoint {
    QPointF source;
    QPointF destination;
};

struct ReferenceWarpResult {
    bool valid = false;
    ReferenceWarpMode resolvedMode = ReferenceWarpMode::Auto;
    QString error;
    double rmsError = 0.0;
    double maximumError = 0.0;
    QVector<double> xCoefficients;
    QVector<double> yCoefficients;
    QVector<ReferenceControlPoint> controlPoints;

    QPointF map(const QPointF &point) const;
};

ReferenceWarpResult solveReferenceWarp(ReferenceWarpMode mode,
                                       const QVector<ReferenceControlPoint> &points);

