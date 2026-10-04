#include "maprenderitem.h"
#include <pandoeditor/map/projectionengine.h>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace {
bool containsRef(const std::vector<pandoeditor::ObjectRef>& refs,
              const pandoeditor::ObjectRef& object) {
    return std::find(refs.begin(),refs.end(),object)!=refs.end();
}

QColor colorFor(const RenderStyle& style) {
    return QColor::fromRgb(style.color);
}

void applyComposition(QPainter* painter,const RenderStyle& style) {
    painter->setOpacity(std::clamp(double(style.alpha),0.,1.));
    painter->setCompositionMode(style.blendMode==BlendMode::Multiply?
        QPainter::CompositionMode_Multiply:QPainter::CompositionMode_SourceOver);
}

bool visibleCountry(const std::vector<bool>& mask,std::size_t index) {
    return mask.empty()||(index<mask.size()&&mask[index]);
}
}

MapRenderItem::MapRenderItem(QQuickItem* parent):QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setOpaquePainting(false);
    connect(this,&QQuickItem::visibleChanged,this,[this] {
        if(isVisible()) {if(sceneBridge_)syncSceneBridge();else update();}
    });
}

void MapRenderItem::setSmoothLines(bool value) {
    if(smoothLines_==value)return;
    smoothLines_=value;
    setAntialiasing(value);
    emit smoothLinesChanged();
    if(isVisible())update();
}

void MapRenderItem::setSceneBridge(QObject* value) {
    auto* next=qobject_cast<MapSceneBridge*>(value);
    if(sceneBridge_==next)return;
    if(sceneBridge_)disconnect(sceneBridge_,nullptr,this,nullptr);
    sceneBridge_=next;
    if(sceneBridge_) {
        connect(sceneBridge_,&MapSceneBridge::sceneChanged,this,[this]{syncSceneBridge();});
        connect(sceneBridge_,&MapSceneBridge::viewChanged,this,[this]{syncSceneBridge();});
        connect(sceneBridge_,&QObject::destroyed,this,[this]{
            sceneBridge_=nullptr;
            scene_.reset();
            emit sceneBridgeChanged();
            if(isVisible())update();
        });
    }
    syncSceneBridge();
}

void MapRenderItem::syncSceneBridge() {
    if(sceneBridge_)setSceneSnapshot(sceneBridge_->sceneSnapshot(),sceneBridge_->viewState());
    else scene_.reset();
    emit sceneBridgeChanged();
    if(isVisible())update();
}

void MapRenderItem::setSceneSnapshot(
    std::shared_ptr<const RenderScene> scene,const MapViewState& view) {
    if(!validMapViewState(view))throw std::invalid_argument("invalid typed scene view");
    scene_=std::move(scene);
    view_=view;
    if(isVisible())update();
}

