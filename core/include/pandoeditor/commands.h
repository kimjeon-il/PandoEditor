#pragma once
#include <pandoeditor/document.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace pandoeditor {
class Project;
namespace detail { struct DocumentState; }

struct CountryProperties {
    std::string name, memo;
    std::uint32_t color=0;
    double opacity=1;
    std::string layerId;
    bool operator==(const CountryProperties& other) const;
};
struct CountryEdit { std::string id; CountryProperties properties; };
struct PropertyEdits { std::vector<CountryEdit> countries; std::vector<Layer> layers; };
struct SetCountryColor { std::string id; std::uint32_t color; };
struct AddLayer { std::string id, name; };
struct RemoveLayer { std::string id; };
struct MoveLayer { std::string id; int delta; };
struct SetLayerVisible { std::string id; bool value; };
struct SetLayerLocked { std::string id; bool value; };
struct MoveCountry { std::string id, layerId; };
using CommandAction = std::variant<std::monostate, SetCountryColor, AddLayer,
    RemoveLayer, MoveLayer, SetLayerVisible, SetLayerLocked, MoveCountry>;
struct CommandArguments { PropertyEdits properties; CommandAction action; };
struct CommandRequest {
    std::string commandId, projectInstanceId, documentId;
    std::uint64_t revision=0;
    std::vector<ObjectRef> targets;
    CommandArguments args;
};
enum class CommandStatus { Prepared, Applied, NoOp, Rejected };
enum class CommandError {
    None, ProjectMismatch, DocumentMismatch, StaleRevision, InvalidCommand,
    InvalidTargets, InvalidArguments, Locked, UnsupportedDependency,
    ValidationFailed, PrepareFailed, CommitFailed, PreviewConsumed, RevisionOverflow
};
const char* commandErrorCode(CommandError error) noexcept;
struct CommandResult {
    CommandStatus status=CommandStatus::Rejected;
    CommandError error=CommandError::None;
    std::string detail;
    bool ok() const noexcept { return status!=CommandStatus::Rejected; }
    bool changed() const noexcept { return status==CommandStatus::Applied; }
};
class ChangeSet {
public:
    const ProjectDocument& before() const;
    const ProjectDocument& after() const;
    const CommandRequest& request() const noexcept { return request_; }
private:
    friend class Project;
    friend class CommandProcessor;
    ChangeSet(std::shared_ptr<const detail::DocumentState> before,
              std::shared_ptr<const detail::DocumentState> after, CommandRequest request);
    std::shared_ptr<const detail::DocumentState> before_, after_;
    CommandRequest request_;
};
class CommandPreview {
public:
    CommandPreview(CommandPreview&&) noexcept=default;
    CommandPreview& operator=(CommandPreview&&) noexcept=default;
    CommandPreview(const CommandPreview&)=delete;
    CommandPreview& operator=(const CommandPreview&)=delete;
    bool pending() const noexcept { return static_cast<bool>(change_); }
    const ChangeSet& change() const;
private:
    friend class CommandProcessor;
    explicit CommandPreview(std::unique_ptr<const ChangeSet> change) : change_(std::move(change)) {}
    std::unique_ptr<const ChangeSet> change_;
};
struct PrepareResult : CommandResult { std::optional<CommandPreview> preview; };
// Compare complete canonical data, retaining opaque extension bytes exactly.
// Shared immutable geometry allocations are a fast path, never an equality rule.
bool semanticallyEqual(const ProjectDocument& a, const ProjectDocument& b);
class CommandProcessor {
public:
    static CommandRequest makeRequest(const Project&, std::string commandId, CommandArguments);
    static PrepareResult prepare(const Project&, const CommandRequest&);
    static CommandResult confirm(Project&, CommandPreview&);
    static void cancel(CommandPreview& preview) noexcept { preview.change_.reset(); }
};
} // namespace pandoeditor
