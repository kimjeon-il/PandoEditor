#include "maprenderitem.h"
#include "interactionrenderstyle.h"
#include "stroketopology.h"
#include <pandoeditor/map/projectionengine.h>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QElapsedTimer>
#include <QQuickWindow>
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
    qRegisterMetaType<std::shared_ptr<const MapFrame>>();
    connect(this,&QQuickItem::windowChanged,this,&MapRenderItem::attachPresentationWindow);
    if(window())attachPresentationWindow(window());
    setAntialiasing(true);
    setOpaquePainting(false);
    connect(this,&QQuickItem::visibleChanged,this,[this] {
        if(isVisible()) {if(sceneBridge_)syncSceneBridge();else update();}
    });
}

MapRenderItem::~MapRenderItem() {
    disconnect(this,nullptr,this,nullptr);
    if(presentationWindow_)disconnect(presentationWindow_,nullptr,this,nullptr);
    if(sceneBridge_)disconnect(sceneBridge_,nullptr,this,nullptr);
    ++presentationWindowGeneration_;++presentationContextGeneration_;++presentationBridgeGeneration_;
}

void MapRenderItem::attachPresentationWindow(QQuickWindow* next) {
    if(presentationWindow_)disconnect(presentationWindow_,nullptr,this,nullptr);
    presentationWindow_=next;
    const auto generation=++presentationWindowGeneration_;++presentationContextGeneration_;
    {std::lock_guard lock(presentationMutex_);paintedFrame_.reset();presentationFrame_.reset();}
    if(!next)return;
    const auto invalidate=[this,generation] {
        if(presentationWindowGeneration_.load()!=generation)return;
        ++presentationContextGeneration_;
        std::lock_guard lock(presentationMutex_);paintedFrame_.reset();presentationFrame_.reset();
    };
    connect(next,&QQuickWindow::sceneGraphInitialized,this,invalidate,Qt::DirectConnection);
    connect(next,&QQuickWindow::sceneGraphInvalidated,this,invalidate,Qt::DirectConnection);
    connect(next,&QQuickWindow::beforeFrameBegin,this,[this,generation] {
        if(presentationWindowGeneration_.load()!=generation)return;
        std::lock_guard lock(presentationMutex_);++presentationSequence_;
        presentationFrame_.reset();presentationEnded_=presentationSwapped_=false;
    },Qt::DirectConnection);
    connect(next,&QQuickWindow::afterSynchronizing,this,[this,generation] {
        if(presentationWindowGeneration_.load()!=generation)return;
        std::lock_guard lock(presentationMutex_);
        presentationFrame_=paintedFrame_;presentationStrokeInventory_=paintedStrokeInventory_;
        capturedContextGeneration_=presentationContextGeneration_.load();
        capturedBridgeGeneration_=presentationBridgeGeneration_.load();
    },Qt::DirectConnection);
    connect(next,&QQuickWindow::afterFrameEnd,this,[this,generation] {
        if(presentationWindowGeneration_.load()!=generation)return;
        {std::lock_guard lock(presentationMutex_);presentationEnded_=true;}publishPresentation();
    },Qt::DirectConnection);
    connect(next,&QQuickWindow::frameSwapped,this,[this,generation] {
        if(presentationWindowGeneration_.load()!=generation)return;
        {std::lock_guard lock(presentationMutex_);presentationSwapped_=true;}publishPresentation();
    },Qt::DirectConnection);
}

void MapRenderItem::publishPresentation() {
    std::shared_ptr<const MapFrame> frame;QVariantList inventory;
    const auto windowGeneration=presentationWindowGeneration_.load();qulonglong contextGeneration=0,bridgeGeneration=0;
    {std::lock_guard lock(presentationMutex_);
        if(!presentationEnded_||!presentationSwapped_||!presentationFrame_||presentationSequence_==publishedPresentationSequence_)return;
        frame=presentationFrame_;inventory=presentationStrokeInventory_;publishedPresentationSequence_=presentationSequence_;
        contextGeneration=capturedContextGeneration_;bridgeGeneration=capturedBridgeGeneration_;}
    QMetaObject::invokeMethod(this,[this,frame,inventory,windowGeneration,contextGeneration,bridgeGeneration] {
        if(!isVisible()||presentationWindowGeneration_.load()!=windowGeneration||
            presentationContextGeneration_.load()!=contextGeneration||presentationBridgeGeneration_.load()!=bridgeGeneration)return;
        // Same actual Qt frame completed submission and swap. No GPU fence or
        // compositor receipt is implied by this CPU fallback observation.
        emit framePresented(frame,inventory);
    },Qt::QueuedConnection);
}

