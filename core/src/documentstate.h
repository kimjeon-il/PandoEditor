#pragma once
#include <pandoeditor/document.h>
#include <pandoeditor/objectproperties.h>
#include <utility>
#include <set>
#include <algorithm>

namespace pandoeditor::detail {
// Construct at its final heap address. Views borrow this document, never a
// separate/movable candidate. A shared const state is the snapshot boundary.
struct DocumentState {
    ProjectDocument document;
    DocumentIndex index;
    std::map<ObjectRef,ObjectPropertyView> properties;
    std::vector<CountryView> countries;
    std::map<std::string, std::size_t> countryIndex;
    std::set<std::string> inactiveIds;
    bool needsHistoryPruning=false;

    DocumentState() = default; // unloaded Project only
    explicit DocumentState(ProjectDocument candidate,std::set<std::string> inactive={})
        : document(std::move(candidate)), index(validateDocument(document)), inactiveIds(std::move(inactive))
    {
        if(!isStaticTimeline(document))return;
        properties=objectPropertyViews(document);
        auto allCountries=countryViews(document);
        countries.reserve(allCountries.size());
        for(const auto& view:allCountries)if(!inactiveIds.count(view.id))countries.push_back(view);
        for(const auto& u:document.units)if(u.kind==UnitKind::General &&
            (trimWebText(u.notes)!=u.notes || (u.name.empty() && u.nameExplicit)))needsHistoryPruning=true;
        for (std::size_t i = 0; i < countries.size(); ++i)
            countryIndex.emplace(countries[i].id, i);
    }
    DocumentState(const DocumentState&) = delete;
    DocumentState& operator=(const DocumentState&) = delete;
    DocumentState(DocumentState&&) = delete;
    DocumentState& operator=(DocumentState&&) = delete;
};
} // namespace pandoeditor::detail
