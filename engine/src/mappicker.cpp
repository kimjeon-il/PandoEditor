#include <pandoeditor/map/mappicker.h>
#include <pandoeditor/map/projectionengine.h>
#include <pandoeditor/maprenderorder.h>
#include <pandoeditor/objectproperties.h>
#include <pandoeditor/picking.h>
#include <pandoeditor/presentation.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

using namespace pandoeditor;

namespace {
constexpr double Pi=3.1415926535897932384626433832795;
constexpr double DegreesPerRadian=180.0/Pi;

double segmentDistance(Point p,Point a,Point b) {
    const double dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;
    const double t=length>0?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/length,0.,1.):0.;
    return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);
}

Point mapToGeographic(double x,double y,const MapCameraMetrics& metrics) {
    return {(x+metrics.minX)/metrics.cosLatitude,metrics.maxLatitude-y};
}

Point geographicToMap(Point point,const MapCameraMetrics& metrics) {
    return {point.x*metrics.cosLatitude-metrics.minX,metrics.maxLatitude-point.y};
}

std::optional<std::string> selectedDistributionLayer(
    const ProjectDocument& document,const std::optional<ObjectRef>& primary) {
    if(!primary)return std::nullopt;
    if(primary->domain=="distributionLayer")return primary->id;
    if(primary->domain=="distributionEntry")
        for(const auto& entry:document.distributionEntries)
            if(entry.id==primary->id)return entry.layerId;
    return std::nullopt;
}

bool externalVisible(const ProjectDocument& document,const MapExternalHydroPickFeature& feature) {
    if(!feature.feature||feature.ref.domain!="hydroBuiltin")return false;
    if(std::find(document.physicalData.hiddenHydroIds.begin(),
                 document.physicalData.hiddenHydroIds.end(),feature.ref.id)!=
       document.physicalData.hiddenHydroIds.end())return false;
    const auto group=feature.category=="lake"?"lakes":"rivers";
    return groupVisible(document.presentation.webPresentation,group);
}

std::string documentName(const std::map<ObjectRef,ObjectPropertyView>& views,
                         const ObjectRef& ref) {
    const auto found=views.find(ref);
    return found==views.end()?ref.id:found->second.displayName;
}

int layerOrder(const ProjectDocument& document,const ObjectRef& ref) {
    const auto layerId=nativeLayerId(document,ref);
    for(std::size_t i=0;i<document.presentation.userLayers.size();++i)
        if(document.presentation.userLayers[i].id==layerId)return static_cast<int>(i);
    return -1;
}

std::size_t projectedPathOrder(const ProjectDocument& document,const DocumentIndex& index,
                               const ObjectRef& ref) {
    if(ref.domain=="territorial")
        for(std::size_t i=0;i<document.units.size();++i)
            if(document.units[i].id==ref.id)return i;
    std::size_t order=document.units.size();
    for(const auto& [candidate,unused]:index.objects) {
        (void)unused;
        if(candidate.domain=="territorial")continue;
        if(!objectGeometry(document,index,candidate))continue;
        if(candidate==ref)return order;
        ++order;
    }
    return 0;
}

std::optional<ObjectRef> normalizedDistributionRef(const ProjectDocument& document,ObjectRef ref) {
    if(ref.domain!="distributionEntry")return ref;
    for(const auto& entry:document.distributionEntries)
        if(entry.id==ref.id)return ObjectRef{"distributionLayer",entry.layerId};
    return std::nullopt;
}
}

void MapPicker::reset() {
    spatialIndex_=GeoSpatialIndex{};
    instanceId_.clear();
    indexedRevision_=0;
}

void MapPicker::ensureIndex(const ProjectSnapshot& snapshot) {
    if(instanceId_!=snapshot.instanceId()) {
        spatialIndex_=GeoSpatialIndex{};
        instanceId_=snapshot.instanceId();
        indexedRevision_=0;
    }
    if(spatialIndex_.geometryRevision()==0||indexedRevision_!=snapshot.revision()) {
        spatialIndex_.rebuild(snapshot.document(),snapshot.index());
        indexedRevision_=snapshot.revision();
    }
}

