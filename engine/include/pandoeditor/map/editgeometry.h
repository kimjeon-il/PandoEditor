#pragma once
#include <pandoeditor/geometry-types.h>
#include <pandoeditor/map/mapcamera.h>
#include <optional>

namespace sharedboundary { class Session; }
namespace pandoeditor::map {
struct EditVertexIndex { int polygon=-1,ring=-1,vertex=-1; };
struct EditSegmentHit { EditVertexIndex index;Point coordinate; };
struct EditTranslation { Geometry geometry;bool moved=false; };

// Preserve the legacy shape-specific metric: polygon hits use geographic
// distances, while non-area and boundary hits use overlay distances. Ties go
// to the last eligible entry. Non-area no-hit indices retain ring=0.
EditVertexIndex nearestEditVertex(const Geometry&,Point mapPoint,double mapTolerance,
                                  const MapCameraMetrics&);
std::optional<EditSegmentHit> nearestEditSegment(const Geometry&,Point mapPoint,
                                               double mapTolerance,const MapCameraMetrics&);
int nearestMovableBoundaryNode(const sharedboundary::Session&,Point mapPoint,
                               double mapTolerance,const MapCameraMetrics&);
bool editGeometryContainsPoint(const Geometry&,Point geographic);
// Always derive the proposal from dragOrigin and reject invalid members
// atomically. A zero computed geographic delta is a valid unmoved proposal.
std::optional<EditTranslation> translateEditGeometry(const Geometry& dragOrigin,
                                                    Point mapDelta,const MapCameraMetrics&);
bool validateEditGeometry(const Geometry&,std::string* detail=nullptr);
}
