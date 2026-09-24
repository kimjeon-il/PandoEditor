#pragma once
#include <QQuickPaintedItem>
#include <QVariantList>
#include <QVariantMap>

// One deterministic canvas owns fill, boundary and selected-outline passes.
// QML remains responsible for the viewport transform and edit overlays only.
class MapRenderItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList paths READ paths WRITE setPaths NOTIFY pathsChanged)
    Q_PROPERTY(QVariantMap visuals READ visuals WRITE setVisuals NOTIFY visualsChanged)
    Q_PROPERTY(QVariantList selectedPaths READ selectedPaths WRITE setSelectedPaths NOTIFY selectedPathsChanged)
    Q_PROPERTY(QString primaryId READ primaryId WRITE setPrimaryId NOTIFY primaryIdChanged)
    Q_PROPERTY(double originX READ originX WRITE setOriginX NOTIFY viewportChanged)
    Q_PROPERTY(double originY READ originY WRITE setOriginY NOTIFY viewportChanged)
    Q_PROPERTY(double mapScale READ mapScale WRITE setMapScale NOTIFY viewportChanged)
public:
    explicit MapRenderItem(QQuickItem* parent=nullptr);
    void paint(QPainter*) override;
    QVariantList paths() const{return paths_;} void setPaths(QVariantList);
    QVariantMap visuals() const{return visuals_;} void setVisuals(QVariantMap);
    QVariantList selectedPaths() const{return selected_;} void setSelectedPaths(QVariantList);
    QString primaryId() const{return primary_;} void setPrimaryId(QString);
    double originX() const{return originX_;} void setOriginX(double);
    double originY() const{return originY_;} void setOriginY(double);
    double mapScale() const{return scale_;} void setMapScale(double);
signals:
    void pathsChanged();void visualsChanged();void selectedPathsChanged();void primaryIdChanged();void viewportChanged();
private:
    QVariantList paths_,selected_;QVariantMap visuals_;QString primary_;
    double originX_=0,originY_=0,scale_=1;
};