void MapPicker::applyImpact(const ProjectSnapshot& snapshot,
                            const std::vector<ObjectRef>& changed) {
    if(instanceId_==snapshot.instanceId()&&spatialIndex_.geometryRevision()!=0&&
       indexedRevision_!=std::numeric_limits<std::uint64_t>::max()&&
       indexedRevision_+1==snapshot.revision()) {
        try {
            spatialIndex_.update(snapshot.document(),snapshot.index(),changed,changed);
            indexedRevision_=snapshot.revision();
            if(!changed.empty())++incrementalUpdates_;
            return;
        } catch(...) {
            reset();
            return;
        }
    }
    if(instanceId_!=snapshot.instanceId())reset();
}

std::vector<ObjectRef> MapPicker::pickMap(
    const ProjectSnapshot& snapshot,const MapCameraMetrics& metrics,
    const MapPickMapRequest& request,const MapPickContext& context) {
    if(!validMapCameraMetrics(metrics)||!std::isfinite(request.mapX)||
       !std::isfinite(request.mapY)||!std::isfinite(request.pixelsPerMapUnit)||
       request.pixelsPerMapUnit<0)return {};
    const auto geographic=mapToGeographic(request.mapX,request.mapY,metrics);
    const double pixels=request.pixelsPerMapUnit>0?request.pixelsPerMapUnit:1;
    return pickGeographic(snapshot,metrics,geographic,pixels,context);
}

std::vector<ObjectRef> MapPicker::pickScreen(
    const ProjectSnapshot& snapshot,const MapViewState& view,const MapCameraMetrics& metrics,
    const MapPickScreenRequest& request,const MapPickContext& context) {
    if(!validMapCameraMetrics(metrics)||!validMapViewState(view)||
       !std::isfinite(request.screenX)||!std::isfinite(request.screenY))return {};
    const auto geographic=unprojectView(request.screenX,request.screenY,view);
    if(!geographic)return {};

    double pixelsPerMapUnit=0;
    if(view.mode==ProjectionMode::Flat) {
        pixelsPerMapUnit=view.scale/(metrics.cosLatitude*DegreesPerRadian);
    } else {
        const auto mapPoint=geographicToMap(*geographic,metrics);
        const auto screenPoint=projectPoint(*geographic,view);
        const Point probes[]={{geographic->x+.01,geographic->y},
            {geographic->x,std::clamp(geographic->y+.01,-90.,90.)}};
        for(const auto& probe:probes) {
            const auto screenProbe=projectPoint(probe,view);
            const auto mapProbe=geographicToMap(probe,metrics);
            const double mapDistance=std::hypot(mapProbe.x-mapPoint.x,mapProbe.y-mapPoint.y);
            const double screenDistance=std::hypot(screenProbe.x-screenPoint.x,
                                                   screenProbe.y-screenPoint.y);
            if(screenProbe.finite&&mapDistance>0&&std::isfinite(screenDistance))
                pixelsPerMapUnit=std::max(pixelsPerMapUnit,screenDistance/mapDistance);
        }
    }
    if(!(pixelsPerMapUnit>0)||!std::isfinite(pixelsPerMapUnit))pixelsPerMapUnit=1;
    return pickGeographic(snapshot,metrics,*geographic,pixelsPerMapUnit,context);
}

