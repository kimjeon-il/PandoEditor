#pragma once
#include <pandoeditor/document.h>
#include <string>
#include <variant>
#include <vector>

namespace pandoeditor {
struct CountryProperties {
    std::string name, memo;
    std::uint32_t color;
    double opacity;
    std::string layerId;
    bool operator==(const CountryProperties& other) const;
};

class Project {
public:
    Project() = default;
    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;
    Project(Project&&) noexcept = default;
    Project& operator=(Project&&) noexcept = default;
    static std::string normalizeName(const std::string& name);
    static void validate(const ProjectDocument& document);
    static void validate(const std::vector<Country>& countries);
    void replace(ProjectDocument document);
    void replace(std::vector<Country> countries);
    const ProjectDocument& document() const noexcept { return document_; }
    const std::vector<CountryView>& countries() const noexcept { return countryViews_; }
    const std::vector<Layer>& layers() const noexcept { return document_.presentation.userLayers; }
    const DocumentIndex& index() const noexcept { return index_; }
    const CountryView* country(const std::string& id) const;
    const Layer* layer(const std::string& id) const;
    bool editable(const std::string& id) const;
    std::string pick(Point point) const;
    bool setColor(const std::string& id, std::uint32_t color);
    bool renameCountry(const std::string& id, const std::string& name);
    bool setMemo(const std::string& id, const std::string& memo);
    bool setCountryOpacity(const std::string& id, double opacity);
    bool moveCountry(const std::string& id, const std::string& layerId);
    bool addLayer(const std::string& id, const std::string& name);
    bool removeLayer(const std::string& id);
    bool renameLayer(const std::string& id, const std::string& name);
    bool setLayerVisible(const std::string& id, bool visible);
    bool setLayerLocked(const std::string& id, bool locked);
    bool setLayerOpacity(const std::string& id, double opacity);
    bool moveLayer(const std::string& id, int delta);
    bool undo();
    bool redo();
    bool canUndo() const noexcept { return cursor_ > 0; }
    bool canRedo() const noexcept { return cursor_ < commands_.size(); }
    bool dirty() const;
    void markSaved();
private:
    struct CountryChange { std::string id; CountryProperties before, after; };
    struct LayersChange { std::vector<Layer> before, after; };
    using Command = std::variant<CountryChange, LayersChange>;
    bool changeCountry(const std::string& id, const CountryProperties& next);
    bool changeLayers(std::vector<Layer> next);
    void apply(const Command& command, bool forward);
    ProjectDocument document_;
    DocumentIndex index_;
    std::vector<CountryView> countryViews_;
    std::map<std::string,std::size_t> countryIndex_;
    std::vector<Command> commands_;
    std::vector<CountryProperties> savedCountries_;
    std::vector<Layer> savedLayers_;
    std::size_t cursor_ = 0;
};
} // namespace pandoeditor
