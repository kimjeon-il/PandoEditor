#pragma once
#include <pandoeditor/geometryoperations.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pandoeditor {
enum class TerritorySelectionKind { Annex, BoundedCreation };
enum class TerritorySelectionMethod { None, Line, Polygon, Components };
enum class TerritorySelectionPhase { None, Drawing, Candidate, Components, Result };
enum class TerritoryMethodChange { Activated, NeedsConfirmation, AwaitingComponentArchive, Rejected };
enum class TerritoryArchiveReadiness { Ready, CalculationPending, NoCurrentGeometry, InvalidPhase, RiverComponentsPending, BoundedSourceExhausted };
enum class TerritoryRiverStatus { Idle, Pending, Ready };
struct TerritorySelectionSource {
    ObjectRef ref;
    Geometry geometry;
    GeometryRef geometryRef;
    std::string name, propertiesJson="{}";
};
struct TerritorySelectionCandidate {
    std::string id;
    Geometry geometry;
    std::optional<double> area;
};
struct TerritorySelectionComponent {
    std::string key,countryId,countryName,componentKey;
    std::size_t polygonIndex=0,sourcePolygonIndex=0;
    Geometry geometry;
    // Supplied by the real river partition owner. This class never generates
    // river cells or claims readiness on the basis of an enabled boolean.
    std::string partitionKind,provenanceJson="{}",snapshotId;
    bool usesRiverBoundary=false;
};
struct TerritorySelectionComponentFeature {
    TerritorySelectionSource source;
    std::vector<std::size_t> sourcePolygonIndices;
};
struct TerritorySelectionPart {
    std::string id;
    TerritorySelectionMethod method=TerritorySelectionMethod::None;
    Geometry geometry;
    std::optional<TerritorySelectionComponent> component;
};
struct TerritorySelectionComponentSnapshot {
    std::string id;
    std::vector<TerritorySelectionComponent> items;
};
struct TerritorySelectionRiverSliverContext {
    std::string donorId;
    std::size_t polygonIndex=0;
    std::vector<Geometry> unselectedGeometries;
};
struct TerritorySelectionState {
    // Monotonic for this selection value, including source resets. The owner
    // additionally checks its session/project identity before installing results.
    std::uint64_t revision=0;
    std::optional<std::uint64_t> derivedRevision;
    TerritorySelectionKind kind=TerritorySelectionKind::Annex;
    TerritorySelectionMethod activeMethod=TerritorySelectionMethod::None;
    TerritorySelectionMethod requestedMethod=TerritorySelectionMethod::None;
    TerritorySelectionPhase activePhase=TerritorySelectionPhase::None;
    std::optional<TerritorySelectionMethod> methodChangeConfirmation;
    std::vector<TerritorySelectionSource> sources;
    std::vector<TerritorySelectionComponentFeature> componentFeatures;
    std::vector<TerritorySelectionComponent> components,riverComponents;
    std::vector<TerritorySelectionCandidate> candidates;
    std::vector<std::string> selectedCandidateIds,selectedComponentKeys;
    std::vector<TerritorySelectionPart> parts;
    std::vector<TerritorySelectionComponentSnapshot> componentSnapshots;
    std::optional<Geometry> baseSourceGeometry,workingSourceGeometry,archivedGeometry;
    std::optional<Geometry> currentGeometry,combinedGeometry,remainingGeometry;
    bool useRiverBoundaries=false;
    TerritoryRiverStatus riverStatus=TerritoryRiverStatus::Idle;
    std::string riverPreparationKey;
    std::vector<TerritorySelectionRiverSliverContext> derivedRiverSliverContext;
};
// Calculation-only values. Semantic IDs, selections, parts and confirmations
// never travel back from the worker, so installing a result cannot lose input.
struct TerritorySelectionDerivedResult {
    std::uint64_t inputRevision=0;
    GeometryOperationStatus status=GeometryOperationStatus::Failed;
    std::string detail;
    std::vector<TerritorySelectionComponentFeature> componentFeatures;
    std::vector<TerritorySelectionComponent> components;
    std::optional<Geometry> baseSourceGeometry,workingSourceGeometry,archivedGeometry;
    std::optional<Geometry> currentGeometry,combinedGeometry,remainingGeometry;
    std::vector<TerritorySelectionRiverSliverContext> riverSliverContext;
    bool succeeded() const noexcept {return status==GeometryOperationStatus::Completed;}
};
struct TerritorySelectionDraftResult {
    GeometryOperationStatus status=GeometryOperationStatus::Failed;
    std::string detail;
    std::vector<TerritorySelectionCandidate> candidates;
    bool succeeded() const noexcept {
        return status==GeometryOperationStatus::Completed||status==GeometryOperationStatus::Empty;
    }
};
// Run in the calculation worker. Cancellation is cooperative between polygons
// and clipping operations, not interruption of a synchronous clipper call.
TerritorySelectionDerivedResult rebuildTerritorySelection(const TerritorySelectionState&,
    const GeometryCancellation& cancelled={});