std::vector<ObjectRef> MapPicker::pickGeographic(
    const ProjectSnapshot& snapshot,const MapCameraMetrics& metrics,
    Point point,double pixelsPerMapUnit,const MapPickContext& context) {
    ensureIndex(snapshot);
    const auto& document=snapshot.document();
    const auto& index=snapshot.index();
    const double xScale=metrics.cosLatitude;
    const double tolerance=(context.mobile?18.:10.)/pixelsPerMapUnit;
    const double boundaryTolerance=(context.mobile?12.:7.)/pixelsPerMapUnit;
    const double latitudeMargin=std::max(tolerance,boundaryTolerance);
    const double longitudeMargin=latitudeMargin/std::abs(xScale);
    const auto spatial=spatialIndex_.queryLegacyFlat({{
        point.x-longitudeMargin,point.y-latitudeMargin,
        point.x+longitudeMargin,point.y+latitudeMargin}});
    const std::set<ObjectRef> spatialCandidates(spatial.begin(),spatial.end());

    std::vector<ObjectRef> found;
    bool countryFound=false;
    auto renderLayers=snapshot.layers();
    renderLayers.insert(renderLayers.begin(),Layer{"",""});
    for(auto layer=renderLayers.rbegin();layer!=renderLayers.rend();++layer) {
        if(!layer->visible)continue;
        for(auto unit=document.units.rbegin();unit!=document.units.rend();++unit) {
            const auto ref=territorialRef(unit->id);
            if(!spatialCandidates.count(ref))continue;
            if(nativeLayerId(document,ref)!=layer->id||!effectiveMapVisibility(document,ref))continue;
            if(unit->kind==UnitKind::Country&&countryFound)continue;
            const auto geometry=document.geometries.get(unit->geometry);
            if(!geometry)continue;
            bool hit=pointInCountry(point,geometry->polygons);
            if(!hit&&unit->kind!=UnitKind::Country) {
                const Point cursor{point.x*xScale,point.y};
                for(const auto& polygon:geometry->polygons)
                    for(const auto& ring:polygon)
                        for(std::size_t i=1;i<ring.size()&&!hit;++i)
                            hit=segmentDistance(cursor,
                                {ring[i-1].x*xScale,ring[i-1].y},
                                {ring[i].x*xScale,ring[i].y})<=boundaryTolerance;
            }
            if(hit) {
                found.push_back(ref);
                if(unit->kind==UnitKind::Country)countryFound=true;
            }
        }
    }

    const Point cursor{point.x*xScale,point.y};
    const auto selectedLayer=selectedDistributionLayer(document,context.primary);
    const auto distributionRows=visibleDistributionEntries(document,selectedLayer);
    const std::set<ObjectRef> displayedDistribution(distributionRows.begin(),distributionRows.end());
    for(const auto& [ref,unused]:index.objects) {
        (void)unused;
        if(!spatialCandidates.count(ref)||ref.domain=="territorial"||
           !effectiveMapVisibility(document,ref))continue;
        if(ref.domain=="label"&&!context.placedLabels.count(ref))continue;
        if(ref.domain=="distributionEntry"&&!displayedDistribution.count(ref))continue;
        const auto geometryRef=objectGeometry(document,index,ref);
        if(!geometryRef)continue;
        const auto geometry=document.geometries.get(*geometryRef);
        if(!geometry)continue;
        bool hit=!geometry->polygons.empty()&&pointInCountry(point,geometry->polygons);
        for(const auto p:geometry->points)
            hit=hit||std::hypot(cursor.x-p.x*xScale,cursor.y-p.y)<=tolerance;
        for(const auto& line:geometry->lines)
            for(std::size_t i=1;i<line.size();++i)
                hit=hit||segmentDistance(cursor,
                    {line[i-1].x*xScale,line[i-1].y},
                    {line[i].x*xScale,line[i].y})<=tolerance;
        if(hit)found.push_back(ref);
    }

    std::set<ObjectRef> seenExternal;
    std::map<ObjectRef,std::string> externalNames;
    for(const auto& external:context.externalHydro) {
        if(!externalVisible(document,external)||!seenExternal.insert(external.ref).second)continue;
        externalNames[external.ref]=external.displayName.empty()?external.ref.id:external.displayName;
        if(point.x<external.bounds[0]-tolerance/xScale||
           point.x>external.bounds[2]+tolerance/xScale||
           point.y<external.bounds[1]-tolerance||
           point.y>external.bounds[3]+tolerance)continue;
        bool hit=false;
        const auto& geometry=external.feature->geometry;
        for(const auto& polygon:geometry.polygons) {
            MultiPolygon rings(1);
            for(const auto& sourceRing:polygon) {
                Ring ring;ring.reserve(sourceRing.size());
                for(const auto p:sourceRing)
                    ring.push_back({p.longitude*1e-6,p.latitude*1e-6});
                rings.front().push_back(std::move(ring));
            }
            if(pointInCountry(point,rings)){hit=true;break;}
        }
        for(const auto& line:geometry.lines)
            for(std::size_t i=1;i<line.size()&&!hit;++i) {
                const Point a{line[i-1].longitude*1e-6*xScale,line[i-1].latitude*1e-6};
                const Point b{line[i].longitude*1e-6*xScale,line[i].latitude*1e-6};
                hit=segmentDistance(cursor,a,b)<=tolerance;
            }
        if(hit)found.push_back(external.ref);
    }

    const auto views=objectPropertyViews(document);
    std::stable_sort(found.begin(),found.end(),[&](const ObjectRef& a,const ObjectRef& b) {
        const int leftRank=a.domain=="hydroBuiltin"?mapBuiltinHydroPickOrder():mapPickOrder(document,a);
        const int rightRank=b.domain=="hydroBuiltin"?mapBuiltinHydroPickOrder():mapPickOrder(document,b);
        if(leftRank!=rightRank)return leftRank>rightRank;
        const auto leftName=a.domain=="hydroBuiltin"?
            (externalNames.count(a)?externalNames.at(a):a.id):documentName(views,a);
        const auto rightName=b.domain=="hydroBuiltin"?
            (externalNames.count(b)?externalNames.at(b):b.id):documentName(views,b);
        return leftName<rightName;
    });
    return found;
}

