#pragma once
#include <QImage>
#include <QPoint>
#include <QVector>

QVector<QPoint> referenceLiveWire(const QImage &image,QPoint start,QPoint end,int maximumVisited=250000);
QVector<QPointF> refineReferenceLine(const QImage &image,const QVector<QPointF> &line,int radius=3);

