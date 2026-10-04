#pragma once
#include "../app/mapscenebridge.h"
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QUrl>
#include <QVariantMap>
#include "../app/terrainimageprovider.h"

class GeographicImageItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* sceneBridge READ sceneBridge WRITE setSceneBridge NOTIFY changed)
    Q_PROPERTY(QObject* terrainBridge READ terrainBridge WRITE setTerrainBridge NOTIFY changed)
    Q_PROPERTY(QVariantMap terrainTile READ terrainTile WRITE setTerrainTile NOTIFY changed)
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(double west READ west WRITE setWest NOTIFY changed)
    Q_PROPERTY(double south READ south WRITE setSouth NOTIFY changed)
    Q_PROPERTY(double east READ east WRITE setEast NOTIFY changed)
    Q_PROPERTY(double north READ north WRITE setNorth NOTIFY changed)
    Q_PROPERTY(QString colorMode READ colorMode WRITE setColorMode NOTIFY changed)
    Q_PROPERTY(bool smooth READ smooth WRITE setSmooth NOTIFY changed)
public:
    explicit GeographicImageItem(QQuickItem* parent=nullptr);
    QObject* sceneBridge() const{return bridge_;} void setSceneBridge(QObject*);
    QObject* terrainBridge() const {return terrainBridge_;}
    void setTerrainBridge(QObject*);
    QVariantMap terrainTile() const {return terrainTile_;}
    void setTerrainTile(QVariantMap);
    QUrl source() const{return source_;} void setSource(QUrl);
    double west() const{return west_;} void setWest(double);
    double south() const{return south_;} void setSouth(double);
    double east() const{return east_;} void setEast(double);
    double north() const{return north_;} void setNorth(double);
    QString colorMode() const{return colorMode_;} void setColorMode(QString);
    bool smooth() const{return smooth_;} void setSmooth(bool);
signals:
    void changed();
protected:
    QSGNode* updatePaintNode(QSGNode*,UpdatePaintNodeData*) override;
private:
    void refresh();
    QPointer<MapSceneBridge> bridge_;
    QPointer<TerrainImageBridge> terrainBridge_;
    QVariantMap terrainTile_;
    QUrl source_;
    QImage image_;
    double west_=-180,south_=-90,east_=180,north_=90;
    QString colorMode_=QStringLiteral("color");
    bool smooth_=true;
};
