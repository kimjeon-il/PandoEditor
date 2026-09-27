#pragma once
#include <pandoeditor/document.h>
#include <pandoeditor/territorialmutation.h>
#include <pandoeditor/historicalinstantiation.h>
#include <pandoeditor/gisexchange.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace pandoeditor {
class Project;
class ProjectSnapshot;
namespace detail { struct DocumentState; }

enum class TerritorialField { Name, Notes, ValidFrom, ValidTo };
struct TerritorialFieldEdit { ObjectRef target; TerritorialField field; std::string value; };
struct TerritorialColorEdit { std::vector<ObjectRef> targets; std::optional<std::uint32_t> color; };
struct TerritorialLockEdit { std::vector<ObjectRef> targets; bool locked=false; };
struct ApplyTerritorialMutation { TerritorialMutationPlan plan; std::optional<GeometryPatch> geometry; };

struct CountryProperties {
    std::string name, memo;
    std::uint32_t color=0;
    double opacity=1;
    std::string layerId;
    bool operator==(const CountryProperties& other) const;
};
struct CountryEdit { std::string id; CountryProperties properties; };
struct PropertyEdits { std::vector<CountryEdit> countries; std::vector<Layer> layers; std::vector<TerritorialFieldEdit> fields; };
struct SetCountryColor { std::string id; std::uint32_t color; };
struct AddLayer { std::string id, name; };
struct RemoveLayer { std::string id; };
struct MoveLayer { std::string id; int delta; };
struct SetLayerVisible { std::string id; bool value; };
struct SetLayerLocked { std::string id; bool value; };
struct MoveCountry { std::string id, layerId; };
// A null value deletes the target. Geometry is an immutable new version and is
// inserted only into the candidate; the live document changes at confirm.
using ContentValue=std::variant<std::monostate,PlaceLabel,HydroFeature,DistributionLayer,
    DistributionEntry,GenericFeature,CountryDetails,TerritorialSymbolStyle>;
struct ContentEdit {
    ObjectRef target;
    ContentValue value;
    std::optional<std::pair<GeometryRef,Geometry>> geometry;
    bool create=false;
};
struct SetPhysicalData { PhysicalDataSettings settings; };
using CommandAction = std::variant<std::monostate, SetCountryColor, AddLayer,
    RemoveLayer, MoveLayer, SetLayerVisible, SetLayerLocked, MoveCountry,
    TerritorialFieldEdit, TerritorialColorEdit, TerritorialLockEdit, ApplyTerritorialMutation, ContentEdit, SetPhysicalData,
    HistoricalInstantiationPlan, GisGenericImportPlan, GisTerritorialImportPlan,
    GisDistributionImportPlan>;
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
// Calculated against validated immutable states during prepare. Candidate consumers
// may narrow their work; exact hit testing and document validation stay authoritative.
struct ChangeImpact {
    std::vector<ObjectRef> changedObjects;
    std::vector<GeometryRef> changedGeometries;
    std::vector<ObjectRef> presentationInvalidations;
    std::size_t retainedGeometryCount=0;
    std::size_t estimatedNewGeometryBytes=0;
    bool requiresFullSpatialRebuild=false;
    bool requiresFullSceneRebuild=true;
};
struct CommandResult {
    CommandStatus status=CommandStatus::Rejected;
    CommandError error=CommandError::None;
    std::string detail;
    ChangeImpact impact;
    bool ok() const noexcept { return status!=CommandStatus::Rejected; }
    bool changed() const noexcept { return status==CommandStatus::Applied; }
};
class ChangeSet {
public:
    const ProjectDocument& before() const;
    const ProjectDocument& after() const;
    const CommandRequest& request() const noexcept { return request_; }
    const ChangeImpact& impact() const noexcept { return impact_; }
private:
    friend class Project;
    friend class CommandProcessor;
    ChangeSet(std::shared_ptr<const detail::DocumentState> before,
              std::shared_ptr<const detail::DocumentState> after, CommandRequest request);
    std::shared_ptr<const detail::DocumentState> before_, after_;
    CommandRequest request_;
    ChangeImpact impact_;
    std::uint64_t checkpointBefore_=0, checkpointAfter_=0;
    bool historyCheckpoint_=false; // command-owned source compatibility, never a request flag
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
struct TerritorialPlanResult : CommandResult { std::optional<TerritorialMutationPlan> plan; };
struct ExtensionRewriteResult { bool ok=true; std::string detail; std::vector<std::string> handledExtensionIds; };
using ExtensionRewriter=std::function<ExtensionRewriteResult(const ProjectDocument&,const TerritorialMutationPlan&,std::vector<PreservedExtension>&)>;
// Compare complete canonical data, retaining opaque extension bytes exactly.
// Shared immutable geometry allocations are a fast path, never an equality rule.
bool semanticallyEqual(const ProjectDocument& a, const ProjectDocument& b);
class CommandProcessor {
public:
    static CommandRequest makeRequest(const Project&, std::string commandId, CommandArguments);
    static PrepareResult prepare(const Project&, const CommandRequest&);
    static PrepareResult prepare(const ProjectSnapshot&, const CommandRequest&);
    static PrepareResult prepare(const Project&, const CommandRequest&, const ExtensionRewriter&);
    static PrepareResult prepare(const ProjectSnapshot&, const CommandRequest&, const ExtensionRewriter&);
    static TerritorialPlanResult planTerritorial(const ProjectSnapshot&,const TerritorialMutationIntent&);
    static TerritorialPlanResult planTerritorial(const Project&,const TerritorialMutationIntent&);
    static CommandResult confirm(Project&, CommandPreview&);
    static void cancel(CommandPreview& preview) noexcept { preview.change_.reset(); }
};
} // namespace pandoeditor
