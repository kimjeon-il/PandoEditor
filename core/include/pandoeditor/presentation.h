#pragma once
#include <map>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace pandoeditor {
struct ProjectDocument;
struct Point { double x, y; };
struct ObjectRef {
    std::string domain, id;
    bool operator<(const ObjectRef& b) const { return std::tie(domain,id)<std::tie(b.domain,b.id); }
    bool operator==(const ObjectRef& b) const { return domain==b.domain && id==b.id; }
    bool operator!=(const ObjectRef& b) const { return !(*this==b); }
};
enum class UnitKind;
struct PresentationStyle {
    std::optional<double> opacity;
    std::optional<bool> boundaryVisible, labelsVisible;
    std::optional<double> boundaryWidth;
    std::optional<std::string> blendMode;
    std::optional<bool> colorVisible;
    bool operator==(const PresentationStyle& b) const {
        return opacity==b.opacity && boundaryVisible==b.boundaryVisible && labelsVisible==b.labelsVisible && boundaryWidth==b.boundaryWidth && blendMode==b.blendMode && colorVisible==b.colorVisible;
    }
};
struct LabelSettings {
    std::optional<double> priority, minZoom, maxZoom;
    std::optional<Point> manualPosition;
    bool pinned=false;
    std::string collisionGroup="map";
    bool operator==(const LabelSettings& b) const;
};
enum class DistributionRenderMode { Overlap, Single };
struct DistributionSettings {
    DistributionRenderMode renderMode=DistributionRenderMode::Overlap;
    std::string activeLayerId;
    bool boundaryVisible=true;
    bool operator==(const DistributionSettings& b) const { return renderMode==b.renderMode&&activeLayerId==b.activeLayerId&&boundaryVisible==b.boundaryVisible; }
};
struct DistributionValueScale {
    bool manual=false;
    double min=0,max=1;
    bool operator==(const DistributionValueScale& b) const { return manual==b.manual&&(!manual||(min==b.min&&max==b.max)); }
};
struct DistributionValueRange { double min,max; };
struct WebPresentation {
    std::map<std::string,bool> visibility;
    std::map<std::string,std::set<std::string>> hiddenItems;
    std::map<std::string,PresentationStyle> styles, objectStyles;
    std::vector<std::string> objectOrder;
    std::map<ObjectRef,LabelSettings> labelSettings;
    DistributionSettings distributionSettings;
    bool operator==(const WebPresentation& b) const {
        return visibility==b.visibility && hiddenItems==b.hiddenItems && styles==b.styles && objectStyles==b.objectStyles && objectOrder==b.objectOrder && labelSettings==b.labelSettings && distributionSettings==b.distributionSettings;
    }
};
struct LabelLayoutCandidate {
    ObjectRef ref;
    std::string key, collisionGroup="map";
    double x=0,y=0,width=1,height=1,priority=0,minZoom=0,maxZoom=std::numeric_limits<double>::infinity();
    bool selected=false,pinned=false;
};
struct LabelLayoutBounds { double left=0,top=0,right=0,bottom=0; };
LabelSettings automaticLabelSettings(const std::string& kind,const LabelSettings& stored={});
std::vector<ObjectRef> layoutLabels(const std::vector<LabelLayoutCandidate>&,double zoom,double padding,
                                    std::optional<LabelLayoutBounds> bounds={});
std::vector<ObjectRef> visibleDistributionEntries(const ProjectDocument&);
std::optional<DistributionValueRange> distributionValueRange(const ProjectDocument&,const std::string& layerId);
double distributionValueAlpha(double value,const std::optional<DistributionValueRange>&,double resolvedOpacity=1);
struct ResolvedTerritorialPresentation {
    double opacity=1, effectiveAlpha=1;
    bool boundaryVisible=true, nameVisible=true, flagVisible=true, colorVisible=true;
    std::string blendMode="normal";
};
std::string territorialGroup(UnitKind);
std::string territorialPresentationKey(UnitKind,const std::string&);
bool groupVisible(const WebPresentation&,const std::string&);
bool itemVisible(const WebPresentation&,const std::string&,const std::string&);
bool effectiveMapVisibility(const ProjectDocument&,const ObjectRef&);
ResolvedTerritorialPresentation resolvedTerritorialPresentation(const ProjectDocument&,const ObjectRef&,double terrainAlpha=1);
double territorialRenderOrder(const ProjectDocument&,const ObjectRef&,double primitiveOffset=10);
void normalizePresentation(ProjectDocument&);
void validatePresentation(const ProjectDocument&);
// Preserve current display edits while applying only a content command's
// object-lifetime changes (including reverse changes during Undo).
WebPresentation rebasePresentation(const ProjectDocument& current,const ProjectDocument& from,const ProjectDocument& to);
}
