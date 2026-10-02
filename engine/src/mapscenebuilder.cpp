#include <pandoeditor/map/mapscenebuilder.h>
#include <pandoeditor/map/scenepatch.h>
#include <pandoeditor/maprenderorder.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace {
using namespace pandoeditor;
constexpr std::uint64_t basis=14695981039346656037ull;
void mix(std::uint64_t& hash,std::uint64_t value) {
    for(int i=0;i<8;++i){hash^=(value>>(i*8))&0xff;hash*=1099511628211ull;}
}
void mix(std::uint64_t& hash,const std::string& value) {
    for(unsigned char c:value){hash^=c;hash*=1099511628211ull;}
    mix(hash,static_cast<std::uint64_t>(value.size()));
}
void mixDouble(std::uint64_t& hash,double value) {
    std::uint64_t bits;static_assert(sizeof(bits)==sizeof(value));
    std::memcpy(&bits,&value,sizeof(bits));mix(hash,bits);
}
std::uint64_t advance(std::uint64_t previous) {
    if(previous==std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("render domain revision overflow");
    return previous+1;
}
bool polygon(const Geometry& shape) {
    return shape.type=="Polygon"||shape.type=="MultiPolygon";
}
bool line(const Geometry& shape) {
    return shape.type=="LineString"||shape.type=="MultiLineString";
}
double orderValue(const MapRenderOrder& order) {
    return order.pass+order.group/10.0+order.object/100.0;
}
BlendMode blend(const std::string& name) {
    return name=="multiply"?BlendMode::Multiply:BlendMode::Normal;
}
RenderStyle styleFor(const ProjectDocument& doc,const ObjectRef& ref,
                     std::uint32_t defaultColor,double defaultOpacity=1) {
    RenderStyle style;style.color=defaultColor;
    const auto object=doc.presentation.objectStyles.find(ref);
    if(object!=doc.presentation.objectStyles.end()) {
        style.color=object->second.color;defaultOpacity*=object->second.opacity;
    }
    const auto group=ref.domain=="territorial"?[&]() {
        for(const auto& u:doc.units)if(u.id==ref.id)return territorialGroup(u.kind);
        return std::string{};
    }():contentGroup(doc,ref);
    if(ref.domain=="territorial") {
        const auto resolved=resolvedTerritorialPresentation(doc,ref);
        defaultOpacity*=resolved.effectiveAlpha;
        style.blendMode=blend(resolved.blendMode);
    } else {
        const auto it=doc.presentation.webPresentation.styles.find(group);
        if(it!=doc.presentation.webPresentation.styles.end()) {
            defaultOpacity*=it->second.opacity.value_or(1);
            style.blendMode=blend(it->second.blendMode.value_or("normal"));
        }
        if(const auto membership=doc.presentation.membership.find(ref);
           membership!=doc.presentation.membership.end())
            for(const auto& layer:doc.presentation.userLayers)
                if(layer.id==membership->second)defaultOpacity*=layer.opacity;
    }
    style.alpha=static_cast<float>(defaultOpacity);
    return style;
}
std::optional<GeometryRef> geometryFor(const ProjectDocument& doc,const ObjectRef& ref) {
    if(ref.domain=="territorial") {
        for(const auto& unit:doc.units)if(unit.id==ref.id)return unit.geometry;
    } else if(ref.domain=="hydro") {
        for(const auto& feature:doc.hydro)if(feature.id==ref.id)return feature.geometry;
    } else if(ref.domain=="generic") {
        for(const auto& feature:doc.genericFeatures)if(feature.id==ref.id)return feature.geometry;
    } else if(ref.domain=="label") {
        for(const auto& label:doc.labels)if(label.id==ref.id)return label.geometry;
    } else if(ref.domain=="distributionEntry") {
        for(const auto& entry:doc.distributionEntries)if(entry.id==ref.id) {
            if(entry.geometry)return entry.geometry;
            if(entry.territory)return geometryFor(doc,*entry.territory);
        }
    }
    return std::nullopt;
}
}

std::shared_ptr<const RenderScene> MapSceneBuilder::build(
    const ProjectSnapshot& snapshot,const MapViewState& view,
    const InteractionRenderPacket& interaction,
    const std::shared_ptr<const RenderScene>& previous) {
    return buildDocument(snapshot.document(),snapshot.revision(),view,interaction,previous);
}

