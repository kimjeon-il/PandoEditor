#pragma once

#include "referencewarp.h"
#include <QImage>
#include <QQuickPaintedItem>
#include <QUrl>

class ReferenceImageItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(QString blendMode READ blendMode WRITE setBlendMode NOTIFY changed)
    Q_PROPERTY(QString warpMode READ warpMode WRITE setWarpMode NOTIFY changed)
    Q_PROPERTY(QVariantList controlPoints READ controlPoints WRITE setControlPoints NOTIFY changed)
    Q_PROPERTY(QRectF imageRect READ imageRect WRITE setImageRect NOTIFY changed)
    Q_PROPERTY(bool flipX READ flipX WRITE setFlipX NOTIFY changed)
    Q_PROPERTY(bool flipY READ flipY WRITE setFlipY NOTIFY changed)
public:
    explicit ReferenceImageItem(QQuickItem *parent=nullptr);
    void paint(QPainter *painter) override;
    QUrl source()const{return source_;} QString blendMode()const{return blend_;} QString warpMode()const{return warp_;}
    QVariantList controlPoints()const{return points_;} QRectF imageRect()const{return rect_;} bool flipX()const{return flipX_;}bool flipY()const{return flipY_;}
    void setSource(const QUrl&);void setBlendMode(QString);void setWarpMode(QString);void setControlPoints(QVariantList);void setImageRect(QRectF);void setFlipX(bool);void setFlipY(bool);
signals:void changed();
private:
    void refresh();ReferenceWarpResult resolvedWarp()const;
    QUrl source_;QImage image_;QString blend_="normal",warp_="auto";QVariantList points_;QRectF rect_;bool flipX_=false,flipY_=false;
};