void MapRenderItem::paint(QPainter* painter) {
    if(!isVisible()||!scene_||!validMapViewState(view_))return;
    ++paintCount_;
    QElapsedTimer paintClock;paintClock.start();
    struct Timing {std::atomic<qint64>& total;QElapsedTimer& clock;
        ~Timing(){total.fetch_add(clock.nsecsElapsed());}} timing{paintNanoseconds_,paintClock};
    painter->setRenderHint(QPainter::Antialiasing,smoothLines_);
    const auto copies=scene_->worldPlan.worldOffsets.empty()?
        visibleFlatWorldOffsets(view_):scene_->worldPlan.worldOffsets;

    const auto drawPolygon=[&](QPainter* target,const PolygonDrawPacket& draw,double offset,
                               const RenderStyle* overrideStyle=nullptr) {
        const auto& packet=draw.geometryPacket;
        if(!packet.positions||!packet.indices)return;
        const auto& indices=*(view_.mode==ProjectionMode::Globe?
            packet.globeIndices:packet.indices);
        if(indices.empty())return;
        const auto& style=overrideStyle?*overrideStyle:draw.style;
        target->save();
        applyComposition(target,style);
        target->setPen(Qt::NoPen);
        target->setBrush(colorFor(style));
        QPainterPath path;
        const auto& positions=*packet.positions;
        for(std::size_t i=0;i+2<indices.size();i+=3) {
            const auto a=projectPoint({positions[indices[i]*2],positions[indices[i]*2+1]},view_,offset);
            const auto b=projectPoint({positions[indices[i+1]*2],positions[indices[i+1]*2+1]},view_,offset);
            const auto c=projectPoint({positions[indices[i+2]*2],positions[indices[i+2]*2+1]},view_,offset);
            if(!a.finite||!b.finite||!c.finite||!a.visibleHemisphere||
               !b.visibleHemisphere||!c.visibleHemisphere)continue;
            path.moveTo(a.x,a.y);path.lineTo(b.x,b.y);path.lineTo(c.x,c.y);
            path.closeSubpath();
        }
        target->setOpacity(std::clamp(double(style.alpha*style.fillAlpha),0.,1.));
        target->drawPath(path);
        target->restore();
    };

    const auto drawStroke=[&](const StrokeDrawPacket& draw,double offset,
                              const RenderStyle& style) {
        const auto& packet=draw.geometryPacket;
        if(!packet.startsEnds||!packet.segmentCount)return;
        const auto& segments=*packet.startsEnds;
        const bool variable=packet.endpointWidths&&
            packet.endpointWidths->size()==packet.segmentCount*2;
        painter->save();
        applyComposition(painter,style);
        if(!variable) {
            QPen pen(colorFor(style),std::max(.1f,style.width),
                     Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin);
            if(style.dashOn>0&&style.dashOff>0)
                pen.setDashPattern({qreal(style.dashOn),qreal(style.dashOff)});
            painter->setPen(pen);painter->setBrush(Qt::NoBrush);
        } else {
            painter->setPen(Qt::NoPen);painter->setBrush(colorFor(style));
        }
        for(std::size_t i=0;i<packet.segmentCount;++i) {
            const auto* s=segments.data()+i*4;
            const auto a=projectPoint({s[0],s[1]},view_,offset);
            const auto b=projectPoint({s[2],s[3]},view_,offset);
            if(!a.finite||!b.finite||!a.visibleHemisphere||!b.visibleHemisphere)continue;
            if(!variable) {
                painter->drawLine(QPointF(a.x,a.y),QPointF(b.x,b.y));
                continue;
            }
            const double wa=std::max(.1,double(packet.endpointWidths->at(i*2))+style.width);
            const double wb=std::max(.1,double(packet.endpointWidths->at(i*2+1))+style.width);
            const double dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);
            if(length<1e-9)continue;
            const QPointF normal(-dy/length,dx/length);
            const QPointF pa(a.x,a.y),pb(b.x,b.y);
            const double ha=wa/2,hb=wb/2;
            QPainterPath shape;
            shape.addPolygon(QPolygonF{pa+normal*ha,pb+normal*hb,
                                       pb-normal*hb,pa-normal*ha});
            shape.closeSubpath();
            painter->drawPath(shape);
        }
        painter->restore();
    };

    const auto drawPoint=[&](const PointDrawPacket& draw,double offset,
                             const RenderStyle& style,double radius) {
        // Labels/flags remain in the shared QML safe-area layer in both backends.
        if(draw.object.domain=="label")return;
        const auto& packet=draw.geometryPacket;
        if(!packet.positions)return;
        painter->save();
        applyComposition(painter,style);
        painter->setPen(Qt::NoPen);painter->setBrush(colorFor(style));
        for(std::size_t i=0;i<packet.pointCount;++i) {
            const auto geographic=draw.manualPosition.value_or(
                pandoeditor::Point{packet.positions->at(i*2),packet.positions->at(i*2+1)});
            const auto projected=projectPoint(geographic,view_,offset);
            if(projected.finite&&projected.visibleHemisphere)
                painter->drawEllipse(QPointF(projected.x,projected.y),radius,radius);
        }
        painter->restore();
    };

    const auto drawWorld=[&](PrimitiveKind primitive,std::size_t country,double offset,
                             const RenderStyle* overrideStyle=nullptr) {
        if(!scene_->worldBase||!scene_->worldBase->mesh||
           country>=scene_->worldCountries.size()||!scene_->worldCountries[country].visible)return;
        const auto& mask=primitive==PrimitiveKind::WorldFill?
            scene_->worldPlan.fills.visible:scene_->worldPlan.strokes.visible;
        if(!overrideStyle&&!visibleCountry(mask,country))return;
        const auto& mesh=*scene_->worldBase->mesh;
        const auto& ranges=primitive==PrimitiveKind::WorldFill?
            mesh.countryTriangleRanges:mesh.countryBoundaryRanges;
        const auto& indices=primitive==PrimitiveKind::WorldFill?
            mesh.triangleIndices:mesh.lineIndices;
        if(country*2+1>=ranges.size())return;
        const auto start=ranges[country*2],count=ranges[country*2+1];
        const auto& base=scene_->worldCountries[country];
        const auto& style=overrideStyle?*overrideStyle:
            (primitive==PrimitiveKind::WorldFill?base.fill:base.boundary);
        painter->save();applyComposition(painter,style);
        if(primitive==PrimitiveKind::WorldFill) {
            painter->setPen(Qt::NoPen);painter->setBrush(colorFor(style));
            QPainterPath path;
            for(std::size_t i=0;i+2<count;i+=3) {
                ProjectedPoint p[3];bool visible=true;
                for(int j=0;j<3;++j) {
                    const auto vertex=indices[start+i+j];
                    p[j]=projectPoint({mesh.positionsMicrodegrees[vertex*2]/1e6,
                        mesh.positionsMicrodegrees[vertex*2+1]/1e6},view_,offset);
                    visible=visible&&p[j].finite&&p[j].visibleHemisphere;
                }
                if(!visible)continue;
                path.moveTo(p[0].x,p[0].y);path.lineTo(p[1].x,p[1].y);
                path.lineTo(p[2].x,p[2].y);path.closeSubpath();
            }
            painter->drawPath(path);
        } else {
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(colorFor(style),std::max(.1f,style.width),
                                 Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            for(std::size_t i=0;i+1<count;i+=2) {
                const auto ai=indices[start+i],bi=indices[start+i+1];
                const auto a=projectPoint({mesh.positionsMicrodegrees[ai*2]/1e6,
                    mesh.positionsMicrodegrees[ai*2+1]/1e6},view_,offset);
                const auto b=projectPoint({mesh.positionsMicrodegrees[bi*2]/1e6,
                    mesh.positionsMicrodegrees[bi*2+1]/1e6},view_,offset);
                if(a.finite&&b.finite&&a.visibleHemisphere&&b.visibleHemisphere)
                    painter->drawLine(QPointF(a.x,a.y),QPointF(b.x,b.y));
            }
        }
        painter->restore();
    };

    // The immutable preview world base predates canonical document packets and
    // is intentionally submitted outside drawSequence in both backends.
    if(scene_->worldBase&&scene_->worldBase->startupPreview())
        for(const double offset:copies)
            for(std::size_t i=0;i<scene_->worldCountries.size();++i) {
                drawWorld(PrimitiveKind::WorldFill,i,offset);
                drawWorld(PrimitiveKind::WorldStroke,i,offset);
            }

    // Base pass: exactly the engine-authored draw sequence used by the GPU backend.
    // Semi-transparent user-layer fills retain web/legacy group-opacity semantics:
    // composite the layer once instead of applying its alpha to every overlap.
    for(std::size_t commandIndex=0;commandIndex<scene_->drawSequence.size();) {
        const auto& command=scene_->drawSequence[commandIndex];
        if(command.primitive==PrimitiveKind::Polygon&&command.layerOpacity<.999f&&
           command.index<scene_->polygons.size()) {
            std::size_t end=commandIndex+1;
            while(end<scene_->drawSequence.size()) {
                const auto& next=scene_->drawSequence[end];
                if(next.primitive!=PrimitiveKind::Polygon||
                   next.layerOrder!=command.layerOrder||
                   next.order.pass!=command.order.pass||
                   std::abs(next.layerOpacity-command.layerOpacity)>1e-6f)break;
                ++end;
            }
            QImage buffer(std::max(1,int(std::ceil(width()))),
                          std::max(1,int(std::ceil(height()))),
                          QImage::Format_ARGB32_Premultiplied);
            buffer.fill(Qt::transparent);
            QPainter layerPainter(&buffer);
            layerPainter.setRenderHint(QPainter::Antialiasing,smoothLines_);
            for(std::size_t i=commandIndex;i<end;++i) {
                const auto& grouped=scene_->drawSequence[i];
                if(grouped.index>=scene_->polygons.size())continue;
                auto style=scene_->polygons[grouped.index].style;
                style.alpha=grouped.layerOpacity>0?
                    std::clamp(style.alpha/grouped.layerOpacity,0.f,1.f):0.f;
                for(const double offset:copies)
                    drawPolygon(&layerPainter,scene_->polygons[grouped.index],offset,&style);
            }
            layerPainter.end();
            painter->save();painter->setOpacity(command.layerOpacity);
            painter->drawImage(QPointF(0,0),buffer);painter->restore();
            commandIndex=end;continue;
        }
        for(const double offset:copies) {
            if(command.primitive==PrimitiveKind::Polygon&&command.index<scene_->polygons.size())
                drawPolygon(painter,scene_->polygons[command.index],offset);
            else if(command.primitive==PrimitiveKind::Stroke&&command.index<scene_->strokes.size())
                drawStroke(scene_->strokes[command.index],offset,
                           scene_->strokes[command.index].style);
            else if(command.primitive==PrimitiveKind::Point&&command.index<scene_->points.size()) {
                const auto& point=scene_->points[command.index];
                if(point.object.domain!="label")drawPoint(point,offset,point.style,3);
            } else if(command.primitive==PrimitiveKind::WorldFill||
                      command.primitive==PrimitiveKind::WorldStroke)
                drawWorld(command.primitive,command.index,offset);
        }
        ++commandIndex;
    }

    const auto strokeVertices=[&](const StrokeDrawPacket& draw,double offset,
                                  std::uint32_t color) {
        if(draw.object.domain=="hydroBuiltin"||!draw.geometryPacket.startsEnds)return;
        painter->save();painter->setOpacity(1);
        painter->setPen(QPen(QColor::fromRgb(color),1));
        painter->setBrush(QColor(Qt::white));
        const auto& values=*draw.geometryPacket.startsEnds;
        for(std::size_t i=0;i+1<values.size();i+=2) {
            const auto p=projectPoint({values[i],values[i+1]},view_,offset);
            if(p.finite&&p.visibleHemisphere)
                painter->drawEllipse(QPointF(p.x,p.y),4,4);
        }
        painter->restore();
    };

    const auto outline=[&](const pandoeditor::ObjectRef& ref,double offset,
                           std::uint32_t color,float width,bool vertices=false) {
        for(const auto& draw:scene_->strokes)if(draw.object==ref) {
            auto style=draw.style;style.color=color;style.alpha=1;
            style.width=width;style.dashOn=style.dashOff=0;
            drawStroke(draw,offset,style);
            if(vertices)strokeVertices(draw,offset,color);
        }
        for(const auto& draw:scene_->points)if(draw.object==ref) {
            RenderStyle style;style.color=color;style.alpha=1;
            drawPoint(draw,offset,style,6);
        }
    };

    for(const double offset:copies) {
        const auto worldOutline=[&](const pandoeditor::ObjectRef& ref,
                                    std::uint32_t color,float width) {
            if(ref.domain!="territorial"||!scene_->worldBase||
               !scene_->worldBase->mesh||scene_->worldBase->startupPreview())return;
            RenderStyle style;style.color=color;style.alpha=1;style.width=width;
            for(const auto index:worldRangeIndicesForOwner(*scene_->worldBase,ref.id))
                drawWorld(PrimitiveKind::WorldStroke,index,offset,&style);
        };
        for(const auto& candidate:scene_->interaction.candidates)
            if(!containsRef(scene_->interaction.selected,candidate)) {
                outline(candidate,offset,0x8abddd,1.5f);
                worldOutline(candidate,0x8abddd,1.5f);
            }
        if(scene_->interaction.hover&&
           !containsRef(scene_->interaction.selected,*scene_->interaction.hover)) {
            outline(*scene_->interaction.hover,offset,0x4083bc,2.f);
            worldOutline(*scene_->interaction.hover,0x4083bc,2.f);
        }
        for(const auto& selected:scene_->interaction.selected)
            if(!scene_->interaction.primary||selected!=*scene_->interaction.primary) {
                outline(selected,offset,0x163e64,2.f,true);
                worldOutline(selected,0x163e64,2.f);
            }
        if(scene_->interaction.primary) {
            outline(*scene_->interaction.primary,offset,0x163e64,3.f,true);
            worldOutline(*scene_->interaction.primary,0x163e64,3.f);
        }
        if(scene_->interaction.editTarget) {
            outline(*scene_->interaction.editTarget,offset,0xe89b1a,3.5f,true);
            worldOutline(*scene_->interaction.editTarget,0xe89b1a,3.5f);
        }
    }
}
