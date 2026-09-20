#include "maprenderitem.h"
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <set>

namespace {
struct Parsed { QPainterPath path;std::vector<QLineF> segments; };
Parsed parsePath(const QString& source,double ox,double oy,double scale) {
    static const QRegularExpression token(QStringLiteral("([MLZ])|(-?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?)"));
    auto match=token.globalMatch(source);QString command;QPointF first,last;bool have=false;Parsed parsed;
    while(match.hasNext()) {const auto value=match.next().captured();if(value=="M"||value=="L"||value=="Z") {command=value;if(value=="Z"&&have){parsed.path.closeSubpath();parsed.segments.push_back({last,first});last=first;}continue;}
        bool ok=false;const auto x=value.toDouble(&ok);if(!ok||!match.hasNext())break;const auto y=match.next().captured().toDouble(&ok);if(!ok)break;const QPointF point(ox+x*scale,oy+y*scale);
        if(command=="M"||!have){parsed.path.moveTo(point);first=last=point;have=true;command="L";}else {parsed.path.lineTo(point);parsed.segments.push_back({last,point});last=point;}
    }return parsed;
}
QString segmentKey(QLineF line) {auto a=line.p1(),b=line.p2();if(a.x()>b.x()||(a.x()==b.x()&&a.y()>b.y()))std::swap(a,b);return QString::number(a.x(),'f',5)+","+QString::number(a.y(),'f',5)+":"+QString::number(b.x(),'f',5)+","+QString::number(b.y(),'f',5);}
QColor color(const QVariant& value,const QColor& fallback={}){const QColor parsed(value.toString());return parsed.isValid()?parsed:fallback;}
}
MapRenderItem::MapRenderItem(QQuickItem* parent):QQuickPaintedItem(parent){setAntialiasing(true);setOpaquePainting(false);connect(this,&MapRenderItem::viewportChanged,this,[this]{update();});}
void MapRenderItem::setPaths(QVariantList value){if(paths_==value)return;paths_=std::move(value);emit pathsChanged();update();}
void MapRenderItem::setVisuals(QVariantMap value){if(visuals_==value)return;visuals_=std::move(value);emit visualsChanged();update();}
void MapRenderItem::setSelectedPaths(QVariantList value){if(selected_==value)return;selected_=std::move(value);emit selectedPathsChanged();update();}
void MapRenderItem::setPrimaryId(QString value){if(primary_==value)return;primary_=std::move(value);emit primaryIdChanged();update();}
void MapRenderItem::setOriginX(double v){if(originX_==v)return;originX_=v;emit viewportChanged();}
void MapRenderItem::setOriginY(double v){if(originY_==v)return;originY_=v;emit viewportChanged();}
void MapRenderItem::setMapScale(double v){if(scale_==v||!std::isfinite(v)||v<=0)return;scale_=v;emit viewportChanged();}
void MapRenderItem::paint(QPainter* painter) {
    struct Row{QString id;QVariantMap visual;Parsed geometry;QString type;QVariantList points;};std::vector<Row> rows;rows.reserve(paths_.size());
    for(const auto& entry:paths_){const auto path=entry.toMap();const auto id=path.value("countryId").toString();const auto visual=visuals_.value(id).toMap();if(!visual.value("visible").toBool())continue;rows.push_back({id,visual,parsePath(path.value("path").toString(),originX_,originY_,scale_),path.value("geometryType").toString(),path.value("points").toList()});}
    std::stable_sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){const auto al=a.visual.value("layerOrder",-1).toInt(),bl=b.visual.value("layerOrder",-1).toInt();return al==bl?a.visual.value("rank").toDouble()<b.visual.value("rank").toDouble():al<bl;});
    painter->setRenderHint(QPainter::Antialiasing,true);
    for(std::size_t first=0;first<rows.size();) {std::size_t last=first+1;const auto layer=rows[first].visual.value("layerId").toString();while(last<rows.size()&&rows[last].visual.value("layerId").toString()==layer)++last;
        QImage buffer(std::max(1,int(std::ceil(width()))),std::max(1,int(std::ceil(height()))),QImage::Format_ARGB32_Premultiplied);buffer.fill(Qt::transparent);QPainter layerPainter(&buffer);layerPainter.setRenderHint(QPainter::Antialiasing,true);
        for(auto i=first;i<last;++i){const auto& row=rows[i];if(row.type=="LineString"||row.type=="MultiLineString"||row.type=="Point"||row.type=="MultiPoint")continue;layerPainter.save();layerPainter.setOpacity(std::clamp(row.visual.value("opacity",1.).toDouble(),0.,1.));layerPainter.setCompositionMode(row.visual.value("blendMode").toString()=="multiply"?QPainter::CompositionMode_Multiply:QPainter::CompositionMode_SourceOver);layerPainter.setPen(Qt::NoPen);layerPainter.setBrush(color(row.visual.value("color"),Qt::lightGray));layerPainter.drawPath(row.geometry.path);layerPainter.restore();}
        for(auto i=first;i<last;++i) {
            const auto& row=rows[i]; layerPainter.save(); layerPainter.setOpacity(row.visual.value("opacity",1.).toDouble());
            layerPainter.setPen(QPen(color(row.visual.value("color"),Qt::darkBlue),2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            layerPainter.setBrush(Qt::NoBrush);
            if(row.type=="LineString" || row.type=="MultiLineString") layerPainter.drawPath(row.geometry.path);
            layerPainter.setBrush(color(row.visual.value("color"),Qt::darkBlue));
            for(const auto& value:row.points) {const auto point=value.toMap();layerPainter.drawEllipse(QPointF(originX_+point["x"].toDouble()*scale_,originY_+point["y"].toDouble()*scale_),3.,3.);}
            layerPainter.restore();
        }
        std::set<QString> boundarySegments;for(auto i=first;i<last;++i){const auto& row=rows[i];if(!row.visual.value("boundary",true).toBool())continue;const auto kind=row.visual.value("kind").toString();QPen pen(QColor("#61778a"),kind=="country"?1.2:kind=="subunit"?.9:.7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin);if(kind=="subunit")pen.setDashPattern({4,2});else if(kind=="region")pen.setDashPattern({1.5,2});layerPainter.setPen(pen);for(const auto& segment:row.geometry.segments)if(boundarySegments.insert(segmentKey(segment)).second)layerPainter.drawLine(segment);}layerPainter.end();
        painter->save();painter->setOpacity(std::clamp(rows[first].visual.value("layerOpacity",1.).toDouble(),0.,1.));painter->drawImage(QPointF(0,0),buffer);painter->restore();first=last;
    }
    std::set<QString> selectedSegments;
    for(const auto& entry:selected_)for(const auto& point:entry.toMap().value("points").toList()) {
        const auto p=point.toMap();painter->setPen(QPen(QColor("#163e64"),2));painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(QPointF(originX_+p["x"].toDouble()*scale_,originY_+p["y"].toDouble()*scale_),6.,6.);
    }
    for(const auto& entry:selected_){const auto value=entry.toMap();const auto id=value.value("countryId").toString();const auto parsed=parsePath(value.value("path").toString(),originX_,originY_,scale_);QPen pen(QColor("#163e64"),id==primary_?3:2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin);painter->setPen(pen);for(const auto& segment:parsed.segments)if(selectedSegments.insert(segmentKey(segment)).second)painter->drawLine(segment);}
}
