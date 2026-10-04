#pragma once
#include <pandoeditor/document.h>
#include <algorithm>

// Fixture construction uses the same three canonical static record collections
// as production commands; the identity contains no shape or relationship state.
inline void appendTerritory(pandoeditor::ProjectDocument& document,
                            pandoeditor::TerritorialUnit identity,
                            pandoeditor::GeometryRef geometry,
                            const std::string& parentId="",
                            const std::string& coverageMode="explicit") {
    const auto id=identity.id;
    document.units.push_back(std::move(identity));
    pandoeditor::addStaticTerritorialRecords(document,id,std::move(geometry),parentId,coverageMode);
}
inline void setFixtureParent(pandoeditor::ProjectDocument& document,
                             const pandoeditor::ObjectRef& child,
                             const pandoeditor::ObjectRef& parent) {
    const auto unit=std::find_if(document.units.begin(),document.units.end(),[&](const auto& row){return row.id==child.id;});
    if(unit==document.units.end()||unit->kind!=pandoeditor::UnitKind::General)throw std::invalid_argument("fixture parent requires general entity");
    pandoeditor::staticParentRelation(document,child.id).parentId=parent.id;
}
