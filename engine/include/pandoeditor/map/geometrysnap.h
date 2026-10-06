#pragma once
#include <pandoeditor/project.h>
#include <array>
#include <functional>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Qt-free adapter of the pinned web snap contracts. This helper never edits Project.
namespace geometrysnap {
using pandoeditor::Point;
using SourceRanks=std::map<pandoeditor::ObjectRef,std::uint64_t>;
struct Request {
    Point coordinate;
    double margin=0; // Worker margin, twice marginForScale(camera.scale).
    std::vector<std::string> activeOwnerIds;
    std::string sourceKey;
    std::shared_ptr<const pandoeditor::Geometry> sourceGeometry;
    std::uint64_t sourceRevision=0;
    // Transition-owned ordering metadata. Absence keeps the standalone helper's
    // query-snapshot fallback; explicit maps must match the prepared document.
    std::shared_ptr<const SourceRanks> sourceRanks;
    std::string sourceRanksInstance;
    std::uint64_t sourceRanksRevision=0;
};
struct Candidate {
    std::string kind;
    std::optional<Point> coordinate,a,b;
    std::vector<std::string> ownerIds;
    std::string nodeKey,segmentKey;
};
struct Diagnostics {
    std::size_t nearbyObjects=0,visitedSegments=0,intersectionTests=0;
    std::size_t segmentEntriesExamined=0;
    std::size_t geometryIndexBuilds=0,preparedObjects=0;
};
struct CandidateBatch { std::vector<Candidate> candidates; Diagnostics diagnostics; };
struct Result {
    Candidate candidate;
    Point coordinate;
    double distancePx=0;
    std::optional<double> segmentT;
    std::optional<std::array<Point,2>> segmentEndpoints;
};
using ProjectPoint=std::function<std::optional<Point>(Point)>;
class Index {
public:
    Index();
    ~Index();
    Index(const Index&)=delete;
    Index& operator=(const Index&)=delete;
    // Serializes prepare/collect. Snapshot geometry allocations are shared, never copied.
    // Updates preserve insertion order; removal followed by re-addition appends.
    void prepare(const pandoeditor::ProjectSnapshot&);
    CandidateBatch collect(const Request&);
    CandidateBatch prepareAndCollect(const pandoeditor::ProjectSnapshot&,const Request&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
double marginForScale(double projectionScale);
double snapThreshold(const std::string& pointerType="mouse");
// Exact web toFixed(7) for supported finite geographic values (absolute <= 1e12).
// Nonfinite/unrepresentable transient inputs are rejected, never normalized.
std::string nodeKey(Point);
std::optional<Result> resolveSnap(Point coordinate,Point screenPoint,
    const std::vector<Candidate>&,const ProjectPoint&,
    const std::string& pointerType="mouse",const std::string& excludeNodeKey={});
}
