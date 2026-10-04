#include "mapscenenode.h"
#include "mapmaterial.h"
#include "resourceidentity.h"
#include <QSGGeometryNode>
#include <QSGGeometry>
#include <QElapsedTimer>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
struct FillVertex {float x,y;};
struct StrokeVertex {float ax,ay,bx,by,side,endpoint,width;};
struct PointVertex {float longitude,latitude,cornerX,cornerY;};

const QSGGeometry::AttributeSet& attributes(MapPrimitive primitive) {
    static QSGGeometry::Attribute fill[]{QSGGeometry::Attribute::create(0,2,QSGGeometry::FloatType,true)};
    static QSGGeometry::Attribute stroke[]{QSGGeometry::Attribute::create(0,4,QSGGeometry::FloatType,true),
        QSGGeometry::Attribute::create(1,2,QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(2,1,QSGGeometry::FloatType)};
    static QSGGeometry::Attribute point[]{QSGGeometry::Attribute::create(0,2,QSGGeometry::FloatType,true),
        QSGGeometry::Attribute::create(1,2,QSGGeometry::FloatType)};
    static const QSGGeometry::AttributeSet fillSet{1,sizeof(FillVertex),fill};
    static const QSGGeometry::AttributeSet strokeSet{3,sizeof(StrokeVertex),stroke};
    static const QSGGeometry::AttributeSet pointSet{2,sizeof(PointVertex),point};
    return primitive==MapPrimitive::Fill?fillSet:primitive==MapPrimitive::Stroke?strokeSet:pointSet;
}
struct Entry final : QSGGeometryNode {
    std::string key;
    MapResourceIdentity resource;
    std::size_t bytes=0;
    RenderStyle style;
    MapPrimitive kind;
    BlendMode blend;
    int world=0;
    Entry(std::string id,MapPrimitive primitive,BlendMode mode)
        :key(std::move(id)),kind(primitive),blend(mode) {
        setMaterial(new MapMaterial(kind,blend));setFlag(OwnsMaterial,true);
    }
    MapMaterial* mapMaterial() const {return static_cast<MapMaterial*>(material());}
    void allocate(int vertices,int indexCount=0) {
        auto* data=new QSGGeometry(attributes(kind),vertices,indexCount,QSGGeometry::UnsignedIntType);
        data->setDrawingMode(QSGGeometry::DrawTriangles);
        data->setVertexDataPattern(QSGGeometry::StaticPattern);
        data->setIndexDataPattern(QSGGeometry::StaticPattern);
        setGeometry(data);setFlag(OwnsGeometry,true);
        bytes=std::size_t(vertices)*std::size_t(data->sizeOfVertex())+
              std::size_t(indexCount)*sizeof(std::uint32_t);
        markDirty(DirtyGeometry);
    }
};
std::string key(const std::string& source,MapPrimitive kind,int world,bool interaction=false) {
    return source+"/"+std::to_string(int(kind))+"/"+std::to_string(world)+
        (interaction?"/interaction":"/base");
}
bool contains(const std::vector<pandoeditor::ObjectRef>& refs,const pandoeditor::ObjectRef& object) {
    return std::find(refs.begin(),refs.end(),object)!=refs.end();
}
}

