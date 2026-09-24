#pragma once
#include <pandoeditor/document.h>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace pandoeditor {
class ProjectSnapshot;
enum class TerritorialMutationKind { CreateCountry, CreateSubunit, CreateRegion, ChangeParent, ChangeRegionSovereign, TransferSubunit, DeleteCountry, DeleteUnits, PromoteSubunitToCountry, ConvertCountryToSubunit, ReplaceGeometry, MergeTerritorial, AnnexTerritory, SplitTerritorial, ReconcileSharedBoundary, ReconcileCoastline };
enum class ReferenceRewriteOperation { RemoveEntry, ReplaceId, ClearField, DeleteKey };
struct ReferenceRewrite { std::string sourcePath; ReferenceRewriteOperation operation=ReferenceRewriteOperation::RemoveEntry; std::string fromId,toId;
    bool operator==(const ReferenceRewrite& b) const {return sourcePath==b.sourcePath&&operation==b.operation&&fromId==b.fromId&&toId==b.toId;}
};
struct TerritorialImpact { std::string kind; ObjectRef target; std::string messageKey; };
struct ExtensionGuard { std::string id,payload; bool operator==(const ExtensionGuard& b) const {return id==b.id&&payload==b.payload;} };
struct CreateTerritorialIntent { UnitKind kind=UnitKind::Region; std::string id,name; Geometry geometry; std::optional<ObjectRef> parent,sovereign; std::string coverageMode; std::optional<std::uint32_t> explicitColor; Validity validity; std::string notes; };
struct ChangeParentIntent { ObjectRef target,parent; };
struct ChangeRegionSovereignIntent { ObjectRef target; std::optional<ObjectRef> sovereign; };
struct DeleteTerritorialIntent { std::vector<ObjectRef> targets; };
struct TransferSubunitIntent { ObjectRef target,destinationCountry; };
struct ConvertTerritorialTypeIntent { ObjectRef source; UnitKind targetKind=UnitKind::Subunit; std::optional<ObjectRef> sovereign,parent; std::string generatedId; };
// The edit session supplies the replacement geometry separately as a checked
// GeometryPatch.  Keeping the intent geometry-free makes stale preview checks
// independent from pointer-event traffic.
struct ReplaceGeometryIntent { ObjectRef target; };
struct MergeTerritorialIntent { ObjectRef target; std::vector<ObjectRef> donors; };
struct AnnexTerritoryIntent { ObjectRef target; std::vector<ObjectRef> donors; Geometry selection; };
struct SplitTerritorialIntent { ObjectRef source; Ring cutLine; int retainedPart=0; std::string createdId,createdName; };
struct DraftGeometry { ObjectRef owner; Geometry geometry; };
struct SharedBoundaryIntent { std::vector<DraftGeometry> drafts; };
enum class CoastlineAuthority { Country, Subunit, Independent };
struct CoastlineIntent { ObjectRef target; Geometry draft; CoastlineAuthority authority=CoastlineAuthority::Country; };
using TerritorialMutationIntent=std::variant<CreateTerritorialIntent,ChangeParentIntent,ChangeRegionSovereignIntent,DeleteTerritorialIntent,TransferSubunitIntent,ConvertTerritorialTypeIntent,ReplaceGeometryIntent,MergeTerritorialIntent,AnnexTerritoryIntent,SplitTerritorialIntent,SharedBoundaryIntent,CoastlineIntent>;
enum class GeometryRequirementKind { None, Prepared, WorkerPatch };
struct GeometryRequirement {
    GeometryRequirementKind kind=GeometryRequirementKind::None;
    std::string operation;
    // Every immutable geometry read by the worker, including owners used only
    // for topology/containment decisions.
    std::vector<ObjectRef> readOwners;
    struct OwnerMapping {
        ObjectRef source, result;
        bool operator==(const OwnerMapping& b) const { return source==b.source&&result==b.result; }
    };
    // Existing geometry bindings replaced by the patch.  source and result
    // differ for country -> new subunit conversion.
    std::vector<OwnerMapping> replacements;
    // Owners that may be created/deleted by one-to-many or many-to-one plans.
    std::vector<ObjectRef> createOwners;
    std::vector<ObjectRef> removableOwners;
};
struct GeometryReplacement { ObjectRef owner; Geometry geometry; };
struct GeometryCreation { ObjectRef owner; Geometry geometry; };
struct GeometryPatch { std::uint64_t sourceRevision=0; std::vector<GeometryReplacement> replacements; std::vector<GeometryCreation> creations; std::vector<ObjectRef> removedGeometryOwners; };
struct TerritorialMutationPlan { std::string projectInstanceId,documentId; std::uint64_t baseRevision=0; TerritorialMutationKind kind=TerritorialMutationKind::ChangeParent; TerritorialMutationIntent intent; std::vector<ObjectRef> targets,affectedObjects; std::vector<TerritorialImpact> impacts; std::vector<ReferenceRewrite> rewrites; std::vector<ExtensionGuard> retainedGuards; GeometryRequirement geometry; std::optional<ObjectRef> selectedAfter; bool requiresConfirmation=false; };
TerritorialMutationPlan planChangeParent(const ProjectSnapshot&,const ChangeParentIntent&);
TerritorialMutationPlan planRegionSovereign(const ProjectSnapshot&,const ChangeRegionSovereignIntent&);
TerritorialMutationPlan planCreate(const ProjectSnapshot&,const CreateTerritorialIntent&);
TerritorialMutationPlan planDelete(const ProjectSnapshot&,const DeleteTerritorialIntent&);
TerritorialMutationPlan planTransfer(const ProjectSnapshot&,const TransferSubunitIntent&);
TerritorialMutationPlan planConversion(const ProjectSnapshot&,const ConvertTerritorialTypeIntent&);
TerritorialMutationPlan planReplaceGeometry(const ProjectSnapshot&,const ReplaceGeometryIntent&);
TerritorialMutationPlan planMerge(const ProjectSnapshot&,const MergeTerritorialIntent&);
TerritorialMutationPlan planAnnex(const ProjectSnapshot&,const AnnexTerritoryIntent&);
TerritorialMutationPlan planSplit(const ProjectSnapshot&,const SplitTerritorialIntent&);
TerritorialMutationPlan planSharedBoundary(const ProjectSnapshot&,const SharedBoundaryIntent&);
TerritorialMutationPlan planCoastline(const ProjectSnapshot&,const CoastlineIntent&);
}