QSGNode* MapRenderItem::updatePaintNode(QSGNode* previous,UpdatePaintNodeData* data) {
    paintingForSceneGraph_=true;
    auto* result=QQuickPaintedItem::updatePaintNode(previous,data);
    paintingForSceneGraph_=false;return result;
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
    ++presentationBridgeGeneration_;
    {std::lock_guard lock(presentationMutex_);paintedFrame_.reset();presentationFrame_.reset();}
    if(sceneBridge_)disconnect(sceneBridge_,nullptr,this,nullptr);
    sceneBridge_=next;
    if(sceneBridge_) {
        connect(sceneBridge_,&MapSceneBridge::sceneChanged,this,[this]{syncSceneBridge();});
        connect(sceneBridge_,&MapSceneBridge::viewChanged,this,[this]{syncSceneBridge();});
        connect(sceneBridge_,&QObject::destroyed,this,[this]{
            ++presentationBridgeGeneration_;
            {std::lock_guard lock(presentationMutex_);paintedFrame_.reset();presentationFrame_.reset();}
            sceneBridge_=nullptr;
            scene_.reset();frame_.reset();
            emit sceneBridgeChanged();
            if(isVisible())update();
        });
    }
    syncSceneBridge();
}

void MapRenderItem::syncSceneBridge() {
    if(sceneBridge_) {
        frame_=sceneBridge_->frameSnapshot();scene_=frame_->scene;view_=frame_->view;
    }
    else {scene_.reset();frame_.reset();}
    emit sceneBridgeChanged();
    if(isVisible())update();
}

