#pragma once
#include "hydroruntimeprovider.h"
#include <pandoeditor/geometryoperations.h>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>
#include <array>

namespace pandoeditor {
enum class RiverRevisionMode { Explicit, LiveCoordinates };
struct RiverPartitionDonor {
    QString countryId;
    Geometry geometry;
    // Explicit: forwarded unchanged, including numeric fixture revision 1.
    // LiveCoordinates: computed inside JS as id + ':' + JSON.stringify(coords).
    RiverRevisionMode revisionMode=RiverRevisionMode::Explicit;
    QJsonValue geometryRevision=QJsonValue::Undefined;
};
struct RiverBaseComponent {
    QString key, countryId, componentKey;
    int polygonIndex=0, sourcePolygonIndex=0;
    Geometry geometry;
    // Other original component fields survive composition without normalization.
    QJsonObject attributes;
};
struct RiverPartitionRequest {
    std::vector<RiverPartitionDonor> donors;
    std::vector<HydroRiverFeatureValue> riverFeatures;
    std::vector<RiverBaseComponent> components;
    // Empty means the original module's exact defaults; no second native copy.
    QJsonObject configOverrides;
    std::optional<QString> algorithmRevision;
    QJsonValue hydroRevision=QString{};
    std::vector<EditedRiverValue> signatureEdits;
    // Opt-in live source revision. Appended to preserve aggregate callers.
    // The suffix uses exact JavaScript JSON.stringify/sort, before the kernel.
    std::optional<QString> liveHydroRevisionPrefix;
};
enum class RiverPartitionStatus { Completed, Cancelled, Failed };
enum class RiverDonorStatus { Ready, Empty, Invalid };
struct RiverPartitionCell {
    QString key, donorCountryId, componentKey, algorithmRevision;
    Geometry geometry;
    double areaM2=0, area=0;
    QStringList sourceRiverIds;
    std::vector<std::array<Point,2>> riverBoundarySegments;
    // Full owned candidate provenance, including fields unknown to this adapter.
    QJsonObject attributes;
};
struct RiverPartitionComponent {
    QString key, countryId, componentKey;
    int polygonIndex=0, sourcePolygonIndex=0;
    Geometry geometry;
    bool isRiver=false;
    std::optional<RiverPartitionCell> river;
    QJsonObject attributes;
};
struct RiverPartitionDonorResult {
    QString donorCountryId, reason;
    RiverDonorStatus status=RiverDonorStatus::Empty;
    int candidateCount=0;
};
// Pure, worker-only presentation stages. A completed normalization with no
// geometry means the exact upstream normalizer filtered this polygon.
struct RiverGeometryNormalizationResult {
    RiverPartitionStatus status=RiverPartitionStatus::Failed;
    QString detail;
    std::optional<Geometry> geometry;
    bool succeeded() const noexcept {return status==RiverPartitionStatus::Completed;}
};
struct RiverPartitionNormalizationResult {
    RiverPartitionStatus status=RiverPartitionStatus::Failed;
    QString detail;
    std::vector<RiverPartitionCell> candidates;
    QByteArray json;
    bool succeeded() const noexcept {return status==RiverPartitionStatus::Completed;}
};
struct RiverPartitionCompositionResult {
    RiverPartitionStatus status=RiverPartitionStatus::Failed;
    QString detail;
    std::vector<RiverPartitionComponent> components;
    QStringList invalidDonorIds;
    int splitComponentCount=0,riverCandidateCount=0;
    QByteArray json;
    bool succeeded() const noexcept {return status==RiverPartitionStatus::Completed;}
};
RiverGeometryNormalizationResult normalizeRiverGeometry(const Geometry&,
    const GeometryCancellation& cancelled={});
RiverPartitionNormalizationResult normalizeRiverPartitionCandidates(const std::vector<RiverPartitionCell>&,
    const GeometryCancellation& cancelled={});
RiverPartitionCompositionResult composeRiverPartitionComponents(const std::vector<RiverBaseComponent>&,
    const std::vector<RiverPartitionCell>& normalizedCells,const std::vector<RiverPartitionDonorResult>&,
    const GeometryCancellation& cancelled={});
struct RiverPartitionDiagnostics {
    QString algorithmRevision;
    QJsonValue hydroRevision;
    int scannedDonors=0, scannedRiverFeatures=0, scannedRiverParts=0, scannedRiverSegments=0;
    int boundaryIntersectionCount=0, riverIntersectionCount=0, boundaryFollowingEdges=0;
    int retainedRiverEdges=0, prunedRiverEdges=0, tracedFaceCount=0, candidateCount=0;
    double computeMs=0;
};
struct RiverPartitionResult {
    RiverPartitionStatus status=RiverPartitionStatus::Failed;
    QString detail;
    std::vector<RiverPartitionCell> candidates;
    std::vector<RiverPartitionComponent> components;
    std::vector<RiverPartitionDonorResult> donors;
    RiverPartitionDiagnostics diagnostics;
    QStringList invalidDonorIds, donorRevisionStrings;
    QString editedRiverSignature;
    int splitComponentCount=0, riverCandidateCount=0;
    bool inputUnchanged=false, compositionInputUnchanged=false;
    // Owned exact JS serialization for differential diagnostics, never JS objects.
    QByteArray json;
    // Actual pinned presentation normalizer, after raw identity creation.
    std::vector<RiverPartitionCell> presentationCandidates;
    std::vector<RiverPartitionComponent> presentationComponents;
    QByteArray presentationJson, workspaceJson;
    bool succeeded() const noexcept { return status==RiverPartitionStatus::Completed; }
};
// Fresh private engine in the calling worker thread. Synchronous JS cannot be
// interrupted mid-call; cancellation is checked around load/call/decode and wins
// over errors. Never call from the GUI thread. Raw geometry is not normalized:
// the separately returned presentation is normalized AFTER identities are produced.
RiverPartitionResult calculateRiverPartitions(const RiverPartitionRequest&,
    const GeometryCancellation& cancelled={});
// Narrow differential seam: {request, components, liveDonorIndices?, signatureEdits?, liveHydroRevisionPrefix?}.
// Same production path and typed output. Finite, owned JSON data only.
RiverPartitionResult calculateRiverPartitionsJson(const QByteArray&,
    const GeometryCancellation& cancelled={});
}
