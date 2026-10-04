#pragma once
#include <pandoeditor/document.h>
namespace pandoeditor {
// Pure read model. It never rewrites source geometry, relations or automatic colors.
std::string trimWebText(const std::string&);
std::string objectDisplayName(const TerritorialUnit&);
std::uint32_t effectiveObjectColor(const ProjectDocument&, const ObjectRef&,
                                  std::uint32_t countryDefault=0xcccccc,
                                  std::uint32_t fallback=0x8c68d8, bool ignoreOwnExplicit=false);
struct ObjectPropertyView { std::string displayName; std::uint32_t effectiveColor=0; };
std::map<ObjectRef,ObjectPropertyView> objectPropertyViews(const ProjectDocument&);
}