std::shared_ptr<const RenderScene> MapSceneBuilder::buildDocument(
    const ProjectDocument& doc,std::uint64_t documentRevision,const MapViewState& view,
    const InteractionRenderPacket& interaction,
    const std::shared_ptr<const RenderScene>& previous) {
    return buildDocumentImpl(doc,documentRevision,view,interaction,previous,nullptr);
}

std::shared_ptr<const RenderScene> MapSceneBuilder::buildPatch(
    const ProjectSnapshot& snapshot,const MapViewState& view,
    const InteractionRenderPacket& interaction,const std::shared_ptr<const RenderScene>& previous,
    const std::set<ObjectRef>& changed) {
    if(!previous||changed.empty()||previous->worldBase!=worldBase_)
        return build(snapshot,view,interaction,previous);
    return buildDocumentImpl(snapshot.document(),snapshot.revision(),view,interaction,previous,&changed);
}

std::shared_ptr<const RenderScene> MapSceneBuilder::buildDocumentImpl(
    const ProjectDocument& doc,std::uint64_t documentRevision,const MapViewState& view,
    const InteractionRenderPacket& interaction,
    const std::shared_ptr<const RenderScene>& previous,
    const std::set<ObjectRef>* changed) {
    if(!validMapViewState(view))throw std::invalid_argument("invalid scene view");
    auto scene=std::make_shared<RenderScene>();
    scene->interaction=interaction;
    scene->worldBase=worldBase_;
    std::set<ObjectRef> protectedObjects(interaction.selected.begin(),interaction.selected.end());
    if(interaction.editTarget)protectedObjects.insert(*interaction.editTarget);
    cache_.protect(protectedObjects);
    std::set<std::string> baseCountries;
    if(worldBase_&&worldBase_->mesh) {
        if(worldBase_->ranges.size()!=258)throw std::invalid_argument("world base needs 258 ranges");
        if(worldBase_->mesh->preview)
            for(const auto& range:worldBase_->ranges)baseCountries.insert(range.ownerId);
        scene->worldCountries.reserve(258);
        for(const auto& range:worldBase_->ranges) {
            WorldCountryDraw base;base.id=range.ownerId;
            base.fill.color=0xa8c7db;base.boundary.color=0x61778a;
            base.boundary.width=1.2f;
            if(!worldBase_->mesh->preview) {
                const auto unit=std::find_if(doc.units.begin(),doc.units.end(),
                    [&](const auto& value){return value.id==range.ownerId;});
                if(unit==doc.units.end()||!(unit->geometry==GeometryRef{range.geometryId,1})) {
                    base.visible=false;
                } else {
                    const auto ref=territorialRef(range.ownerId);
                    base.visible=effectiveMapVisibility(doc,ref);
                    base.fill=styleFor(doc,ref,0xa8c7db);
                    base.boundary.alpha=base.fill.alpha;
                    baseCountries.insert(range.ownerId);
                    if(base.visible) {
                        int layerOrder=-1;
                        const auto& layerId=nativeLayerId(doc,ref);
                        for(std::size_t j=0;j<doc.presentation.userLayers.size();++j)
                            if(doc.presentation.userLayers[j].id==layerId){layerOrder=int(j);break;}
                        const auto i=scene->worldCountries.size();
                        const auto fillOrder=mapRenderOrder(doc,ref,RenderPrimitiveRole::Fill);
                        scene->drawSequence.push_back({PrimitiveKind::WorldFill,i,fillOrder,layerOrder});
                        if(resolvedTerritorialPresentation(doc,ref).boundaryVisible) {
                            const auto edgeOrder=mapRenderOrder(doc,ref,RenderPrimitiveRole::Boundary);
                            scene->drawSequence.push_back({PrimitiveKind::WorldStroke,i,edgeOrder,layerOrder});
                        }
                    }
                }
            }
            scene->worldCountries.push_back(std::move(base));
        }
    }
    if(changed)appendUnchangedScenePackets(*scene,*previous,*changed);
    auto add=[&](const ObjectRef& object,std::uint32_t color,double opacity=1) {
        if(changed&&!changed->count(object))return;
        if(!effectiveMapVisibility(doc,object))return;
        const auto ref=geometryFor(doc,object);
        if(!ref)return;
        const auto shape=doc.geometries.get(*ref);
        if(!shape)throw std::invalid_argument("scene has dangling geometry ref");
        const auto key=object.domain+":"+object.id;
        const auto style=styleFor(doc,object,color,opacity);
        bool independent=false;
        if(object.domain=="generic")for(const auto& feature:doc.genericFeatures)
            if(feature.id==object.id){independent=feature.fallbackOnly;break;}
        const auto lod=independent&&!protectedObjects.count(object)?
            int(quality_.backgroundLod):int(RenderLod::High);
        const auto preparation=lod!=int(RenderLod::High)&&view.mode==ProjectionMode::Globe?
            ProjectionPreparationPolicy::GlobeReady:ProjectionPreparationPolicy::Geographic;
        int layerOrder=-1;
        const auto& layerId=nativeLayerId(doc,object);
        for(std::size_t i=0;i<doc.presentation.userLayers.size();++i)
            if(doc.presentation.userLayers[i].id==layerId){layerOrder=static_cast<int>(i);break;}
        if(polygon(*shape)) {
            PolygonDrawPacket draw;draw.key=key;draw.object=object;draw.geometry=*ref;
            draw.geometryRevision=ref->version;draw.style=style;
            draw.drawOrder=mapRenderOrder(doc,object,RenderPrimitiveRole::Fill);
            draw.order=orderValue(draw.drawOrder);
            draw.geometryPacket=cache_.polygon(object,*ref,*shape,lod,preparation);
            const auto index=scene->polygons.size();scene->polygons.push_back(std::move(draw));
            scene->drawSequence.push_back({PrimitiveKind::Polygon,index,scene->polygons.back().drawOrder,layerOrder});
            bool boundary=true;
            if(object.domain=="territorial")boundary=resolvedTerritorialPresentation(doc,object).boundaryVisible;
            if(object.domain=="distributionEntry")boundary=doc.presentation.webPresentation.distributionSettings.boundaryVisible;
            const bool highlighted=std::find(interaction.selected.begin(),interaction.selected.end(),object)!=
                interaction.selected.end()||
                std::find(interaction.candidates.begin(),interaction.candidates.end(),object)!=
                interaction.candidates.end()||
                (interaction.primary&&*interaction.primary==object)||
                (interaction.hover&&*interaction.hover==object)||
                (interaction.editTarget&&*interaction.editTarget==object);
            if(boundary||highlighted) {
                StrokeDrawPacket stroke;stroke.key=key;stroke.object=object;stroke.geometry=*ref;
                stroke.geometryRevision=ref->version;stroke.style=style;
                if(object.domain=="territorial")for(const auto& unit:doc.units)
                    if(unit.id==object.id) {
                        stroke.style.color=0x61778a;
                        stroke.style.width=unit.kind==UnitKind::Country?1.2f:
                            unit.kind==UnitKind::Subunit?.9f:.7f;
                        if(unit.kind==UnitKind::Subunit){stroke.style.dashOn=4;stroke.style.dashOff=2;}
                        if(unit.kind==UnitKind::Region){stroke.style.dashOn=1.5f;stroke.style.dashOff=2;}
                        break;
                    }
                stroke.drawOrder=mapRenderOrder(doc,object,RenderPrimitiveRole::Boundary);
                stroke.order=orderValue(stroke.drawOrder);
                stroke.geometryPacket=cache_.stroke(object,*ref,*shape,lod,preparation);
                const auto strokeIndex=scene->strokes.size();scene->strokes.push_back(std::move(stroke));
                if(boundary)scene->drawSequence.push_back({PrimitiveKind::Stroke,strokeIndex,
                    scene->strokes.back().drawOrder,layerOrder});
            }
        } else if(line(*shape)) {
            StrokeDrawPacket draw;draw.key=key;draw.object=object;draw.geometry=*ref;
            draw.geometryRevision=ref->version;draw.style=style;
            draw.drawOrder=mapRenderOrder(doc,object,RenderPrimitiveRole::Line);
            draw.order=orderValue(draw.drawOrder);
            draw.geometryPacket=cache_.stroke(object,*ref,*shape,lod,preparation);
            const auto index=scene->strokes.size();scene->strokes.push_back(std::move(draw));
            scene->drawSequence.push_back({PrimitiveKind::Stroke,index,scene->strokes.back().drawOrder,layerOrder});
        } else if(shape->type=="Point"||shape->type=="MultiPoint") {
            PointDrawPacket draw;draw.key=key;draw.object=object;draw.geometry=*ref;
            draw.geometryRevision=ref->version;draw.style=style;
            draw.drawOrder=mapRenderOrder(doc,object,
                object.domain=="label"?RenderPrimitiveRole::Label:RenderPrimitiveRole::Point);
            draw.order=orderValue(draw.drawOrder);
            draw.geometryPacket=cache_.point(object,*ref,*shape,lod,preparation);
            if(object.domain=="label") {
                for(const auto& label:doc.labels)if(label.id==object.id){draw.labelText=label.name;break;}
                if(const auto it=doc.presentation.webPresentation.labelSettings.find(object);
                   it!=doc.presentation.webPresentation.labelSettings.end()) {
                    draw.pinned=it->second.pinned;draw.manualPosition=it->second.manualPosition;
                }
            }
            const auto index=scene->points.size();scene->points.push_back(std::move(draw));
            scene->drawSequence.push_back({PrimitiveKind::Point,index,scene->points.back().drawOrder,layerOrder});
        }
    };
    for(const auto& unit:doc.units)
        if(!baseCountries.count(unit.id))add(territorialRef(unit.id),0xa8c7db);
    for(const auto& hydro:doc.hydro)add({"hydro",hydro.id},hydro.color);
    for(const auto& feature:doc.genericFeatures)add({"generic",feature.id},feature.color);
    for(const auto& label:doc.labels)add({"label",label.id},0x222222);
    std::optional<std::string> selectedLayer;
    if(interaction.primary&&interaction.primary->domain=="distributionLayer")
        selectedLayer=interaction.primary->id;
    for(const auto& ref:visibleDistributionEntries(doc,selectedLayer)) {
        for(const auto& entry:doc.distributionEntries)if(entry.id==ref.id)
            for(const auto& layer:doc.distributionLayers)if(layer.id==entry.layerId) {
                add(ref,layer.color,distributionFillAlpha(entry.share));break;
            }
    }
    // A patch appends newly prepared packets after retained packets. Restore the
    // original document submission order for equal M5 draw-order keys.
    std::map<ObjectRef,std::size_t> submissionOrder;
    if(changed) {
        std::size_t rank=0;
        for(const auto& unit:doc.units)submissionOrder.emplace(territorialRef(unit.id),rank++);
        for(const auto& hydro:doc.hydro)submissionOrder.emplace(ObjectRef{"hydro",hydro.id},rank++);
        for(const auto& feature:doc.genericFeatures)submissionOrder.emplace(ObjectRef{"generic",feature.id},rank++);
        for(const auto& label:doc.labels)submissionOrder.emplace(ObjectRef{"label",label.id},rank++);
        for(const auto& ref:visibleDistributionEntries(doc,selectedLayer))submissionOrder.emplace(ref,rank++);
    }
    std::stable_sort(scene->drawSequence.begin(),scene->drawSequence.end(),
        [&](const auto& a,const auto& b) {
            if(a.order<b.order)return true;
            if(b.order<a.order)return false;
            if(a.layerOrder!=b.layerOrder)return a.layerOrder<b.layerOrder;
            if(!changed)return false;
            const auto rank=[&](const SceneDrawRef& draw) {
                if(draw.primitive==PrimitiveKind::WorldFill||draw.primitive==PrimitiveKind::WorldStroke)
                    return std::pair<int,std::size_t>{0,draw.index};
                const ObjectRef* object=nullptr;
                if(draw.primitive==PrimitiveKind::Polygon)object=&scene->polygons.at(draw.index).object;
                else if(draw.primitive==PrimitiveKind::Stroke)object=&scene->strokes.at(draw.index).object;
                else object=&scene->points.at(draw.index).object;
                const auto found=submissionOrder.find(*object);
                return std::pair<int,std::size_t>{1,found==submissionOrder.end()?submissionOrder.size():found->second};
            };
            return rank(a)<rank(b);
        });
    std::uint64_t geometry=basis,presentation=basis,selection=basis,dataset=basis;
    mix(geometry,static_cast<std::uint64_t>(quality_.backgroundLod));
    for(const auto& command:scene->drawSequence) {
        const auto& order=command.order;
        const auto addPacket=[&](const auto& packet) {
            mix(geometry,packet.key);mix(geometry,packet.geometry.id);
            mix(geometry,packet.geometry.version);mix(geometry,static_cast<std::uint64_t>(command.primitive));
            mix(presentation,packet.key);mix(presentation,packet.style.color);
            mixDouble(presentation,packet.style.alpha);mixDouble(presentation,packet.style.fillAlpha);
            mixDouble(presentation,packet.style.width);
            mix(presentation,static_cast<std::uint64_t>(packet.style.blendMode));
            mix(presentation,order.pass);mix(presentation,order.group);mixDouble(presentation,order.object);
            mix(presentation,command.layerOrder);
        };
        if(command.primitive==PrimitiveKind::Polygon)addPacket(scene->polygons[command.index]);
        else if(command.primitive==PrimitiveKind::Stroke)addPacket(scene->strokes[command.index]);
        else if(command.primitive==PrimitiveKind::Point) {
            const auto& point=scene->points[command.index];addPacket(point);
            mix(presentation,point.labelText);mix(presentation,point.pinned);
            if(point.manualPosition) {
                mixDouble(presentation,point.manualPosition->x);
                mixDouble(presentation,point.manualPosition->y);
            }
        } else if(command.index<scene->worldCountries.size()) {
            const auto& world=scene->worldCountries[command.index];
            const auto& style=command.primitive==PrimitiveKind::WorldFill?world.fill:world.boundary;
            mix(geometry,world.id);mix(geometry,static_cast<std::uint64_t>(command.primitive));
            mix(presentation,world.id);mix(presentation,style.color);
            mixDouble(presentation,style.alpha);mixDouble(presentation,style.width);
            mix(presentation,order.pass);mix(presentation,order.group);
            mixDouble(presentation,order.object);mix(presentation,command.layerOrder);
        }
    }
    for(const auto& ref:interaction.candidates){mix(selection,ref.domain);mix(selection,ref.id);}
    for(const auto& ref:interaction.selected){mix(selection,ref.domain);mix(selection,ref.id);}
    for(const auto* ref:{&interaction.primary,&interaction.hover,&interaction.editTarget})if(*ref){
        mix(selection,(*ref)->domain);mix(selection,(*ref)->id);
    }
    mix(dataset,doc.documentId);
    if(worldBase_&&worldBase_->mesh) {
        mix(dataset,worldBase_->mesh->preview?1:2);
        mix(dataset,std::uint64_t(reinterpret_cast<std::uintptr_t>(worldBase_->mesh.get())));
        for(const auto& range:worldBase_->ranges) {
            mix(dataset,range.sourceId);mix(dataset,range.ownerId);mix(dataset,range.geometryId);
        }
    }
    mix(dataset,doc.physicalData.dataset);mix(dataset,doc.physicalData.version);
    mix(dataset,doc.physicalData.source);
    scene->geometrySignature=geometry;scene->presentationSignature=presentation;
    scene->interactionSignature=selection;scene->datasetSignature=dataset;
    if(previous&&previous->revisions.document==documentRevision&&
       previous->geometrySignature==geometry&&previous->presentationSignature==presentation&&
       previous->interactionSignature==selection&&previous->datasetSignature==dataset&&
       previous->revisions.view==view.revision)return previous;
    scene->revision=nextSceneRevision(previous);
    scene->revisions.document=documentRevision;
    scene->revisions.geometry=previous?
        (previous->geometrySignature!=geometry?advance(previous->revisions.geometry):previous->revisions.geometry):1;
    scene->revisions.presentation=previous?
        (previous->presentationSignature!=presentation?advance(previous->revisions.presentation):previous->revisions.presentation):1;
    scene->revisions.selection=previous?
        (previous->interactionSignature!=selection?advance(previous->revisions.selection):previous->revisions.selection):1;
    scene->revisions.dataset=previous?
        (previous->datasetSignature!=dataset?advance(previous->revisions.dataset):previous->revisions.dataset):1;
    scene->revisions.view=view.revision;
    return scene;
}