std::optional<ObjectRef> MapPicker::topCandidate(
    const ProjectSnapshot& snapshot,const std::vector<ObjectRef>& hits) const {
    const auto& document=snapshot.document();
    const auto& index=snapshot.index();
    std::optional<ObjectRef> top;
    int topLayer=std::numeric_limits<int>::min();
    double topRank=-std::numeric_limits<double>::infinity();
    std::size_t topSequence=0;

    for(const auto& ref:hits) {
        if(ref.domain=="hydroBuiltin")continue;
        if(!index.objects.count(ref))continue;
        const auto geometry=objectGeometry(document,index,ref);
        if(!geometry)continue;
        const int layer=layerOrder(document,ref);
        const auto order=mapRenderOrder(document,ref,RenderPrimitiveRole::Fill);
        const double rank=order.pass+order.object;
        const auto sequence=projectedPathOrder(document,index,ref);
        if(!top||layer>topLayer||
           (layer==topLayer&&(rank>topRank||(rank==topRank&&sequence>=topSequence)))) {
            top=ref;topLayer=layer;topRank=rank;topSequence=sequence;
        }
    }

    for(const auto& ref:hits)if(ref.domain=="hydroBuiltin"&&
        (!top||mapBuiltinHydroPickOrder()>mapPickOrder(document,*top))) {
        top=ref;break;
    }
    if(!top)return std::nullopt;
    return normalizedDistributionRef(document,*top);
}

std::vector<ObjectRef> MapPicker::normalizeSelectionCandidates(
    const ProjectSnapshot& snapshot,std::vector<ObjectRef> refs) const {
    const auto& document=snapshot.document();
    for(auto& ref:refs) {
        if(const auto normalized=normalizedDistributionRef(document,ref))ref=*normalized;
    }
    std::set<ObjectRef> unique;
    refs.erase(std::remove_if(refs.begin(),refs.end(),
        [&](const auto& ref){return !unique.insert(ref).second;}),refs.end());
    return refs;
}