// Produces exactly one drawn candidate, even for disconnected transfer.
TerritorySelectionDraftResult prepareTerritoryPolygonCandidates(const Geometry& drawn,
    const Geometry& workingSource,const Geometry& target,const GeometryCancellation& cancelled={});
// A value for integration into the existing GeometryEditSession. It owns no project,
// history, stages, worker identity, preview or UI. Inputs/outputs are copied
// geometry snapshots; const access prevents mutation of the original source.
// The owner must enforce stage, session/project revisions and matching validated
// preview readiness before calling addPart, advancing review or applying.
class TerritorySelection {
public:
    explicit TerritorySelection(TerritorySelectionKind kind=TerritorySelectionKind::Annex);
    const TerritorySelectionState& state() const noexcept {return state_;}
    const std::string& lastError() const noexcept {return lastError_;}
    bool derivedReady() const noexcept {return state_.derivedRevision==state_.revision;}
    bool installDerived(TerritorySelectionDerivedResult);
    // Mutations change only intent and invalidate caches; call rebuild/install
    // asynchronously before consuming derived geometry or archiving.
    bool resetSources(std::vector<TerritorySelectionSource>);
    TerritoryMethodChange requestMethod(TerritorySelectionMethod,bool draftHasWork=false);
    bool confirmMethodChange();
    bool cancelMethodChange();
    void cancelRequestedMethod() noexcept;
    bool clearCurrent();
    bool finishArchivedDraft();
    bool setCandidates(std::vector<TerritorySelectionCandidate>);
    bool toggleCandidate(const std::string& id);
    bool toggleComponent(const std::string& key);
    bool toggleRiverBoundaries(bool enabled);
    // Caller has already checked source/session/preparation identity. Items are
    // the production composition (including untouched fallback original cells).
    bool installRiverComponents(std::vector<TerritorySelectionComponent>,std::string preparationKey);
    const std::vector<TerritorySelectionComponent>& activeComponents() const noexcept;
    // Operands follow source/index order, never selected-ID insertion order.
    std::vector<Geometry> currentOperands() const;
    TerritoryArchiveReadiness archiveReadiness() const noexcept;
    bool candidateRequiresArchival() const noexcept;
    bool addPart();
    bool removePart(const std::string& id);
    bool undoPart(bool draftHasWork=false);
    std::size_t partCount() const noexcept;
    std::vector<std::string> donorIds() const;
    // Reads the installed worker result; throws while derived data is pending.
    // No clipping is performed on the owner thread.
    std::vector<TerritorySelectionRiverSliverContext> riverSliverContext() const;
private:
    TerritorySelectionState state_;
    std::uint64_t nextId_=0;
    std::string lastError_;
    bool touch(bool rebuildSources=false);
};
}