void MapSceneNode::sync(const std::shared_ptr<const RenderScene>& scene,
                        const MapViewState& view,const MapFlatViewport& flat,
                        MapGpuStats& stats,std::size_t uploadBudgetBytes,
                        const WorldRenderPlan* framePlan) {
    QElapsedTimer syncClock;syncClock.start();++stats.syncCount;
    struct Timing {MapGpuStats& stats;QElapsedTimer& clock;
        ~Timing(){stats.syncMilliseconds=clock.nsecsElapsed()/1.e6;}} timing{stats,syncClock};
    const WorldRenderPlan emptyPlan;
    const auto& worldPlan=framePlan?*framePlan:(scene?scene->worldPlan:emptyPlan);
    const auto& worldOffsets=worldPlan.worldOffsets;
    const auto& fills=worldPlan.fills;
    const auto& strokes=worldPlan.strokes;
    // The canonical globe base is a static mesh. Batch only adjacent compatible
    // draw commands; hemisphere/screen clipping remains in the shaders. Keeping
    // the batches view-independent avoids RHI buffer churn at country bounds.
    const bool batchBase=view.mode==ProjectionMode::Globe&&scene&&scene->worldBase&&
        scene->worldBase->mesh&&!scene->worldBase->mesh->preview&&
        worldOffsets.size()==1&&worldOffsets.front()==0;
    stats.visibleCountryCount=0;
    stats.drawIndexCount=0;
    stats.fullIndexCount=(fills.fullIndexCount+strokes.fullIndexCount)*worldOffsets.size();
    if(scene&&scene->worldBase&&scene->worldBase->mesh) {
        const auto& mesh=*scene->worldBase->mesh;
        for(std::size_t i=0;i<scene->worldCountries.size();++i)
            if(i<fills.visible.size()&&fills.visible[i]&&scene->worldCountries[i].visible)
                ++stats.visibleCountryCount;
        const auto submit=[&](PrimitiveKind kind,std::size_t i) {
            if(i>=scene->worldCountries.size()||!scene->worldCountries[i].visible)return;
            if(kind==PrimitiveKind::WorldFill&&(batchBase||(i<fills.visible.size()&&fills.visible[i])))
                stats.drawIndexCount+=mesh.countryTriangleRanges[i*2+1]*worldOffsets.size();
            if(kind==PrimitiveKind::WorldStroke&&(batchBase||(i<strokes.visible.size()&&strokes.visible[i])))
                stats.drawIndexCount+=mesh.countryBoundaryRanges[i*2+1]*worldOffsets.size();
        };
        if(scene->worldBase->startupPreview())for(std::size_t i=0;i<scene->worldCountries.size();++i) {
            submit(PrimitiveKind::WorldFill,i);submit(PrimitiveKind::WorldStroke,i);
        }
        else for(const auto& command:scene->drawSequence)
            if(command.primitive==PrimitiveKind::WorldFill||
               command.primitive==PrimitiveKind::WorldStroke)
                submit(command.primitive,command.index);
    }
    stats.uploadBytesThisFrame=0;stats.uploadsPending=false;
    const bool samePreparedScene=scene&&lastScene_&&
        (scene==lastScene_||(scene->preparationIdentity&&scene->preparationIdentity==lastScene_->preparationIdentity))&&
        scene->worldBase==lastScene_->worldBase&&
        scene->revisions.geometry==lastScene_->revisions.geometry&&
        scene->revisions.presentation==lastScene_->revisions.presentation&&
        scene->revisions.selection==lastScene_->revisions.selection&&
        scene->revisions.dataset==lastScene_->revisions.dataset;
    if(!pending_&&samePreparedScene&&worldOffsets==lastOffsets_&&
       view.mode==lastMode_&&
       (batchBase||(fills.visible==lastFillVisibility_&&strokes.visible==lastStrokeVisibility_))) {
        // QML pan/zoom and globe rotation change uniforms only. The SG tree,
        // vertex/index buffers and render packet pointers are untouched.
        for(auto* child=firstChild();child;child=child->nextSibling()) {
            auto* entry=static_cast<Entry*>(child);
            auto* material=entry->mapMaterial();
            const auto f0=material->flat0,f1=material->flat1;
            const auto g0=material->globe0,g1=material->globe1;
            material->setView(view,flat.originX,flat.originY,flat.mapScale,
                              flat.cosLatitude,flat.minX,flat.maxLatitude,float(entry->world));
            if(f0!=material->flat0||f1!=material->flat1||
               g0!=material->globe0||g1!=material->globe1) {
                entry->markDirty(QSGNode::DirtyMaterial);
                ++stats.viewUniformUpdateCount;
            }
        }
        lastScene_=scene;stats.sceneRevision=scene->revision;
        return;
    }
    std::map<std::string,Entry*> old;
    const auto retire=[&](Entry* node) {
        if(node->geometry())++stats.resourceRetirementCount;
        delete node;
    };
    ++stats.treeRebuildCount;stats.drawNodes=0;stats.strokeBytes=0;
    // Entry identities own the actual source buffers. Also retain the previous
    // preparation until reconciliation finishes, including entries retired here.
    const auto previousScene=lastScene_;
    (void)previousScene;
    for(auto* child=firstChild();child;child=child->nextSibling()) {
        auto* entry=static_cast<Entry*>(child);
        old.emplace(entry->key,entry);
    }
    std::vector<Entry*> ordered;
    stats.geometryBytes=0;
    pending_=false;
    lastScene_=scene;lastOffsets_=worldOffsets;lastMode_=view.mode;
    lastFillVisibility_=fills.visible;lastStrokeVisibility_=strokes.visible;
    if(!scene) {
        for(auto& [name,node]:old)retire(node);
        stats.liveResourceCount=0;stats.liveResourceBytes=0;
        stats.sceneRevision=0;
        return;
    }
    stats.sceneRevision=scene->revision;
    scheduler_.beginFrame(scene->revision,uploadBudgetBytes);
    auto install=[&](const std::string& id,MapPrimitive kind,BlendMode blend,
                     const RenderStyle& style,const MapResourceIdentity& resource,
                     int world,bool interaction,std::size_t estimatedBytes,
                     bool protectedGeometry,auto upload) {
        const std::string identity=key(id,kind,world,interaction);
        Entry* node=nullptr;
        if(auto it=old.find(identity);it!=old.end()) {
            node=it->second;old.erase(it);
            if(node->kind!=kind||node->resource!=resource) {
                retire(node);node=nullptr;
            }
        }
        if(!node) {
            const auto prefix=id+"/"+std::to_string(int(kind))+"/";
            for(auto it=old.lower_bound(prefix);it!=old.end()&&
                it->first.compare(0,prefix.size(),prefix)==0;++it) {
                auto* candidate=it->second;
                if(candidate->kind!=kind||candidate->resource!=resource||
                   (it->first.find("/interaction")!=std::string::npos)!=interaction)continue;
                node=candidate;old.erase(it);node->key=identity;break;
            }
        }
        if(!node) {
            if(!scheduler_.reserve(estimatedBytes,protectedGeometry)) {
                pending_=true;return;
            }
            node=new Entry(identity,kind,blend);
            QElapsedTimer uploadClock;uploadClock.start();
            upload(*node);
            const auto ms=uploadClock.nsecsElapsed()/1.e6;
            stats.uploadMilliseconds+=ms;
            if(kind==MapPrimitive::Stroke)stats.strokeUploadMilliseconds+=ms;
            stats.uploadedBytes+=node->bytes;
            node->resource=resource;
            ++stats.geometryUploadCount;
            ++stats.resourceCreationCount;
            if(interaction)++stats.interactionGeometryUploadCount;
            else ++stats.baseGeometryUploadCount;
        }
        if(node->blend!=blend) {
            auto* replacement=new MapMaterial(kind,blend);
            auto* oldMaterial=node->material();
            node->setFlag(QSGGeometryNode::OwnsMaterial,false);
            node->setMaterial(replacement);delete oldMaterial;
            node->setFlag(QSGGeometryNode::OwnsMaterial,true);node->blend=blend;
            node->markDirty(QSGNode::DirtyMaterial);
            ++stats.materialUpdateCount;
        }
        node->world=world;
        auto* material=node->mapMaterial();
        const auto oldFlat0=material->flat0,oldFlat1=material->flat1;
        const auto oldGlobe0=material->globe0,oldGlobe1=material->globe1;
        const auto oldColor=material->color,oldEffects=material->effects;
        material->setView(view,flat.originX,flat.originY,flat.mapScale,
                          flat.cosLatitude,flat.minX,flat.maxLatitude,float(world));
        material->setStyle(style,kind==MapPrimitive::Point&&interaction?-6.f:
                           interaction?6.f:3.f);
        if(oldFlat0!=material->flat0||oldFlat1!=material->flat1||
           oldGlobe0!=material->globe0||oldGlobe1!=material->globe1) {
            ++stats.viewUniformUpdateCount;node->markDirty(QSGNode::DirtyMaterial);
        }
        if(oldColor!=material->color||oldEffects!=material->effects) {
            ++stats.materialUpdateCount;node->markDirty(QSGNode::DirtyMaterial);
        }
        stats.geometryBytes+=node->bytes;
        ++stats.drawNodes;
        if(kind==MapPrimitive::Stroke)stats.strokeBytes+=node->bytes;
        ordered.push_back(node);
    };
    auto fill=[&](const PolygonDrawPacket& draw,int world) {
        const auto& packet=draw.geometryPacket;
        if(!packet.positions||!packet.indices)return;
        const auto& index=*(view.mode==ProjectionMode::Globe?packet.globeIndices:packet.indices);
        if(packet.vertexCount>INT_MAX||index.size()>INT_MAX||index.empty())return;
        auto resource=mapDrawResourceIdentity(draw);
        resource.buffers={packet.positions,view.mode==ProjectionMode::Globe?packet.globeIndices:packet.indices,{}};
        resource.counts={packet.vertexCount,index.size(),packet.positions->size()};
        install(draw.key,MapPrimitive::Fill,draw.style.blendMode,draw.style,
                resource,world,false,
                packet.vertexCount*sizeof(FillVertex)+index.size()*sizeof(std::uint32_t),
                contains(scene->interaction.selected,draw.object)||
                    (scene->interaction.editTarget&&*scene->interaction.editTarget==draw.object),
                [&](Entry& node) {
            node.allocate(int(packet.vertexCount),int(index.size()));
            auto* vertex=static_cast<FillVertex*>(node.geometry()->vertexData());
            for(std::size_t i=0;i<packet.vertexCount;++i)
                vertex[i]={packet.positions->at(i*2),packet.positions->at(i*2+1)};
            std::memcpy(node.geometry()->indexData(),index.data(),index.size()*sizeof(std::uint32_t));
        });
    };
    auto worldFill=[&](std::size_t country,int world) {
        if(!scene->worldBase||!scene->worldBase->mesh||
           country>=scene->worldCountries.size()||!scene->worldCountries[country].visible||
           (country<fills.visible.size()&&!fills.visible[country]))return;
        const auto& mesh=*scene->worldBase->mesh;
        const auto start=mesh.countryTriangleRanges.at(country*2);
        const auto count=mesh.countryTriangleRanges.at(country*2+1);
        if(!count)return;
        auto first=std::lower_bound(mesh.countryIndices.begin(),mesh.countryIndices.end(),
            std::uint16_t(country));
        auto last=std::upper_bound(first,mesh.countryIndices.end(),std::uint16_t(country));
        const auto begin=std::size_t(first-mesh.countryIndices.begin());
        const auto vertices=std::size_t(last-first);
        if(vertices>INT_MAX||count>INT_MAX)return;
        const auto& countryDraw=scene->worldCountries[country];
        MapResourceIdentity resource;resource.buffers[0]=scene->worldBase->mesh;
        resource.object={"territorial",countryDraw.id};
        if(country<scene->worldBase->ranges.size())resource.geometry={scene->worldBase->ranges[country].geometryId,1};
        resource.slice="fill/"+std::to_string(country)+"/"+std::to_string(begin)+"/"+std::to_string(start);
        resource.counts={vertices,count,mesh.triangleIndices.size()};
        // Several immutable mesh slots can belong to one logical country.
        // The owner alone is not a geometry identity (e.g. overseas islands).
        install("world/"+std::to_string(country)+"/"+countryDraw.id,MapPrimitive::Fill,countryDraw.fill.blendMode,
                countryDraw.fill,resource,world,false,
                vertices*sizeof(FillVertex)+count*sizeof(std::uint32_t),false,[&](Entry& node) {
            node.allocate(int(vertices),int(count));
            auto* output=static_cast<FillVertex*>(node.geometry()->vertexData());
            auto* indices=node.geometry()->indexDataAsUInt();
            for(std::size_t i=0;i<vertices;++i)
                output[i]={mesh.positionsMicrodegrees[(begin+i)*2]/1000000.f,
                           mesh.positionsMicrodegrees[(begin+i)*2+1]/1000000.f};
            for(std::size_t i=0;i<count;++i)indices[i]=mesh.triangleIndices[start+i]-std::uint32_t(begin);
        });
    };
    auto worldStroke=[&](std::size_t country,int world,RenderStyle style,
                         const std::string& channel="") {
        if(!scene->worldBase||!scene->worldBase->mesh||country>=scene->worldCountries.size()||
           !scene->worldCountries[country].visible||
           (channel.empty()&&country<strokes.visible.size()&&!strokes.visible[country]))return;
        const auto& mesh=*scene->worldBase->mesh;
        const auto start=mesh.countryBoundaryRanges.at(country*2);
        const auto count=mesh.countryBoundaryRanges.at(country*2+1);
        if(!count||count/2>std::size_t(INT_MAX/4))return;
        MapResourceIdentity resource;resource.buffers[0]=scene->worldBase->mesh;
        resource.object={"territorial",scene->worldCountries[country].id};
        if(country<scene->worldBase->ranges.size())resource.geometry={scene->worldBase->ranges[country].geometryId,1};
        resource.slice="stroke/"+std::to_string(country)+"/"+std::to_string(start);
        resource.counts={count/2,count,mesh.lineIndices.size()};
        install("world/"+std::to_string(country)+"/"+scene->worldCountries[country].id+
                (channel.empty()?"":"/"+channel),MapPrimitive::Stroke,style.blendMode,
                style,resource,world,!channel.empty(),
                (count/2)*(4*sizeof(StrokeVertex)+6*sizeof(std::uint32_t)),
                !channel.empty(),[&](Entry& node) {
            const auto segments=count/2;
            node.allocate(int(segments*4),int(segments*6));
            auto* vertex=static_cast<StrokeVertex*>(node.geometry()->vertexData());
            auto* indices=node.geometry()->indexDataAsUInt();
            for(std::size_t i=0;i<segments;++i) {
                const auto a=std::size_t(mesh.lineIndices[start+i*2]);
                const auto b=std::size_t(mesh.lineIndices[start+i*2+1]);
                const float x0=mesh.positionsMicrodegrees[a*2]/1000000.f;
                const float y0=mesh.positionsMicrodegrees[a*2+1]/1000000.f;
                const float x1=mesh.positionsMicrodegrees[b*2]/1000000.f;
                const float y1=mesh.positionsMicrodegrees[b*2+1]/1000000.f;
                for(int j=0;j<4;++j)vertex[i*4+j]={x0,y0,x1,y1,
                    (j%2)?1.f:-1.f,(j/2)?1.f:0.f,0.f};
                const std::uint32_t base=std::uint32_t(i*4);
                const std::uint32_t quad[]{base,base+1,base+2,base+2,base+1,base+3};
                std::memcpy(indices+i*6,quad,sizeof(quad));
            }
        });
    };
    auto stroke=[&](const StrokeDrawPacket& draw,int world,bool interaction,
                    RenderStyle style,const std::string& channel="") {
        const auto& packet=draw.geometryPacket;
        if(!packet.startsEnds||packet.segmentCount>INT_MAX/4||!packet.segmentCount)return;
        auto resource=mapDrawResourceIdentity(draw);
        resource.buffers={packet.startsEnds,packet.endpointWidths,{}};
        resource.counts={packet.segmentCount,packet.startsEnds->size(),packet.endpointWidths?packet.endpointWidths->size():0};
        install(draw.key+(interaction?"/"+channel:""),MapPrimitive::Stroke,style.blendMode,style,
                resource,world,interaction,
                packet.segmentCount*(4*sizeof(StrokeVertex)+6*sizeof(std::uint32_t)),
                interaction||contains(scene->interaction.selected,draw.object)||
                    (scene->interaction.editTarget&&*scene->interaction.editTarget==draw.object),
                [&](Entry& node) {
            node.allocate(int(packet.segmentCount*4),int(packet.segmentCount*6));
            auto* vertex=static_cast<StrokeVertex*>(node.geometry()->vertexData());
            auto* index=node.geometry()->indexDataAsUInt();
            const bool variable=packet.endpointWidths&&
                packet.endpointWidths->size()==packet.segmentCount*2;
            for(std::size_t i=0;i<packet.segmentCount;++i) {
                const auto* s=packet.startsEnds->data()+i*4;
                for(int j=0;j<4;++j) {
                    const auto endpoint=(j/2)?1u:0u;
                    const float width=variable?packet.endpointWidths->at(i*2+endpoint):0.f;
                    vertex[i*4+j]={s[0],s[1],s[2],s[3],
                        (j%2)?1.f:-1.f,(j/2)?1.f:0.f,width};
                }
                const std::uint32_t base=std::uint32_t(i*4);
                const std::uint32_t quad[]{base,base+1,base+2,base+2,base+1,base+3};
                std::memcpy(index+i*6,quad,sizeof(quad));
            }
        });
    };
    auto point=[&](const PointDrawPacket& draw,int world,bool interaction=false,
                   RenderStyle highlight={},const std::string& channel="") {
        // Label glyphs and flags have a separate QML safe-area layer.
        if(draw.object.domain=="label")return;
        const auto& packet=draw.geometryPacket;
        if(!packet.positions||packet.pointCount>INT_MAX/4||!packet.pointCount)return;
        const auto markerKey=draw.manualPosition?
            draw.key+"/"+std::to_string(draw.manualPosition->x)+"/"+
            std::to_string(draw.manualPosition->y):draw.key;
        const auto& style=interaction?highlight:draw.style;
        auto resource=mapDrawResourceIdentity(draw);resource.buffers[0]=packet.positions;
        resource.counts={packet.pointCount,packet.positions->size(),0};
        resource.slice=markerKey;
        resource.manualPosition=bool(draw.manualPosition);
        if(draw.manualPosition)resource.position={draw.manualPosition->x,draw.manualPosition->y};
        install(markerKey+(interaction?"/"+channel:""),MapPrimitive::Point,
                style.blendMode,style,
                resource,world,interaction,
                packet.pointCount*(4*sizeof(PointVertex)+6*sizeof(std::uint32_t)),
                interaction||contains(scene->interaction.selected,draw.object)||
                    (scene->interaction.editTarget&&*scene->interaction.editTarget==draw.object),
                [&](Entry& node) {
            node.allocate(int(packet.pointCount*4),int(packet.pointCount*6));
            auto* vertex=static_cast<PointVertex*>(node.geometry()->vertexData());
            auto* index=node.geometry()->indexDataAsUInt();
            for(std::size_t i=0;i<packet.pointCount;++i) {
                const auto lon=draw.manualPosition?float(draw.manualPosition->x):packet.positions->at(i*2);
                const auto lat=draw.manualPosition?float(draw.manualPosition->y):packet.positions->at(i*2+1);
                for(int j=0;j<4;++j)vertex[i*4+j]={lon,lat,(j%2)?1.f:-1.f,(j/2)?1.f:-1.f};
                const std::uint32_t base=std::uint32_t(i*4);
                const std::uint32_t quad[]{base,base+1,base+2,base+2,base+1,base+3};
                std::memcpy(index+i*6,quad,sizeof(quad));
            }
        });
    };
    auto selectionVertices=[&](const StrokeDrawPacket& draw,int world,
                               const std::string& channel) {
        const auto& packet=draw.geometryPacket;
        if(!packet.startsEnds||packet.segmentCount>INT_MAX/8||!packet.segmentCount)return;
        RenderStyle style;style.color=0x163e64;style.alpha=1;
        auto resource=mapDrawResourceIdentity(draw);resource.buffers[0]=packet.startsEnds;
        resource.counts={packet.segmentCount,packet.startsEnds->size(),0};
        install(draw.key+"/vertices/"+channel,MapPrimitive::Point,BlendMode::Normal,style,
                resource,world,true,
                packet.segmentCount*2*(4*sizeof(PointVertex)+6*sizeof(std::uint32_t)),true,
                [&](Entry& node) {
            const auto count=packet.segmentCount*2;
            node.allocate(int(count*4),int(count*6));
            auto* vertex=static_cast<PointVertex*>(node.geometry()->vertexData());
            auto* index=node.geometry()->indexDataAsUInt();
            for(std::size_t i=0;i<count;++i) {
                const auto lon=packet.startsEnds->at(i*2),lat=packet.startsEnds->at(i*2+1);
                for(int j=0;j<4;++j)vertex[i*4+j]={lon,lat,(j%2)?1.f:-1.f,(j/2)?1.f:-1.f};
                const std::uint32_t base=std::uint32_t(i*4);
                const std::uint32_t quad[]{base,base+1,base+2,base+2,base+1,base+3};
                std::memcpy(index+i*6,quad,sizeof(quad));
            }
        });
    };
    if(scene->worldBase&&scene->worldBase->startupPreview())
        for(double offset:worldOffsets)for(std::size_t i=0;i<scene->worldCountries.size();++i) {
            worldFill(i,int(offset));
            worldStroke(i,int(offset),scene->worldCountries[i].boundary);
        }
    struct BasePart {std::size_t slot,first,vertices,indexFirst,indices;};
    std::vector<BasePart> batch;
    MapPrimitive batchKind=MapPrimitive::Fill;RenderStyle batchStyle;
    std::size_t batchBytes=0;
    const auto sameStyle=[](const RenderStyle& a,const RenderStyle& b) {
        return a.color==b.color&&a.alpha==b.alpha&&a.fillAlpha==b.fillAlpha&&
            a.width==b.width&&a.dashOn==b.dashOn&&a.dashOff==b.dashOff&&a.blendMode==b.blendMode;
    };
    const auto flushBase=[&] {
        if(batch.empty())return;
        const auto& mesh=*scene->worldBase->mesh;
        std::string id="base-batch";std::size_t vertices=0,indices=0;
        for(const auto& part:batch) {
            id+="/"+std::to_string(part.slot);
            vertices+=part.vertices;indices+=part.indices;
        }
        MapResourceIdentity resource;resource.buffers[0]=scene->worldBase->mesh;
        resource.counts={vertices,indices,batch.size()};
        resource.slice=std::to_string(int(batchKind));
        for(const auto& part:batch)resource.slice+="/"+std::to_string(part.slot)+":"+
            std::to_string(part.first)+":"+std::to_string(part.vertices)+":"+
            std::to_string(part.indexFirst)+":"+std::to_string(part.indices);
        install(id,batchKind,batchStyle.blendMode,batchStyle,resource,
                0,false,batchBytes,false,[&](Entry& node) {
            node.allocate(int(vertices),int(indices));
            auto* outputIndices=node.geometry()->indexDataAsUInt();
            std::size_t vertexOffset=0,indexOffset=0;
            for(const auto& part:batch) {
                if(batchKind==MapPrimitive::Fill) {
                    auto* output=static_cast<FillVertex*>(node.geometry()->vertexData());
                    for(std::size_t i=0;i<part.vertices;++i)
                        output[vertexOffset+i]={mesh.positionsMicrodegrees[(part.first+i)*2]/1000000.f,
                                               mesh.positionsMicrodegrees[(part.first+i)*2+1]/1000000.f};
                    for(std::size_t i=0;i<part.indices;++i)
                        outputIndices[indexOffset+i]=std::uint32_t(vertexOffset)+mesh.triangleIndices[part.indexFirst+i]-std::uint32_t(part.first);
                } else {
                    auto* output=static_cast<StrokeVertex*>(node.geometry()->vertexData());
                    for(std::size_t i=0;i<part.vertices/4;++i) {
                        const auto a=std::size_t(mesh.lineIndices[part.indexFirst+i*2]);
                        const auto b=std::size_t(mesh.lineIndices[part.indexFirst+i*2+1]);
                        const float ax=mesh.positionsMicrodegrees[a*2]/1000000.f,ay=mesh.positionsMicrodegrees[a*2+1]/1000000.f;
                        const float bx=mesh.positionsMicrodegrees[b*2]/1000000.f,by=mesh.positionsMicrodegrees[b*2+1]/1000000.f;
                        for(int j=0;j<4;++j)output[vertexOffset+i*4+j]={ax,ay,bx,by,(j%2)?1.f:-1.f,(j/2)?1.f:0.f,0.f};
                        const std::uint32_t base=std::uint32_t(vertexOffset+i*4);
                        const std::uint32_t quad[]{base,base+1,base+2,base+2,base+1,base+3};
                        std::memcpy(outputIndices+indexOffset+i*6,quad,sizeof(quad));
                    }
                }
                vertexOffset+=part.vertices;indexOffset+=part.indices;
            }
        });
        batch.clear();batchBytes=0;
    };
    for(const auto& command:scene->drawSequence) {
        if(batchBase&&(command.primitive==PrimitiveKind::WorldFill||command.primitive==PrimitiveKind::WorldStroke)) {
            const auto slot=command.index;
            if(slot>=scene->worldCountries.size()||!scene->worldCountries[slot].visible)continue;
            const auto& mesh=*scene->worldBase->mesh;
            const auto kind=command.primitive==PrimitiveKind::WorldFill?MapPrimitive::Fill:MapPrimitive::Stroke;
            const auto& style=kind==MapPrimitive::Fill?scene->worldCountries[slot].fill:scene->worldCountries[slot].boundary;
            BasePart part{slot,0,0,0,0};
            if(kind==MapPrimitive::Fill) {
                const auto begin=std::lower_bound(mesh.countryIndices.begin(),mesh.countryIndices.end(),std::uint16_t(slot));
                const auto end=std::upper_bound(begin,mesh.countryIndices.end(),std::uint16_t(slot));
                part.first=std::size_t(begin-mesh.countryIndices.begin());part.vertices=std::size_t(end-begin);
                part.indexFirst=mesh.countryTriangleRanges.at(slot*2);part.indices=mesh.countryTriangleRanges.at(slot*2+1);
            } else {
                part.indexFirst=mesh.countryBoundaryRanges.at(slot*2);
                const auto segments=mesh.countryBoundaryRanges.at(slot*2+1)/2;
                part.vertices=segments*4;part.indices=segments*6;
            }
            if(!part.vertices||!part.indices)continue;
            if(part.vertices>INT_MAX||part.indices>INT_MAX)continue;
            const auto bytes=part.vertices*(kind==MapPrimitive::Fill?sizeof(FillVertex):sizeof(StrokeVertex))+part.indices*sizeof(std::uint32_t);
            // Bounded batches limit the upload cost of a color/order edit.
            if(!batch.empty()&&(kind!=batchKind||!sameStyle(style,batchStyle)||batchBytes+bytes>4*1024*1024))flushBase();
            batchKind=kind;batchStyle=style;batch.push_back(part);batchBytes+=bytes;
            continue;
        }
        flushBase();
        for(double offset:worldOffsets) {
            const int world=int(offset);
            if(command.primitive==PrimitiveKind::Polygon&&command.index<scene->polygons.size())
                fill(scene->polygons[command.index],world);
            else if(command.primitive==PrimitiveKind::Stroke&&command.index<scene->strokes.size())
                stroke(scene->strokes[command.index],world,false,
                       scene->strokes[command.index].style);
            else if(command.primitive==PrimitiveKind::Point&&command.index<scene->points.size())
                point(scene->points[command.index],world);
            else if(command.primitive==PrimitiveKind::WorldFill)
                worldFill(command.index,world);
            else if(command.primitive==PrimitiveKind::WorldStroke&&
                    command.index<scene->worldCountries.size())
                worldStroke(command.index,world,scene->worldCountries[command.index].boundary);
        }
    }
    flushBase();
    // Interaction is a separate final pass; the source packets remain immutable.
    const auto outline=[&](const pandoeditor::ObjectRef& ref,int world,
                           const std::string& channel,std::uint32_t color,
                           float width,bool vertices=false) {
        for(const auto& draw:scene->strokes)if(draw.object==ref) {
            auto style=draw.style;style.color=color;style.alpha=1;
            style.width=width;style.dashOn=style.dashOff=0;
            stroke(draw,world,true,style,channel);
            if(vertices&&draw.object.domain!="hydroBuiltin")selectionVertices(draw,world,channel);
        }
        for(const auto& draw:scene->points)if(draw.object==ref) {
            RenderStyle style;style.color=color;style.alpha=1;
            point(draw,world,true,style,channel);
        }
    };
    for(double offset:worldOffsets) {
        const int world=int(offset);
        if(scene->worldBase&&scene->worldBase->mesh&&!scene->worldBase->startupPreview()) {
            const auto baseHighlight=[&](const pandoeditor::ObjectRef& ref,
                                         const std::string& channel,std::uint32_t color,float width) {
                if(ref.domain!="territorial")return;
                RenderStyle style;style.color=color;style.width=width;
                for(const auto index:worldRangeIndicesForOwner(*scene->worldBase,ref.id))
                    worldStroke(index,world,style,channel);
            };
            for(const auto& candidate:scene->interaction.candidates)
                if(!contains(scene->interaction.selected,candidate))
                    baseHighlight(candidate,"candidate",0x8abddd,1.5f);
            if(scene->interaction.hover&&
               !contains(scene->interaction.selected,*scene->interaction.hover))
                baseHighlight(*scene->interaction.hover,"hover",0x4083bc,2.f);
            for(const auto& selected:scene->interaction.selected)
                if(!scene->interaction.primary||selected!=*scene->interaction.primary)
                    baseHighlight(selected,"secondary",0x163e64,2.f);
            if(scene->interaction.primary)
                baseHighlight(*scene->interaction.primary,"primary",0x163e64,3.f);
            if(scene->interaction.editTarget)
                baseHighlight(*scene->interaction.editTarget,"edit-target",0xe89b1a,3.5f);
        }
        for(const auto& candidate:scene->interaction.candidates)
            if(!contains(scene->interaction.selected,candidate))
                outline(candidate,world,"candidate",0x8abddd,1.5f);
        if(scene->interaction.hover &&
           !contains(scene->interaction.selected,*scene->interaction.hover))
            outline(*scene->interaction.hover,world,"hover",0x4083bc,2.f);
        for(const auto& selected:scene->interaction.selected)
            if(!scene->interaction.primary||selected!=*scene->interaction.primary)
                outline(selected,world,"secondary",0x163e64,2.f,true);
        if(scene->interaction.primary)
            outline(*scene->interaction.primary,world,"primary",0x163e64,3.f,true);
        if(scene->interaction.editTarget)
            outline(*scene->interaction.editTarget,world,"edit-target",0xe89b1a,3.5f,true);
    }
    // Removing/re-adding unchanged nodes invalidates Qt's RHI batches, even
    // when our QSGGeometry allocations were retained. Reconcile only actual
    // additions/order changes so camera culling and hover preserve GPU buffers.
    for(auto& [name,node]:old)retire(node);
    auto* cursor=firstChild();
    for(auto* node:ordered) {
        if(node==cursor) {cursor=cursor->nextSibling();continue;}
        if(node->parent()==this)removeChildNode(node);
        if(cursor)insertChildNodeBefore(node,cursor);else appendChildNode(node);
        ++stats.nodeAttachmentCount;
    }
    stats.uploadBytesThisFrame=scheduler_.frameBytes();
    stats.uploadsPending=pending_;
    stats.liveResourceCount=stats.drawNodes;stats.liveResourceBytes=stats.geometryBytes;
}
