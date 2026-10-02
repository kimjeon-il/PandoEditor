#include <pandoeditor/map/scenepatch.h>
#include <optional>

ScenePatchStats appendUnchangedScenePackets(RenderScene& target,const RenderScene& previous,
                                             const std::set<pandoeditor::ObjectRef>& changed) {
    ScenePatchStats stats;
    std::vector<std::optional<std::size_t>> polygons(previous.polygons.size());
    std::vector<std::optional<std::size_t>> strokes(previous.strokes.size());
    std::vector<std::optional<std::size_t>> points(previous.points.size());
    for(std::size_t i=0;i<previous.polygons.size();++i)
        if(previous.polygons[i].object.domain!="hydroBuiltin"&&
           !changed.count(previous.polygons[i].object)) {
            polygons[i]=target.polygons.size();target.polygons.push_back(previous.polygons[i]);++stats.retainedPolygons;
        }else ++stats.removedPackets;
    for(std::size_t i=0;i<previous.strokes.size();++i)
        if(previous.strokes[i].object.domain!="hydroBuiltin"&&
           !changed.count(previous.strokes[i].object)) {
            strokes[i]=target.strokes.size();target.strokes.push_back(previous.strokes[i]);++stats.retainedStrokes;
        }else ++stats.removedPackets;
    for(std::size_t i=0;i<previous.points.size();++i)
        if(previous.points[i].object.domain!="hydroBuiltin"&&
           !changed.count(previous.points[i].object)) {
            points[i]=target.points.size();target.points.push_back(previous.points[i]);++stats.retainedPoints;
        }else ++stats.removedPackets;
    for(auto draw:previous.drawSequence) {
        const std::vector<std::optional<std::size_t>>* lookup=nullptr;
        if(draw.primitive==PrimitiveKind::Polygon)lookup=&polygons;
        else if(draw.primitive==PrimitiveKind::Stroke)lookup=&strokes;
        else if(draw.primitive==PrimitiveKind::Point)lookup=&points;
        if(lookup&&draw.index<lookup->size()&&(*lookup)[draw.index]) {
            draw.index=*(*lookup)[draw.index];target.drawSequence.push_back(std::move(draw));
        }
        // World-base commands were constructed from the current document.
    }
    return stats;
}
