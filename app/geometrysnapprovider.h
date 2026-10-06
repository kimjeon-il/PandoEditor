#pragma once
#include <pandoeditor/map/geometrysnap.h>
#include <QObject>
#include <QString>
#include <QPointer>
#include <optional>
#include <set>
#include <pandoeditor/jobs.h>
class CommandJobRunner;
namespace geometrysnap {
// Owner-thread cache of inert candidates. Completion only readies the next
// pointer event; it never retroactively changes an accepted coordinate.
class Provider : public QObject {
public:
    explicit Provider(CommandJobRunner&,QObject* parent=nullptr);
    ~Provider() override;
    const std::vector<Candidate>& candidates(const pandoeditor::ProjectSnapshot&,
        const Request&,const QString& tool,double baseMargin,std::uint64_t sourceEpoch=0);
    void reset();
    // The shared edit Worker stops independently of its READY snap cache.
    // Only a genuine subsequent execute observes/rebases current source rows.
    void notifyWorkerStopped();
    std::uint64_t beginWorkerOperation(const pandoeditor::ProjectSnapshot&);
    bool completeWorkerOperation(const pandoeditor::ProjectSnapshot&,std::uint64_t lifecycleEpoch);
    // Synchronize at an actual Worker query or a root-country sync-patch event.
    // Unchanged revisions are O(1); content-only updates retain map identity.
    void synchronizeSources(const pandoeditor::ProjectSnapshot&);
    // Publication alone observes installation, not intermediate generic edits.
    void synchronizeInstallation(const pandoeditor::ProjectSnapshot&);
    bool requiresImmediateSynchronization(const pandoeditor::ProjectSnapshot&,
        const pandoeditor::ChangeImpact&) const;
    std::shared_ptr<const SourceRanks> sourceRanks() const {return sourceRanks_;}
    std::shared_ptr<const pandoeditor::Geometry> retainSource(const pandoeditor::Geometry&,const QString& identity);
    std::string sourceKey() const {return sourceKey_;}
    QString status() const {return status_;}
    const Diagnostics& diagnostics() const {return diagnostics_;}
    std::uint64_t submittedCount() const {return submitted_;}
private:
    void updateSourceRanks(const pandoeditor::ProjectSnapshot&,bool rebase=false);
    QPointer<CommandJobRunner> runner_;
    std::shared_ptr<Index> index_;
    std::optional<pandoeditor::JobTicket> job_;
    QString key_,status_="empty";
    std::uint64_t epoch_=0,submitted_=0;
    std::vector<Candidate> candidates_;
    Diagnostics diagnostics_;
    QString sourceIdentity_;
    std::string sourceKey_;
    std::shared_ptr<const pandoeditor::Geometry> source_;
    std::uint64_t sourceSequence_=0;
    std::shared_ptr<const SourceRanks> sourceRanks_;
    std::set<std::string> rootGeneralIds_;
    std::string ranksInstance_;
    std::uint64_t ranksRevision_=0,nextSourceRank_=0;
    std::uint64_t workerEpoch_=0;
    bool workerStopped_=false;
};
}
