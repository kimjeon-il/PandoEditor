#pragma once
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace pandoeditor {
struct ProjectDocument;
struct ObjectRef;
enum class UnitKind;
struct PresentationStyle {
    std::optional<double> opacity;
    std::optional<bool> boundaryVisible, labelsVisible;
    std::optional<double> boundaryWidth;
    std::optional<std::string> blendMode;
    bool operator==(const PresentationStyle& b) const {
        return opacity==b.opacity && boundaryVisible==b.boundaryVisible && labelsVisible==b.labelsVisible && boundaryWidth==b.boundaryWidth && blendMode==b.blendMode;
    }
};
struct WebPresentation {
    std::map<std::string,bool> visibility;
    std::map<std::string,std::set<std::string>> hiddenItems;
    std::map<std::string,PresentationStyle> styles, objectStyles;
    std::vector<std::string> objectOrder;
    bool operator==(const WebPresentation& b) const {
        return visibility==b.visibility && hiddenItems==b.hiddenItems && styles==b.styles && objectStyles==b.objectStyles && objectOrder==b.objectOrder;
    }
};
struct ResolvedTerritorialPresentation {
    double opacity=1, effectiveAlpha=1;
    bool boundaryVisible=true, nameVisible=true, flagVisible=true;
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
