#pragma once
#include <pandoeditor/document.h>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace pandoeditor {
class ProjectSnapshot;
enum class TerritorialMutationKind { CreateCountry, CreateSubunit, CreateRegion, ChangeParent, ChangeRegionSovereign, TransferSubunit, DeleteCountry, DeleteUnits, PromoteSubunitToCountry, ConvertCountryToSubunit };
enum class ReferenceRewriteOperation { RemoveEntry, ReplaceId, ClearField, DeleteKey };
struct ReferenceRewrite { std::string sourcePath; ReferenceRewriteOperation operation=ReferenceRewriteOperation::RemoveEntry; std::string fromId,toId; };
struct TerritorialImpact { std::string kind; ObjectRef target; std::string messageKey; };
struct ExtensionGuard { std::string id,payload; };
struct CreateTerritorialIntent { UnitKind kind=UnitKind::Region; std::string id,name; Geometry geometry; std::optional<ObjectRef> parent,sovereign; std::string coverageMode; std::optional<std::uint32_t> explicitColor; Validity validity; std::string notes; };
struct ChangeParentIntent { ObjectRef target,parent; };
struct ChangeRegionSovereignIntent { ObjectRef target; std::optional<ObjectRef> sovereign; };
struct DeleteTerritorialIntent { std::vector<ObjectRef> targets; };
struct TransferSubunitIntent { ObjectRef target,destinationCountry; };
struct ConvertTerritorialTypeIntent { ObjectRef source; UnitKind targetKind=UnitKind::Subunit; std::optional<ObjectRef> sovereign,parent; std::string generatedId; };
using TerritorialMutationIntent=std::variant<CreateTerritorialIntent,ChangeParentIntent,ChangeRegionSovereignIntent,DeleteTerritorialIntent,TransferSubunitIntent,ConvertTerritorialTypeIntent>;
enum class GeometryRequirementKind { None, Prepared, WorkerPatch };
struct GeometryRequirement { GeometryRequirementKind kind=GeometryRequirementKind::None; std::string operation; std::vector<ObjectRef> sources; };
struct GeometryReplacement { ObjectRef owner; Geometry geometry; };
struct GeometryPatch { std::uint64_t sourceRevision=0; std::vector<GeometryReplacement> replacements; std::vector<ObjectRef> removedGeometryOwners; };
struct TerritorialMutationPlan { std::string projectInstanceId,documentId; std::uint64_t baseRevision=0; TerritorialMutationKind kind=TerritorialMutationKind::ChangeParent; TerritorialMutationIntent intent; std::vector<ObjectRef> targets,affectedObjects; std::vector<TerritorialImpact> impacts; std::vector<ReferenceRewrite> rewrites; std::vector<ExtensionGuard> retainedGuards; GeometryRequirement geometry; std::optional<ObjectRef> selectedAfter; bool requiresConfirmation=false; };
TerritorialMutationPlan planChangeParent(const ProjectSnapshot&,const ChangeParentIntent&);
TerritorialMutationPlan planRegionSovereign(const ProjectSnapshot&,const ChangeRegionSovereignIntent&);
TerritorialMutationPlan planCreate(const ProjectSnapshot&,const CreateTerritorialIntent&);
TerritorialMutationPlan planDelete(const ProjectSnapshot&,const DeleteTerritorialIntent&);
TerritorialMutationPlan planTransfer(const ProjectSnapshot&,const TransferSubunitIntent&);
TerritorialMutationPlan planConversion(const ProjectSnapshot&,const ConvertTerritorialTypeIntent&);
}
