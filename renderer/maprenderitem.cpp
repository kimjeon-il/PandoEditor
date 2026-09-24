#include "maprenderitem.h"
#include "hydroruntimeprovider.h"
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
QObject* MapRenderItem::hydroSource() const{return hydroSource_;}
void MapRenderItem::setHydroSource(QObject* value) {
    auto* source=qobject_cast<HydroRuntimeProvider*>(value);
    if(source==hydroSource_)return;
    if(hydroSource_)disconnect(hydroSource_,nullptr,this,nullptr);
    hydroSource_=source;hydroFrame_=source?source->frame():nullptr;
    if(source){
        connect(source,&HydroRuntimeProvider::frameChanged,this,[this]{hydroFrame_=hydroSource_->frame();update();});
        connect(source,&QObject::destroyed,this,[this]{hydroSource_=nullptr;hydroFrame_.reset();update();});
    }
    emit hydroSourceChanged();update();
}
void MapRenderItem::setHydroProjection(QVariantMap value){if(hydroProjection_==value)return;hydroProjection_=std::move(value);emit hydroPresentationChanged();update();}
void MapRenderItem::setHydroStyle(QVariantMap value){if(hydroStyle_==value)return;hydroStyle_=std::move(value);emit hydroPresentationChanged();update();}
void MapRenderItem::setHiddenHydroIds(QVariantList value){if(hiddenHydroIds_==value)return;hiddenHydroIds_=std::move(value);emit hydroPresentationChanged();update();}
void MapRenderItem::setSelectedHydroId(QString value){if(selectedHydroId_==value)return;selectedHydroId_=std::move(value);emit hydroPresentationChanged();update();}
void MapRenderItem::setHydroFrame(std::shared_ptr<const HydroRuntimeFrame> value){hydroFrame_=std::move(value);update();}
void MapRenderItem::paint(QPainter* painter) {
    struct Row{QString id;QVariantMap visual;Parsed geometry;QString type;QVariantList points;};std::vector<Row> rows;rows.reserve(paths_.size());
    for(const auto& entry:paths_){const auto path=entry.toMap();const auto id=path.value("countryId").toString();const auto visual=visuals_.value(id).toMap();if(!visual.value("visible").toBool())continue;rows.push_back({id,visual,parsePath(path.value("path").toString(),originX_,originY_,scale_),path.value("geometryType").toString(),path.value("points").toList()});}
    painter->setRenderHint(QPainter::Antialiasing,true);
    auto order=[&](const Row& row,const char* key){return row.visual.value(key,20+row.visual.value("rank",0.).toInt()).toInt();};
    auto sorted=[&](const char* key){std::vector<const Row*> ordered;ordered.reserve(rows.size());
        for(const auto& row:rows)ordered.push_back(&row);
        std::stable_sort(ordered.begin(),ordered.end(),[&](const Row* a,const Row* b){
            const auto pa=order(*a,key),pb=order(*b,key);if(pa!=pb)return pa<pb;
            const auto ga=a->visual.value("drawGroup",0).toInt(),gb=b->visual.value("drawGroup",0).toInt();
            if(ga!=gb)return ga<gb;
            const auto oa=a->visual.value("drawObject",0.).toDouble(),ob=b->visual.value("drawObject",0.).toDouble();
            if(oa!=ob)return oa<ob;
            return a->visual.value("layerOrder",-1).toInt()<b->visual.value("layerOrder",-1).toInt();
        });return ordered;};
    auto paintRow=[&](const Row& row,int role){
        painter->save();painter->setOpacity(std::clamp(row.visual.value("opacity",1.).toDouble()*
            row.visual.value("layerOpacity",1.).toDouble(),0.,1.));
        painter->setCompositionMode(row.visual.value("blendMode").toString()=="multiply"?
            QPainter::CompositionMode_Multiply:QPainter::CompositionMode_SourceOver);
        if(role==0){
            if(row.type!="LineString"&&row.type!="MultiLineString"&&row.type!="Point"&&row.type!="MultiPoint"){
                painter->setPen(Qt::NoPen);painter->setBrush(color(row.visual.value("color"),Qt::lightGray));
                painter->drawPath(row.geometry.path);
            }
        }else if(role==1){
            painter->setPen(QPen(color(row.visual.value("color"),Qt::darkBlue),2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            if(row.type=="LineString"||row.type=="MultiLineString")painter->drawPath(row.geometry.path);
            painter->setBrush(color(row.visual.value("color"),Qt::darkBlue));
            for(const auto& value:row.points){const auto point=value.toMap();painter->drawEllipse(
                QPointF(originX_+point["x"].toDouble()*scale_,originY_+point["y"].toDouble()*scale_),3.,3.);}
        }
        painter->restore();
    };
    for(const auto* row:sorted("drawFillPass"))paintRow(*row,0);
    if(hydroFrame_){
        const double cosLatitude=hydroProjection_.value("cosLatitude",1.).toDouble();
        const double minX=hydroProjection_.value("minX",0.).toDouble();
        const double maxLatitude=hydroProjection_.value("maxLatitude",0.).toDouble();
        auto screen=[&](pandoeditor::HydroPoint point){return QPointF(
            originX_+(point.longitude*1e-6*cosLatitude-minX)*scale_,
            originY_+(maxLatitude-point.latitude*1e-6)*scale_);};
        std::set<QString> hidden;for(const auto& id:hiddenHydroIds_)hidden.insert(id.toString());
        auto visible=[&](std::uint32_t fid,std::uint32_t logical){
            if(hidden.count(QString::number(fid))||hidden.count(QString::number(logical)))return false;
            if(hydroSource_)if(const auto record=hydroSource_->recordByFid(fid))
                return !hidden.count(record->awId);
            return true;
        };
        auto selected=[&](std::uint32_t fid){if(!hydroSource_||selectedHydroId_.isEmpty())return false;
            const auto record=hydroSource_->recordByFid(fid);return record&&record->awId==selectedHydroId_;};
        if(hydroStyle_.value("lakesVisible",true).toBool()){
            painter->save();painter->setOpacity(std::clamp(hydroStyle_.value("lakeOpacity",1.).toDouble(),0.,1.));
            painter->setPen(Qt::NoPen);painter->setBrush(color(hydroStyle_.value("lakeColor"),QColor("#82bfd7")));
            for(const auto& lake:hydroFrame_->packet.lakes)if(visible(lake.fid,lake.logicalFid)){
                QPainterPath path;path.setFillRule(Qt::OddEvenFill);
                for(const auto& polygon:lake.polygons)for(const auto& ring:polygon){
                    if(ring.empty())continue;
                    path.moveTo(screen(ring.front()));
                    for(std::size_t i=1;i<ring.size();i++)path.lineTo(screen(ring[i]));
                    path.closeSubpath();
                }
                painter->drawPath(path);
                if(selected(lake.fid)){painter->save();painter->setOpacity(1);
                    painter->setPen(QPen(QColor("#163e64"),2));painter->setBrush(Qt::NoBrush);
                    painter->drawPath(path);painter->restore();}
            }
            painter->restore();
        }
        if(hydroStyle_.value("riversVisible",true).toBool()){
            painter->save();painter->setOpacity(std::clamp(hydroStyle_.value("riverOpacity",1.).toDouble(),0.,1.));
            painter->setPen(Qt::NoPen);painter->setBrush(color(hydroStyle_.value("riverColor"),QColor("#4b9cc6")));
            for(const auto& river:hydroFrame_->packet.rivers)if(visible(river.fid,river.logicalFid)){
                const auto a=screen(river.start),b=screen(river.end);
                const double dx=b.x()-a.x(),dy=b.y()-a.y(),length=std::hypot(dx,dy);
                if(length<1e-9)continue;
                const QPointF normal(-dy/length,dx/length);
                const double wa=std::clamp(river.startWidth,0.,100.)/2;
                const double wb=std::clamp(river.endWidth,0.,100.)/2;
                painter->drawPolygon(QPolygonF{a+normal*wa,b+normal*wb,b-normal*wb,a-normal*wa});
                painter->drawEllipse(a,wa,wa);painter->drawEllipse(b,wb,wb);
                if(selected(river.fid)){painter->save();painter->setOpacity(1);
                    painter->setPen(QPen(QColor("#163e64"),std::max(2.,std::max(wa,wb)*2+2)));
                    painter->setBrush(Qt::NoBrush);painter->drawLine(a,b);painter->restore();}
            }
            painter->restore();
        }
    }
    for(const auto* row:sorted("drawLinePass"))if(order(*row,"drawLinePass")<50)paintRow(*row,1);
    std::set<QString> boundarySegments;
    auto paintBoundary=[&](const Row& row){
        if(!row.visual.value("boundary",true).toBool())return;
        painter->save();painter->setOpacity(std::clamp(row.visual.value("opacity",1.).toDouble()*
            row.visual.value("layerOpacity",1.).toDouble(),0.,1.));
        const auto kind=row.visual.value("kind").toString();
        QPen pen(QColor("#61778a"),kind=="country"?1.2:kind=="subunit"?.9:.7,
            Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin);
        if(kind=="subunit")pen.setDashPattern({4,2});else if(kind=="region")pen.setDashPattern({1.5,2});
        painter->setPen(pen);painter->setBrush(Qt::NoBrush);
        for(const auto& segment:row.geometry.segments)
            if(boundarySegments.insert(segmentKey(segment)).second)painter->drawLine(segment);
        painter->restore();
    };
    for(const auto* row:sorted("drawBoundaryPass"))if(order(*row,"drawBoundaryPass")<=50)paintBoundary(*row);
    for(const auto* row:sorted("drawLinePass"))if(order(*row,"drawLinePass")>=50)paintRow(*row,1);
    for(const auto* row:sorted("drawBoundaryPass"))if(order(*row,"drawBoundaryPass")>50)paintBoundary(*row);
    std::set<QString> selectedSegments;
    for(const auto& entry:selected_)for(const auto& point:entry.toMap().value("points").toList()) {
        const auto p=point.toMap();painter->setPen(QPen(QColor("#163e64"),2));painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(QPointF(originX_+p["x"].toDouble()*scale_,originY_+p["y"].toDouble()*scale_),6.,6.);
    }
    for(const auto& entry:selected_){const auto value=entry.toMap();const auto id=value.value("countryId").toString();const auto parsed=parsePath(value.value("path").toString(),originX_,originY_,scale_);QPen pen(QColor("#163e64"),id==primary_?3:2,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin);painter->setPen(pen);for(const auto& segment:parsed.segments)if(selectedSegments.insert(segmentKey(segment)).second)painter->drawLine(segment);}
}
