#pragma once
#include <pandoeditor/commands.h>
#include <string>
#include <memory>
#include <vector>

namespace pandoeditor {

class Project {
public:
    Project();
    const std::string& instanceId() const { return instanceId_; }
    std::uint64_t revision() const { return revision_; }
    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;
    Project(Project&&) noexcept = default;
    Project& operator=(Project&&) noexcept = default;
    static std::string normalizeName(const std::string& name);
    static void validate(const ProjectDocument& document);
    static void validate(const std::vector<Country>& countries);
    void replace(ProjectDocument document);
    void replace(std::vector<Country> countries);
    const ProjectDocument& document() const noexcept;
    const std::vector<CountryView>& countries() const noexcept;
    const std::vector<Layer>& layers() const noexcept;
    const DocumentIndex& index() const noexcept;
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
    void markSaved() noexcept;
private:
    friend class CommandProcessor;
    bool execute(std::string commandId, CommandArguments args);
    bool changeCountry(const std::string& id, CountryProperties next);
    bool changeLayer(Layer next);
    void apply(const ChangeSet& change);
    // All mutation (including prepare/confirm) is serialized by the caller.
    // Immutable snapshots can be retained; background jobs are M1.4 scope.
    std::shared_ptr<const detail::DocumentState> state_, saved_;
    std::vector<ChangeSet> commands_;
    std::string instanceId_;
    std::uint64_t revision_ = 0;
    std::size_t cursor_ = 0;
};
} // namespace pandoeditor