void MapRenderItem::setSceneSnapshot(
    std::shared_ptr<const RenderScene> scene,const MapViewState& view) {
    if(!validMapViewState(view))throw std::invalid_argument("invalid typed scene view");
    frame_=FramePipeline::compose(std::move(scene),view,frame_);
    scene_=frame_->scene;
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
    const auto& worldPlan=*frame_->worldPlan;
    const auto copies=worldPlan.worldOffsets.empty()?
        visibleFlatWorldOffsets(view_):worldPlan.worldOffsets;
    QVariantList actualStrokes;
    const auto recordStroke=[&](const pandoeditor::ObjectRef& ref,const pandoeditor::GeometryRef& geometry,float alpha) {
        if(ref.id.empty()||geometry.id.empty()||alpha<=0)return;
        const QVariantMap identity{{"domain",QString::fromStdString(ref.domain)},{"id",QString::fromStdString(ref.id)},
            {"geometryId",QString::fromStdString(geometry.id)},{"geometryVersion",qulonglong(geometry.version)}};
        if(!actualStrokes.contains(identity))actualStrokes.push_back(identity);
    };

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
        if((style.width<=0&&!variable)||style.alpha<=0)return;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing,smoothLines_&&style.antiAlias);
        applyComposition(painter,style);
        const bool dashed=style.dashOn>0&&style.dashOff>0;
        QPen pen(colorFor(style),style.width,Qt::SolidLine,
                 dashed||style.cap==mapstyle::Cap::Butt?Qt::FlatCap:Qt::RoundCap,
                 style.join==mapstyle::Join::Miter?Qt::MiterJoin:Qt::RoundJoin);
        pen.setMiterLimit(4);
        if(dashed)pen.setDashPattern({qreal(style.dashOn/style.width),qreal(style.dashOff/style.width)});
        painter->setPen(variable?QPen(Qt::NoPen):pen);painter->setBrush(variable?QBrush(colorFor(style)):QBrush(Qt::NoBrush));
        QPainterPath path,variableShape;stroke::Point chainStart{};bool connected=false;
        const double phaseScale=view_.scale*3.14159265358979323846/180;
        for(const auto& segment:stroke::chains(segments)) {
            auto ga=pandoeditor::Point{segment.start[0],segment.start[1]},gb=pandoeditor::Point{segment.end[0],segment.end[1]};
            auto a=projectPoint(ga,view_,offset),b=projectPoint(gb,view_,offset);
            if(!a.finite||!b.finite||(!a.visibleHemisphere&&!b.visibleHemisphere)){connected=false;continue;}
            const bool originalStartVisible=a.visibleHemisphere,originalEndVisible=b.visibleHemisphere;
            const bool clipped=view_.mode==ProjectionMode::Globe&&originalStartVisible!=originalEndVisible;
            if(view_.mode==ProjectionMode::Globe&&a.visibleHemisphere!=b.visibleHemisphere) {
                auto visible=a.visibleHemisphere?ga:gb,hidden=a.visibleHemisphere?gb:ga;
                for(int n=0;n<16;++n){pandoeditor::Point mid{(visible.x+hidden.x)/2,(visible.y+hidden.y)/2};if(projectPoint(mid,view_,offset).visibleHemisphere)visible=mid;else hidden=mid;}
                if(!a.visibleHemisphere)a=projectPoint(visible,view_,offset);else b=projectPoint(visible,view_,offset);
                connected=false;
            }
            const QPointF pa(a.x,a.y),pb(b.x,b.y);
            if(!variable) {
                if(clipped&&!dashed) {
                    if(!path.isEmpty()){painter->drawPath(path);path=QPainterPath();}
                    const auto delta=pb-pa;const double length=std::hypot(delta.x(),delta.y());
                    if(length>1e-9) {
                        const double half=style.width/2;const QPointF normal(-delta.y()/length,delta.x()/length);
                        QPainterPath shape;shape.addPolygon(QPolygonF{pa+normal*half,pb+normal*half,pb-normal*half,pa-normal*half});shape.closeSubpath();
                        if(style.cap==mapstyle::Cap::Round) {
                            QPainterPath caps;
                            if(originalStartVisible&&!(segment.flags&1))caps.addEllipse(pa,half,half);
                            if(originalEndVisible&&!(segment.flags&2))caps.addEllipse(pb,half,half);
                            shape=shape.united(caps);
                        }
                        painter->setPen(Qt::NoPen);painter->setBrush(colorFor(style));painter->drawPath(shape);
                        painter->setPen(pen);painter->setBrush(Qt::NoBrush);
                        recordStroke(draw.object,draw.geometry,style.alpha);
                    }
                    connected=false;continue;
                }
                if(dashed) {
                    pen.setDashOffset(segment.phase*phaseScale/style.width);painter->setPen(pen);painter->drawLine(pa,pb);
                }else {
                    if(!connected||!(segment.flags&1)){path.moveTo(pa);chainStart=segment.start;}
                    path.lineTo(pb);connected=(segment.flags&2)!=0;
                    if((segment.flags&16)&&stroke::equal(segment.end,chainStart))path.closeSubpath();
                }
            }else {
                const auto i=segment.input;
                const double ha=std::max(.1,double(packet.endpointWidths->at(i*2))+style.width)/2;
                const double hb=std::max(.1,double(packet.endpointWidths->at(i*2+1))+style.width)/2;
                const double dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);if(length<1e-9)continue;
                const QPointF normal(-dy/length,dx/length),delta=pb-pa;
                // The production Web bounded miter applies to each body's
                // connected endpoint, including variable-width and dashed bodies.
                const auto direction=[](QPointF d){const double n=std::hypot(d.x(),d.y());return n>.0001?d/n:QPointF(1,0);};
                const auto miter=[&](QPointF incoming,QPointF outgoing,double half) {
                    const auto sum=direction(incoming+outgoing);const QPointF n(-sum.y(),sum.x());
                    const double denominator=QPointF::dotProduct(n,normal);
                    if(std::abs(denominator)<.08)return normal*half;
                    const double scale=half/denominator;
                    return std::abs(scale)>half*4?normal*half:n*scale;
                };
                QPointF startOffset=normal*ha,endOffset=normal*hb;
                if(style.join==mapstyle::Join::Miter) {
                    if(originalStartVisible&&(segment.flags&1)) {
                        const auto previous=projectPoint({segment.previous[0],segment.previous[1]},view_,offset);
                        startOffset=miter(direction(pa-QPointF(previous.x,previous.y)),direction(delta),ha);
                    }
                    if(originalEndVisible&&(segment.flags&2)) {
                        const auto next=projectPoint({segment.next[0],segment.next[1]},view_,offset);
                        endOffset=miter(direction(delta),direction(QPointF(next.x,next.y)-pb),hb);
                    }
                }
                const auto body=[&](double from,double to) {
                    const auto left=pa+delta*(from/length),right=pa+delta*(to/length);
                    const auto wl=startOffset+(endOffset-startOffset)*(from/length),wr=startOffset+(endOffset-startOffset)*(to/length);
                    // Match addEllipse's winding so shared filled coverage is
                    // a union rather than cancelling at a variable-width cap.
                    variableShape.addPolygon(QPolygonF{left-wl,right-wr,right+wr,left+wl});variableShape.closeSubpath();
                };
                if(dashed) {
                    const double period=std::max(1.,double(style.dashOn+style.dashOff));
                    for(double start=-std::fmod(segment.phase*phaseScale,period);start<length;start+=period)
                        if(std::min(length,start+style.dashOn)>std::max(0.,start))body(std::max(0.,start),std::min(length,start+style.dashOn));
                }else {
                    body(0,length);
                    if(originalStartVisible&&(((segment.flags&1)&&style.join==mapstyle::Join::Round)||(!(segment.flags&1)&&style.cap==mapstyle::Cap::Round)))variableShape.addEllipse(pa,ha,ha);
                    if(originalEndVisible&&!(segment.flags&2)&&style.cap==mapstyle::Cap::Round)variableShape.addEllipse(pb,hb,hb);
                }
            }
            recordStroke(draw.object,draw.geometry,style.alpha);
        }
        if(variable){variableShape.setFillRule(Qt::WindingFill);painter->drawPath(variableShape);}
        else if(!dashed)painter->drawPath(path);
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
            worldPlan.fills.visible:worldPlan.strokes.visible;
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
            auto positions=std::make_shared<std::vector<float>>();positions->reserve(count*2);
            for(std::size_t i=0;i+1<count;i+=2) {
                for(const auto vertex:{indices[start+i],indices[start+i+1]}) {
                    positions->push_back(float(mesh.positionsMicrodegrees[vertex*2]/1e6));
                    positions->push_back(float(mesh.positionsMicrodegrees[vertex*2+1]/1e6));
                }
            }
            StrokeDrawPacket boundary;boundary.object={"territorial",base.id};
            if(country<scene_->worldBase->ranges.size())boundary.geometry={scene_->worldBase->ranges[country].geometryId,1};
            boundary.geometryPacket.startsEnds=positions;boundary.geometryPacket.segmentCount=count/2;
            drawStroke(boundary,offset,style);
        }
        painter->restore();
    };

    // Only the startup placeholder predates canonical document draw ordering.
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
                           mapstyle::InteractionRole role,bool vertices=false) {
        const auto highlight=fixedInteractionStyle(role,scene_->interaction.styleOptions,view_);
        if(highlight.fillAlpha>0) {
            auto tint=highlight;tint.alpha=highlight.fillAlpha;tint.fillAlpha=1;
            for(const auto& draw:scene_->polygons)if(draw.object==ref)drawPolygon(painter,draw,offset,&tint);
        }
        for(const auto& draw:scene_->strokes)if(draw.object==ref) {
            auto style=highlight;style.blendMode=draw.style.blendMode;
            drawStroke(draw,offset,style);
            if(vertices)strokeVertices(draw,offset,highlight.color);
        }
        for(const auto& draw:scene_->points)if(draw.object==ref) {
            drawPoint(draw,offset,highlight,6);
        }
    };

    for(const double offset:copies) {
        const auto worldOutline=[&](const pandoeditor::ObjectRef& ref,
                                    mapstyle::InteractionRole role) {
            if(ref.domain!="territorial"||!scene_->worldBase||
               !scene_->worldBase->mesh||scene_->worldBase->startupPreview())return;
            const auto style=fixedInteractionStyle(role,scene_->interaction.styleOptions,view_);
            if(style.fillAlpha>0) {
                auto tint=style;tint.alpha=style.fillAlpha;tint.fillAlpha=1;
                for(const auto index:worldRangeIndicesForOwner(*scene_->worldBase,ref.id))drawWorld(PrimitiveKind::WorldFill,index,offset,&tint);
            }
            if(style.width<=0||style.alpha<=0)return;
            for(const auto index:worldRangeIndicesForOwner(*scene_->worldBase,ref.id))
                drawWorld(PrimitiveKind::WorldStroke,index,offset,&style);
        };
        for(const auto& candidate:scene_->interaction.candidates)
            if(!containsRef(scene_->interaction.selected,candidate)) {
                outline(candidate,offset,mapstyle::InteractionRole::Candidate);
                worldOutline(candidate,mapstyle::InteractionRole::Candidate);
            }
        if(scene_->interaction.hover&&
           !containsRef(scene_->interaction.selected,*scene_->interaction.hover)) {
            outline(*scene_->interaction.hover,offset,mapstyle::InteractionRole::Hover);
            worldOutline(*scene_->interaction.hover,mapstyle::InteractionRole::Hover);
        }
        for(const auto& selected:scene_->interaction.selected)
            if(!scene_->interaction.primary||selected!=*scene_->interaction.primary) {
                outline(selected,offset,mapstyle::InteractionRole::Secondary,true);
                worldOutline(selected,mapstyle::InteractionRole::Secondary);
            }
        if(scene_->interaction.primary) {
            outline(*scene_->interaction.primary,offset,mapstyle::InteractionRole::Primary,true);
            worldOutline(*scene_->interaction.primary,mapstyle::InteractionRole::Primary);
        }
        if(scene_->interaction.editTarget) {
            outline(*scene_->interaction.editTarget,offset,mapstyle::InteractionRole::EditTarget,true);
            worldOutline(*scene_->interaction.editTarget,mapstyle::InteractionRole::EditTarget);
        }
    }
    if(paintingForSceneGraph_) {
        std::lock_guard lock(presentationMutex_);
        paintedFrame_=frame_;paintedStrokeInventory_=std::move(actualStrokes);
    }
}
