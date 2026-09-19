#pragma once
#include <pandoeditor/commands.h>
#include <pandoeditor/objectproperties.h>
#include <string>
#include <memory>
#include <vector>
#include <utility>

namespace pandoeditor {

class Project;
class ProjectSnapshot {
public:
    const std::string& instanceId() const { return instanceId_; }
    std::uint64_t revision() const { return revision_; }
    const ProjectDocument& document() const noexcept;
    const DocumentIndex& index() const noexcept;
    const std::vector<Layer>& layers() const noexcept;
    const CountryView* country(const std::string&) const;
    const Layer* layer(const std::string&) const;
    bool matches(const Project&) const noexcept;
private:
    friend class Project;
    friend class CommandProcessor;
    ProjectSnapshot(std::shared_ptr<const detail::DocumentState> state, std::string instanceId, std::uint64_t revision)
        : state_(std::move(state)), instanceId_(std::move(instanceId)), revision_(revision) {}
    std::shared_ptr<const detail::DocumentState> state_;
    std::string instanceId_;
    std::uint64_t revision_;
};

class Project {
public:
    Project();
    ProjectSnapshot snapshot() const { return {state_,instanceId_,revision_}; }
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
    const ObjectPropertyView* propertyView(const ObjectRef&) const;
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
    // Capture snapshots and mutate Project only on the owner/editor thread.
    // Workers may prepare against ProjectSnapshot, never against live Project.
    std::shared_ptr<const detail::DocumentState> state_, saved_;
    std::vector<ChangeSet> commands_;
    std::string instanceId_;
    std::uint64_t revision_ = 0;
    std::uint64_t checkpoint_=0, savedCheckpoint_=0, checkpointSequence_=0;
    std::size_t cursor_ = 0;
};
} // namespace pandoeditor
