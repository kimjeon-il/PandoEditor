#pragma once
#include <pandoeditor/document.h>
#include <utility>

namespace pandoeditor::detail {
// Construct at its final heap address. Views borrow this document, never a
// separate/movable candidate. A shared const state is the snapshot boundary.
struct DocumentState {
    ProjectDocument document;
    DocumentIndex index;
    std::vector<CountryView> countries;
    std::map<std::string, std::size_t> countryIndex;

    DocumentState() = default; // unloaded Project only
    explicit DocumentState(ProjectDocument candidate)
        : document(std::move(candidate)), index(validateDocument(document)),
          countries(countryViews(document))
    {
        for (std::size_t i = 0; i < countries.size(); ++i)
            countryIndex.emplace(countries[i].id, i);
    }
    DocumentState(const DocumentState&) = delete;
    DocumentState& operator=(const DocumentState&) = delete;
    DocumentState(DocumentState&&) = delete;
    DocumentState& operator=(DocumentState&&) = delete;
};
} // namespace pandoeditor::detail
