#include <pandoeditor/map/mapscenebuilder.h>
#include <pandoeditor/map/scenepatch.h>
#include <pandoeditor/map/countryculling.h>
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
RenderStyle builtinHydroStyle(const ProjectDocument& doc,const std::string& group,
                              std::uint32_t color,float width) {
    RenderStyle style;style.color=color;style.width=width;
    const auto found=doc.presentation.webPresentation.styles.find(group);
    if(found!=doc.presentation.webPresentation.styles.end()) {
        style.alpha=static_cast<float>(found->second.opacity.value_or(1));
        style.blendMode=blend(found->second.blendMode.value_or("normal"));
    }
    return style;
}
bool builtinHydroHidden(const ProjectDocument& doc,const BuiltinHydroFeaturePacket& feature) {
    const auto& hidden=doc.physicalData.hiddenHydroIds;
    return std::find(hidden.begin(),hidden.end(),feature.object.id)!=hidden.end()||
        std::find(hidden.begin(),hidden.end(),std::to_string(feature.fid))!=hidden.end()||
        std::find(hidden.begin(),hidden.end(),std::to_string(feature.logicalFid))!=hidden.end();
}
RenderStyle styleFor(const ProjectDocument& doc,const ObjectRef& ref,
                     std::uint32_t defaultColor,double defaultOpacity=1) {
    RenderStyle style;style.color=defaultColor;
    const auto object=doc.presentation.objectStyles.find(ref);
    if(object!=doc.presentation.objectStyles.end()) {
        style.color=object->second.color;defaultOpacity*=object->second.opacity;
    }
    const auto group=ref.domain=="territorial"?[&]() {
        for(const auto& u:doc.units)if(u.id==ref.id)return territorialGroup(doc,u.id);
        return std::string{};
    }():contentGroup(doc,ref);
    if(ref.domain=="territorial") {
        const auto resolved=resolvedTerritorialPresentation(doc,ref);
        if(!resolved.colorVisible)style.color=0xa8c7db;
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
float layerOpacityFor(const ProjectDocument& doc,const ObjectRef& ref) {
    const auto membership=doc.presentation.membership.find(ref);
    if(membership==doc.presentation.membership.end())return 1;
    for(const auto& layer:doc.presentation.userLayers)
        if(layer.id==membership->second)return static_cast<float>(std::clamp(layer.opacity,0.,1.));
    return 1;
}
std::optional<GeometryRef> geometryFor(const ProjectDocument& doc,const ObjectRef& ref) {
    if(ref.domain=="territorial") {
        for(const auto& unit:doc.units)if(unit.id==ref.id)return staticGeometryBinding(doc,unit.id).geometryRef;
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
bool sameInteraction(const InteractionRenderPacket& a,const InteractionRenderPacket& b) {
    return a.candidates==b.candidates&&a.selected==b.selected&&a.primary==b.primary&&
        a.hover==b.hover&&a.editTarget==b.editTarget;
}
std::uint64_t interactionSignature(const InteractionRenderPacket& interaction) {
    std::uint64_t selection=basis;
    for(const auto& ref:interaction.candidates){mix(selection,ref.domain);mix(selection,ref.id);}
    for(const auto& ref:interaction.selected){mix(selection,ref.domain);mix(selection,ref.id);}
    for(const auto* ref:{&interaction.primary,&interaction.hover,&interaction.editTarget})if(*ref){
        mix(selection,(*ref)->domain);mix(selection,(*ref)->id);
    }
    return selection;
}
}

void MapSceneBuilder::remember(const ProjectSnapshot& snapshot,const MapViewState& view,
                              const std::shared_ptr<const RenderScene>& scene) {
    cache_.setActiveScene(scene);
    preparedSnapshot_=snapshot;preparedScene_=scene;preparedHydro_=builtinHydro_;
    preparedMode_=view.mode;preparedLod_=quality_.backgroundLod;
}
bool MapSceneBuilder::canReusePreparation(const ProjectSnapshot& snapshot,const MapViewState& view,
                                         const std::shared_ptr<const RenderScene>& previous) const {
    return preparationMatchesView(view,previous)&&preparedSnapshot_&&
        &preparedSnapshot_->document()==&snapshot.document()&&
        preparedSnapshot_->instanceId()==snapshot.instanceId()&&
        preparedSnapshot_->revision()==snapshot.revision();
}
bool MapSceneBuilder::preparationMatchesView(const MapViewState& view,
                                           const std::shared_ptr<const RenderScene>& previous) const {
    return previous&&previous==preparedScene_.lock()&&
        previous->worldBase==worldBase_&&preparedHydro_==builtinHydro_&&
        preparedMode_==view.mode&&preparedLod_==quality_.backgroundLod;
}
std::shared_ptr<const RenderScene> MapSceneBuilder::build(
    const ProjectSnapshot& snapshot,const MapViewState& view,
    const InteractionRenderPacket& interaction,
    const std::shared_ptr<const RenderScene>& previous) {
    if(!validMapViewState(view))throw std::invalid_argument("invalid scene view");
    if(preparedSnapshot_&&preparedSnapshot_->instanceId()!=snapshot.instanceId()) {
        // Geometry ids/versions and revision zero may repeat in a new project.
        cache_.clear();
        auto scene=std::make_shared<RenderScene>(*buildDocument(snapshot.document(),snapshot.revision(),view,interaction,{}));
        scene->revision=nextSceneRevision(previous);
        remember(snapshot,view,scene);return scene;
    }
    bool reusable=canReusePreparation(snapshot,view,previous);
    const bool unchangedInteraction=previous&&sameInteraction(previous->interaction,interaction);
    if(reusable&&!unchangedInteraction) {
        // Selection/editing can promote background fallback geometry to high
        // LOD. Hidden boundaries may also need their first stroke packet.
        const auto protectedBy=[](const InteractionRenderPacket& state,const ObjectRef& ref) {
            return std::find(state.selected.begin(),state.selected.end(),ref)!=state.selected.end()||
                (state.editTarget&&*state.editTarget==ref);
        };
        if(quality_.backgroundLod!=RenderLod::High&&
           std::any_of(snapshot.document().genericFeatures.begin(),snapshot.document().genericFeatures.end(),
                       [&](const auto& feature){const ObjectRef ref{"generic",feature.id};
                           return feature.fallbackOnly&&protectedBy(previous->interaction,ref)!=protectedBy(interaction,ref);
                       }))reusable=false;
        std::set<ObjectRef> highlighted(interaction.selected.begin(),interaction.selected.end());
        highlighted.insert(interaction.candidates.begin(),interaction.candidates.end());
        for(const auto* ref:{&interaction.primary,&interaction.hover,&interaction.editTarget})
            if(*ref)highlighted.insert(**ref);
        for(const auto& polygon:previous->polygons)if(highlighted.count(polygon.object)&&
            std::none_of(previous->strokes.begin(),previous->strokes.end(),
                [&](const auto& stroke){return stroke.object==polygon.object;}))reusable=false;
    }
    if(reusable) {
        if(unchangedInteraction) {
            ++unchanged_;return previous;
        }
        auto scene=std::make_shared<RenderScene>(*previous);
        std::set<ObjectRef> protectedObjects(interaction.selected.begin(),interaction.selected.end());
        if(interaction.editTarget)protectedObjects.insert(*interaction.editTarget);
        cache_.protectReasons(std::set<ObjectRef>(interaction.selected.begin(),interaction.selected.end()),
            interaction.editTarget?std::set<ObjectRef>{*interaction.editTarget}:std::set<ObjectRef>{});
        scene->revision=nextSceneRevision(previous);
        scene->interaction=interaction;scene->interactionSignature=interactionSignature(interaction);
        if(!unchangedInteraction)scene->revisions.selection=advance(previous->revisions.selection);
        scene->revisions.view=view.revision;
        ++transientUpdates_;remember(snapshot,view,scene);return scene;
    }
    auto scene=buildDocument(snapshot.document(),snapshot.revision(),view,interaction,previous);
    remember(snapshot,view,scene);return scene;
}

std::shared_ptr<const RenderScene> MapSceneBuilder::buildDocument(
    const ProjectDocument& doc,std::uint64_t documentRevision,const MapViewState& view,
    const InteractionRenderPacket& interaction,
    const std::shared_ptr<const RenderScene>& previous) {
    return buildDocumentImpl(doc,documentRevision,view,interaction,previous,nullptr);
}

std::shared_ptr<const RenderScene> MapSceneBuilder::refresh(
    const ProjectSnapshot& snapshot,const MapViewState& view,
    const InteractionRenderPacket& interaction,const std::shared_ptr<const RenderScene>& previous,
    const SceneDirtySet* dirty) {
    // The retained baseline also covers presentation-only commands and coalesced commits.
    if(preparedSnapshot_&&preparedSnapshot_->instanceId()==snapshot.instanceId()&&
       &preparedSnapshot_->document()!=&snapshot.document()) {
        auto actual=calculateChangeImpact(preparedSnapshot_->document(),snapshot.document()).sceneDirty;
        if(dirty) {
            actual.fullRebuild|=dirty->fullRebuild;
            actual.datasetResource|=dirty->datasetResource;
            actual.interaction|=dirty->interaction;
        }
        return buildDelta(snapshot,view,interaction,previous,actual);
    }
    return build(snapshot,view,interaction,previous);
}
std::shared_ptr<const RenderScene> MapSceneBuilder::buildDelta(
    const ProjectSnapshot& snapshot,const MapViewState& view,
    const InteractionRenderPacket& interaction,const std::shared_ptr<const RenderScene>& previous,
    const SceneDirtySet& dirty) {
    const bool compatible=preparedSnapshot_&&preparedSnapshot_->instanceId()==snapshot.instanceId()&&
        preparationMatchesView(view,previous)&&previous->revisions.document<=snapshot.revision();
    const auto fallback=[&](const std::optional<ObjectRef>& ref) {
        if(!ref||ref->domain!="generic"||quality_.backgroundLod==RenderLod::High)return false;
        return std::any_of(snapshot.document().genericFeatures.begin(),snapshot.document().genericFeatures.end(),
            [&](const auto& feature){return feature.id==ref->id&&feature.fallbackOnly;});
    };
    const bool sameProtected=previous&&previous->interaction.selected==interaction.selected&&
        (previous->interaction.editTarget==interaction.editTarget||
         (!interaction.editTarget&&!fallback(previous->interaction.editTarget)));
    if(!compatible||dirty.fullRebuild||dirty.datasetResource||!sameProtected)
        return build(snapshot,view,interaction,previous);
    std::set<ObjectRef> changed(dirty.affectedObjects.begin(),dirty.affectedObjects.end());
    std::set<ObjectRef> geometryChanged(dirty.geometryObjects.begin(),dirty.geometryObjects.end());
    // A newly highlighted hidden boundary must still prepare its missing stroke.
    for(const auto* ref:{&interaction.primary,&interaction.hover})if(*ref)changed.insert(**ref);
    changed.insert(interaction.candidates.begin(),interaction.candidates.end());
    auto scene=buildDocumentImpl(snapshot.document(),snapshot.revision(),view,interaction,
                                 previous,&changed,&geometryChanged);
    ++deltaUpdates_;remember(snapshot,view,scene);return scene;
}
std::shared_ptr<const RenderScene> MapSceneBuilder::buildPatch(
    const ProjectSnapshot& snapshot,const MapViewState& view,
    const InteractionRenderPacket& interaction,const std::shared_ptr<const RenderScene>& previous,
    const std::set<ObjectRef>& changed) {
    if(!previous||changed.empty()||previous->worldBase!=worldBase_)
        return build(snapshot,view,interaction,previous);
    if(preparedSnapshot_&&(preparedSnapshot_->instanceId()!=snapshot.instanceId()||
       !preparationMatchesView(view,previous)))return build(snapshot,view,interaction,previous);
    auto expanded=changed;
    // Automatic color scaling is shared by all entries in a layer. A changed
    // extremum (or a removed active layer) can restyle unchanged geometries.
    const bool distributionChanged=std::any_of(changed.begin(),changed.end(),[](const auto& ref){return ref.domain=="distributionEntry"||ref.domain=="distributionLayer";});
    if(distributionChanged) {
        for(const auto& entry:snapshot.document().distributionEntries)expanded.insert({"distributionEntry",entry.id});
        for(const auto& packet:previous->polygons)if(packet.object.domain=="distributionEntry")expanded.insert(packet.object);
        for(const auto& packet:previous->strokes)if(packet.object.domain=="distributionEntry")expanded.insert(packet.object);
    }
    std::set<ObjectRef> geometryChanged;
    for(const auto& object:expanded) {
        const auto current=geometryFor(snapshot.document(),object);
        if(preparedSnapshot_) {
            const auto old=geometryFor(preparedSnapshot_->document(),object);
            if(!(old==current)||(old&&current&&preparedSnapshot_->document().geometries.get(*old)!=
                snapshot.document().geometries.get(*current)))geometryChanged.insert(object);
        }else geometryChanged.insert(object);
    }
    auto scene=buildDocumentImpl(snapshot.document(),snapshot.revision(),view,interaction,previous,&expanded,&geometryChanged);
    ++deltaUpdates_;remember(snapshot,view,scene);return scene;
}

std::shared_ptr<const RenderScene> MapSceneBuilder::buildDocumentImpl(
    const ProjectDocument& doc,std::uint64_t documentRevision,const MapViewState& view,
    const InteractionRenderPacket& interaction,
    const std::shared_ptr<const RenderScene>& previous,
    const std::set<ObjectRef>* changed,const std::set<ObjectRef>* geometryChanged) {
    if(!validMapViewState(view))throw std::invalid_argument("invalid scene view");
    bool geometryWork=!changed||!geometryChanged||!geometryChanged->empty();
    if(geometryWork)++preparations_;else ++presentationUpdates_;
    auto scene=std::make_shared<RenderScene>();
    scene->preparationIdentity=geometryWork?std::make_shared<RenderScene::PreparationIdentity>():previous->preparationIdentity;
    const auto markGeometryWork=[&] {
        if(!geometryWork) {
            geometryWork=true;++preparations_;
            scene->preparationIdentity=std::make_shared<RenderScene::PreparationIdentity>();
        }
    };
    scene->interaction=interaction;
    scene->worldBase=worldBase_;
    if(worldBase_&&worldBase_->mesh)
        scene->worldPlan=worldRenderPlanForView(*worldBase_->mesh,view);
    std::set<ObjectRef> protectedObjects(interaction.selected.begin(),interaction.selected.end());
    if(interaction.editTarget)protectedObjects.insert(*interaction.editTarget);
    cache_.protectReasons(std::set<ObjectRef>(interaction.selected.begin(),interaction.selected.end()),
            interaction.editTarget?std::set<ObjectRef>{*interaction.editTarget}:std::set<ObjectRef>{});
    std::set<std::string> baseCountries;
    if(worldBase_&&worldBase_->mesh) {
        if(worldBase_->ranges.size()!=258)throw std::invalid_argument("world base needs 258 ranges");
        if(worldBase_->startupPreview())
            for(const auto& range:worldBase_->ranges)baseCountries.insert(range.ownerId);
        scene->worldCountries.reserve(258);
        for(const auto& range:worldBase_->ranges) {
            WorldCountryDraw base;base.id=range.ownerId;
            base.fill.color=0xa8c7db;base.boundary.color=0x61778a;
            base.boundary.width=1.2f;
            if(!worldBase_->startupPreview()) {
                const auto unit=std::find_if(doc.units.begin(),doc.units.end(),
                    [&](const auto& value){return value.id==range.ownerId;});
                if(unit==doc.units.end()||!(staticGeometryBinding(doc,unit->id).geometryRef==GeometryRef{range.geometryId,1})) {
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
                        scene->drawSequence.push_back({PrimitiveKind::WorldFill,i,fillOrder,layerOrder,
                            layerOpacityFor(doc,ref)});
                        if(resolvedTerritorialPresentation(doc,ref).boundaryVisible) {
                            const auto edgeOrder=mapRenderOrder(doc,ref,RenderPrimitiveRole::Boundary);
                            scene->drawSequence.push_back({PrimitiveKind::WorldStroke,i,edgeOrder,layerOrder,
                                layerOpacityFor(doc,ref)});
                        }
                    }
                }
            }
            scene->worldCountries.push_back(std::move(base));
        }
    }
    // Prepare physical ownership before paint visibility removes any packets.
    // A removed/replaced/Regional owner must never expose its old base mesh slot.
    auto land=std::make_shared<PhysicalLandMaskPacket>();land->worldBase=worldBase_;
    std::set<std::string> immutableLandOwners;
    if(worldBase_&&worldBase_->mesh) {
        for(std::size_t slot=0;slot<worldBase_->ranges.size();++slot) {
            const auto& range=worldBase_->ranges[slot];
            if(worldBase_->startupPreview()) {land->baseSlots.push_back(slot);continue;}
            const auto unit=std::find_if(doc.units.begin(),doc.units.end(),
                [&](const auto& value){return value.id==range.ownerId;});
            if(unit!=doc.units.end()&&unit->kind==UnitKind::General&&
               staticGeometryBinding(doc,unit->id).geometryRef==GeometryRef{range.geometryId,1}) {
                land->baseSlots.push_back(slot);immutableLandOwners.insert(unit->id);
            }
        }
    }
    for(const auto& unit:doc.units) {
        if(unit.kind!=UnitKind::General||immutableLandOwners.count(unit.id))continue;
        const auto object=territorialRef(unit.id);
        const auto ref=staticGeometryBinding(doc,unit.id).geometryRef;
        const auto shape=doc.geometries.get(ref);
        if(!shape)throw std::invalid_argument("land mask has dangling geometry ref");
        if(!polygon(*shape))continue;
        PolygonDrawPacket packet;packet.key="physical-land:"+unit.id;
        packet.object=object;packet.geometry=ref;packet.geometryRevision=ref.version;
        packet.lod=int(RenderLod::High);packet.preparationPolicy=ProjectionPreparationPolicy::Geographic;
        const PolygonDrawPacket* retained=nullptr;
        if(previous&&previous->physicalLandMask&&
           (!geometryChanged||!geometryChanged->count(object))) {
            const auto& old=previous->physicalLandMask->polygons;
            const auto found=std::find_if(old.begin(),old.end(),[&](const auto& value) {
                return value.object==object&&value.geometry==ref;
            });
            if(found!=old.end())retained=&*found;
        }
        if(retained)packet.geometryPacket=retained->geometryPacket;
        else {
            markGeometryWork();
            packet.geometryPacket=cache_.polygon(object,ref,*shape,packet.lod,packet.preparationPolicy);
        }
        land->polygons.push_back(std::move(packet));
    }
    const auto sameLand=[&] {
        if(!previous||!previous->physicalLandMask)return false;
        const auto& old=*previous->physicalLandMask;
        if(old.worldBase!=land->worldBase||old.baseSlots!=land->baseSlots||
           old.polygons.size()!=land->polygons.size())return false;
        for(std::size_t i=0;i<land->polygons.size();++i) {
            const auto& a=old.polygons[i];const auto& b=land->polygons[i];
            if(a.object!=b.object||!(a.geometry==b.geometry)||
               a.geometryPacket.positions!=b.geometryPacket.positions||
               a.geometryPacket.indices!=b.geometryPacket.indices||
               a.geometryPacket.globeIndices!=b.geometryPacket.globeIndices)return false;
        }
        return true;
    };
    scene->physicalLandMask=sameLand()?previous->physicalLandMask:std::move(land);
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
        const float layerOpacity=layerOpacityFor(doc,object);
        const auto retained=[&](const auto& packets) -> const typename std::decay_t<decltype(packets)>::value_type* {
            if(!changed||!geometryChanged||geometryChanged->count(object))return nullptr;
            const auto found=std::find_if(packets.begin(),packets.end(),[&](const auto& packet) {
                return packet.object==object&&packet.geometry==*ref;
            });
            return found==packets.end()?nullptr:&*found;
        };
        if(polygon(*shape)) {
            PolygonDrawPacket draw;draw.key=key;draw.object=object;draw.geometry=*ref;
            draw.lod=lod;draw.preparationPolicy=preparation;
            draw.geometryRevision=ref->version;draw.style=style;
            draw.drawOrder=mapRenderOrder(doc,object,RenderPrimitiveRole::Fill);
            draw.order=orderValue(draw.drawOrder);
            if(const auto old=previous?retained(previous->polygons):nullptr)draw.geometryPacket=old->geometryPacket;
            else {
                const auto& physical=scene->physicalLandMask->polygons;
                const auto prepared=std::find_if(physical.begin(),physical.end(),[&](const auto& value) {
                    return value.object==object&&value.geometry==*ref;
                });
                if(prepared!=physical.end())draw.geometryPacket=prepared->geometryPacket;
                else {markGeometryWork();draw.geometryPacket=cache_.polygon(object,*ref,*shape,lod,preparation);}
            }
            const auto index=scene->polygons.size();scene->polygons.push_back(std::move(draw));
            scene->drawSequence.push_back({PrimitiveKind::Polygon,index,scene->polygons.back().drawOrder,layerOrder,layerOpacity});
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
                stroke.lod=lod;stroke.preparationPolicy=preparation;
                stroke.geometryRevision=ref->version;stroke.style=style;
                if(object.domain=="territorial")for(const auto& unit:doc.units)
                    if(unit.id==object.id) {
                        stroke.style.color=0x61778a;
                        stroke.style.width=isRootGeneral(doc,unit)?1.2f:
                            unit.kind==UnitKind::General?.9f:.7f;
                        if(unit.kind==UnitKind::General&&!isRootGeneral(doc,unit)){stroke.style.dashOn=4;stroke.style.dashOff=2;}
                        if(unit.kind==UnitKind::Regional){stroke.style.dashOn=1.5f;stroke.style.dashOff=2;}
                        break;
                    }
                stroke.drawOrder=mapRenderOrder(doc,object,RenderPrimitiveRole::Boundary);
                stroke.order=orderValue(stroke.drawOrder);
                if(const auto old=previous?retained(previous->strokes):nullptr)stroke.geometryPacket=old->geometryPacket;
                else {markGeometryWork();stroke.geometryPacket=cache_.stroke(object,*ref,*shape,lod,preparation);}
                const auto strokeIndex=scene->strokes.size();scene->strokes.push_back(std::move(stroke));
                if(boundary)scene->drawSequence.push_back({PrimitiveKind::Stroke,strokeIndex,
                    scene->strokes.back().drawOrder,layerOrder,layerOpacity});
            }
        } else if(line(*shape)) {
            StrokeDrawPacket draw;draw.key=key;draw.object=object;draw.geometry=*ref;
            draw.lod=lod;draw.preparationPolicy=preparation;
            draw.geometryRevision=ref->version;draw.style=style;
            draw.drawOrder=mapRenderOrder(doc,object,RenderPrimitiveRole::Line);
            draw.order=orderValue(draw.drawOrder);
            if(const auto old=previous?retained(previous->strokes):nullptr)draw.geometryPacket=old->geometryPacket;
            else {markGeometryWork();draw.geometryPacket=cache_.stroke(object,*ref,*shape,lod,preparation);}
            const auto index=scene->strokes.size();scene->strokes.push_back(std::move(draw));
            scene->drawSequence.push_back({PrimitiveKind::Stroke,index,scene->strokes.back().drawOrder,layerOrder,layerOpacity});
        } else if(shape->type=="Point"||shape->type=="MultiPoint") {
            PointDrawPacket draw;draw.key=key;draw.object=object;draw.geometry=*ref;
            draw.lod=lod;draw.preparationPolicy=preparation;
            draw.geometryRevision=ref->version;draw.style=style;
            draw.drawOrder=mapRenderOrder(doc,object,
                object.domain=="label"?RenderPrimitiveRole::Label:RenderPrimitiveRole::Point);
            draw.order=orderValue(draw.drawOrder);
            if(const auto old=previous?retained(previous->points):nullptr)draw.geometryPacket=old->geometryPacket;
            else {markGeometryWork();draw.geometryPacket=cache_.point(object,*ref,*shape,lod,preparation);}
            if(object.domain=="label") {
                for(const auto& label:doc.labels)if(label.id==object.id){draw.labelText=label.name;break;}
                if(const auto it=doc.presentation.webPresentation.labelSettings.find(object);
                   it!=doc.presentation.webPresentation.labelSettings.end()) {
                    draw.pinned=it->second.pinned;draw.manualPosition=it->second.manualPosition;
                }
            }
            const auto index=scene->points.size();scene->points.push_back(std::move(draw));
            scene->drawSequence.push_back({PrimitiveKind::Point,index,scene->points.back().drawOrder,layerOrder,layerOpacity});
        }
    };
    for(const auto& unit:doc.units)
        if(!baseCountries.count(unit.id))add(territorialRef(unit.id),0xa8c7db);
    for(const auto& hydro:doc.hydro)add({"hydro",hydro.id},hydro.color);
    for(const auto& feature:doc.genericFeatures)add({"generic",feature.id},feature.color);
    for(const auto& label:doc.labels)add({"label",label.id},0x222222);
    std::map<std::string,const DistributionEntry*> distributionEntries;
    std::map<std::string,std::pair<std::uint32_t,std::optional<DistributionValueRange>>> distributionLayers;
    for(const auto& entry:doc.distributionEntries)distributionEntries.emplace(entry.id,&entry);
    for(const auto& layer:doc.distributionLayers)
        distributionLayers.emplace(layer.id,std::make_pair(layer.color,distributionValueRange(doc,layer.id)));
    for(const auto& ref:visibleDistributionEntries(doc)) {
        const auto entry=distributionEntries.at(ref.id);
        const auto& layer=distributionLayers.at(entry->layerId);
        add(ref,layer.first,distributionValueAlpha(entry->value,layer.second));
    }
    if(builtinHydro_) {
        const auto& presentation=doc.presentation.webPresentation;
        for(const auto& feature:builtinHydro_->features) {
            if(builtinHydroHidden(doc,feature))continue;
            const bool lake=feature.category=="lake";
            const auto group=lake?std::string("lakes"):std::string("rivers");
            if(!groupVisible(presentation,group))continue;
            const GeometryRef geometry{"builtin-hydro:"+std::to_string(feature.fid),1};
            const auto key="hydroBuiltin:"+feature.object.id+":"+std::to_string(feature.fid);
            if(lake) {
                PolygonDrawPacket fill;
                fill.key=key+"/fill";fill.object=feature.object;fill.geometry=geometry;
                fill.geometryRevision=builtinHydro_->revision;
                fill.style=builtinHydroStyle(doc,group,0x82bfd7,0);
                fill.drawOrder=mapBuiltinHydroRenderOrder("lake",RenderPrimitiveRole::Fill);
                fill.order=orderValue(fill.drawOrder);fill.geometryPacket=feature.polygon;
                const auto fillIndex=scene->polygons.size();
                scene->polygons.push_back(std::move(fill));
                scene->drawSequence.push_back({PrimitiveKind::Polygon,fillIndex,
                    scene->polygons.back().drawOrder,-1});

                StrokeDrawPacket boundary;
                boundary.key=key+"/boundary";boundary.object=feature.object;boundary.geometry=geometry;
                boundary.geometryRevision=builtinHydro_->revision;
                boundary.style=builtinHydroStyle(doc,group,0x5f9cba,.8f);
                boundary.drawOrder=mapBuiltinHydroRenderOrder("lake",RenderPrimitiveRole::Boundary);
                boundary.order=orderValue(boundary.drawOrder);boundary.geometryPacket=feature.stroke;
                const auto boundaryIndex=scene->strokes.size();
                scene->strokes.push_back(std::move(boundary));
                const auto groupStyle=presentation.styles.find(group);
                const bool boundaryVisible=groupStyle==presentation.styles.end()?true:
                    groupStyle->second.boundaryVisible.value_or(true);
                if(boundaryVisible)
                    scene->drawSequence.push_back({PrimitiveKind::Stroke,boundaryIndex,
                        scene->strokes.back().drawOrder,-1});
            } else {
                StrokeDrawPacket river;
                river.key=key+(feature.borderAligned?"/border":"/river");
                river.object=feature.object;river.geometry=geometry;
                river.geometryRevision=builtinHydro_->revision;
                // Variable widths are absolute endpoint pixels. A zero style
                // width leaves the base width unchanged; interaction passes add
                // their requested outline width in the GPU backend.
                river.style=builtinHydroStyle(doc,group,0x4b9cc6,0);
                river.drawOrder=mapBuiltinHydroRenderOrder(
                    "river",RenderPrimitiveRole::Line,feature.borderAligned);
                river.order=orderValue(river.drawOrder);river.geometryPacket=feature.stroke;
                const auto riverIndex=scene->strokes.size();
                scene->strokes.push_back(std::move(river));
                scene->drawSequence.push_back({PrimitiveKind::Stroke,riverIndex,
                    scene->strokes.back().drawOrder,-1});
            }
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
        for(const auto& ref:visibleDistributionEntries(doc))submissionOrder.emplace(ref,rank++);
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
    // Hidden geometry and classification still change the physical scene.
    // Keep this reproducible across independent full/delta preparations; the
    // shared packet address separately authenticates retained render resources.
    mix(geometry,std::string("physical-land"));
    mix(geometry,scene->physicalLandMask->baseSlots.size());
    for(const auto slot:scene->physicalLandMask->baseSlots)mix(geometry,slot);
    mix(geometry,scene->physicalLandMask->polygons.size());
    for(const auto& landPolygon:scene->physicalLandMask->polygons) {
        mix(geometry,landPolygon.object.domain);mix(geometry,landPolygon.object.id);
        mix(geometry,landPolygon.geometry.id);mix(geometry,landPolygon.geometry.version);
    }
    mix(geometry,static_cast<std::uint64_t>(quality_.backgroundLod));
    mix(geometry,static_cast<std::uint64_t>(view.mode));
    // Immutable resource identity remains significant when numeric revisions coincide.
    mix(dataset,std::uint64_t(reinterpret_cast<std::uintptr_t>(worldBase_.get())));
    mix(dataset,std::uint64_t(reinterpret_cast<std::uintptr_t>(builtinHydro_.get())));
    if(builtinHydro_) {
        mix(geometry,builtinHydro_->revision);
        mix(dataset,builtinHydro_->revision);
    }
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
            mix(presentation,command.layerOrder);mixDouble(presentation,command.layerOpacity);
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
            mixDouble(presentation,command.layerOpacity);
        }
    }
    selection=interactionSignature(interaction);
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
       previous->revisions.view==view.revision){cache_.setActiveScene(previous);return previous;}
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
    cache_.setActiveScene(scene);
    return scene;
}
