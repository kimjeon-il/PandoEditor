#include "editorcontroller.h"
#include "webimport.h"
#include <QFileInfo>
#include <QDir>
#include <QPointer>
#include <exception>

using namespace pandoeditor;
namespace {
QString sourceKey(const QUrl& url) {
    if(url.isEmpty())return {};
    if(!url.isLocalFile())return url.adjusted(QUrl::NormalizePathSegments|QUrl::RemoveFragment).toString(QUrl::FullyEncoded);
    QFileInfo info(url.toLocalFile());auto path=info.canonicalFilePath();
    if(path.isEmpty())path=info.absoluteFilePath();
#ifdef Q_OS_WIN
    path=path.toCaseFolded();
#endif
    return QDir::cleanPath(path);
}
struct CompletedImport {
    webimport::Candidate candidate;
    Project project;
    MapProjection projection;
    QString error;
};
}
// Only owner-thread state lives here. The worker has a separate output box;
// getters never access it before the runner's future-completion synchronization.
struct WebImportSession {
    ProjectSnapshot base;
    qulonglong editEpoch;
    QUrl source;
    QString sourceIdentity;
    std::optional<JobTicket> ticket;
    std::shared_ptr<CompletedImport> ready;
    bool busy=true;
    WebImportSession(ProjectSnapshot snapshot,qulonglong epoch,QUrl url)
        :base(std::move(snapshot)),editEpoch(epoch),source(std::move(url)),sourceIdentity(sourceKey(source)) {}
};
bool EditorController::webImportBusy() const {return webImport_&&webImport_->busy;}
bool EditorController::hasWebImportPreview() const {return webImport_&&static_cast<bool>(webImport_->ready);}
QString EditorController::webImportHash() const {return hasWebImportPreview()?webImport_->ready->candidate.candidateHash:QString();}
QVariantList EditorController::webImportReport() const {return hasWebImportPreview()?webImport_->ready->candidate.report:QVariantList();}
QString EditorController::webImportSummary() const {
    if(webImportBusy())return QStringLiteral("웹 저장본 읽기·변환·검증 중…");
    if(!hasWebImportPreview())return webImportError_.isEmpty()?QStringLiteral("웹 완전 저장본을 선택하세요."):QStringLiteral("가져오기 후보를 준비하지 못했습니다.");
    const auto& c=webImport_->ready->candidate;
    return QStringLiteral("웹 v%1 → 앱 v10 · 최상위 일반객체 %2 · 하위 일반객체 %3 · 독립 권역 %4\n검증된 정적 프로젝트를 가져옵니다.")
        .arg(c.sourceSchema).arg(c.countries).arg(c.subunits).arg(c.regions);
}
void EditorController::webImportFailure(const QString& message) {
    webImportError_=message;
    emit webImportChanged();emit errorOccurred(message+QStringLiteral("\n현재 문서·선택·초안·이력은 유지됩니다."));
}
bool EditorController::isProtectedWebSource(const QUrl& url) const {
    const auto key=sourceKey(url);
    return !key.isEmpty()&&((!protectedWebSource_.isEmpty()&&key==protectedWebSource_)||(webImport_&&key==webImport_->sourceIdentity));
}
void EditorController::cancelWebImport() {
    auto old=std::move(webImport_);webImport_.reset();webImportError_.clear();
    if(old&&old->ticket)jobs_->cancel(old->ticket->id());
    emit webImportChanged();
}
bool EditorController::prepareWebImport(const QUrl& url) {
    if(url.isEmpty())return true; // native file chooser cancellation is a no-op
    try {
        auto session=std::make_shared<WebImportSession>(project_.snapshot(),importEditEpoch_,url);
        auto work=std::make_shared<CompletedImport>();
        const auto storage=storage_;
        // The existing latest-wins runner bounds physical concurrency to one.
        // Source I/O, migration, validation and projection all operate on values.
        auto task=[work,storage,url](const ProjectSnapshot&,const JobToken& token) {
            PrepareResult result;
            try {
                if(token.cancelled())return result;
                auto bytes=storage.read(url);
                work->candidate=webimport::prepare(bytes,[token](){return token.cancelled();});
                if(token.cancelled())return result;
                requireStaticTimeline(work->candidate.document);
                work->project.replace(std::move(work->candidate.document));
                work->projection.rebuild(work->project.document());
                result.status=CommandStatus::Prepared;
            } catch(const std::exception& e) {work->error=QString::fromUtf8(e.what());result.error=CommandError::PrepareFailed;}
            return result;
        };
        auto completion=[this,session,work](std::uint64_t,JobDisposition disposition,PrepareResult result) {
            if(webImport_!=session)return;
            session->busy=false;session->ticket.reset();
            if(disposition!=JobDisposition::Accepted||!session->base.matches(project_)||session->editEpoch!=importEditEpoch_) {
                webImport_.reset();webImportFailure(QStringLiteral("STALE_RESULT: 준비 중 편집 상태가 바뀌었거나 작업이 취소되었습니다. 다시 가져오세요."));return;
            }
            if(!result.ok()||!work->error.isEmpty()) {
                webImport_.reset();webImportFailure(work->error.isEmpty()?QStringLiteral("PREPARE_FAILED: 가져오기 후보를 준비하지 못했습니다."):work->error);return;
            }
            session->ready=work;emit webImportChanged();
        };
        // Enqueue first so an allocation failure cannot erase an existing report.
        // Runner completion is queued, never inline during submit.
        auto ticket=jobs_->submit(session->base,"import:web",std::move(task),std::move(completion),100);
        auto old=webImport_;session->ticket=std::move(ticket);webImport_=std::move(session);webImportError_.clear();
        if(old&&old->ticket)jobs_->cancel(old->ticket->id());
        emit webImportChanged();return true;
    } catch(const std::exception& e) {webImportFailure(QStringLiteral("PREPARE_FAILED: ")+QString::fromUtf8(e.what()));return false;}
}
bool EditorController::confirmWebImport(const QString& hash,const QString& disposition,const QUrl& saveUrl) {
    if(disposition=="cancel") {cancelWebImport();return false;}
    const auto session=webImport_;
    if(!session||!session->ready) {webImportFailure(QStringLiteral("PREVIEW_CONSUMED: 확정할 가져오기 보고서가 없습니다."));return false;}
    if(!session->base.matches(project_)||session->editEpoch!=importEditEpoch_) {
        webImport_.reset();webImportFailure(QStringLiteral("STALE_RESULT: 보고서 이후 편집 상태가 바뀌었습니다. 다시 가져오세요."));return false;
    }
    if(hash!=session->ready->candidate.candidateHash) {webImportFailure(QStringLiteral("CANDIDATE_MISMATCH: 검토한 후보와 일치하지 않습니다."));return false;}
    if(disposition!="discard"&&disposition!="save") {webImportFailure(QStringLiteral("PENDING_EDITS: 기존 작업을 저장할지 버릴지 명시적으로 선택하세요."));return false;}
    QString nextProtection,nextLayer;
    try {
        // Allocate all replacement strings before the optional filesystem write.
        nextProtection=session->sourceIdentity;nextLayer=QStringLiteral("countries");
        if(disposition=="save") {
            if(mobileMode_&&privateRecoveryRequired_)throw std::runtime_error("SAVE_FAILED: 보존 중인 기기 파일의 복구 허용이 필요합니다.");
            QUrl target=saveUrl;
            if(target.isEmpty())target=QUrl::fromLocalFile(mobileMode_?storage_.privateProjectPath():filePath_);
            if(target.isEmpty()||!target.isLocalFile())throw std::runtime_error("SAVE_FAILED: 기존 작업을 저장할 로컬 파일을 선택하세요.");
            if(isProtectedWebSource(target))throw std::runtime_error("SOURCE_OVERWRITE_BLOCKED: 웹 원본 파일에는 쓰지 않습니다. 다른 저장 위치를 선택하세요.");
            CommandArguments args;
            if(!collectPendingEdits(args))return false;
            auto request=CommandProcessor::makeRequest(project_,"edit.properties",std::move(args));
            auto staged=CommandProcessor::prepare(project_,request);
            if(!staged.ok())throw std::runtime_error(std::string("SAVE_FAILED: ")+commandErrorCode(staged.error)+" "+staged.detail);
            Project outgoing;
            outgoing.replace(staged.preview?staged.preview->change().after():project_.document());
            auto bytes=projectcodec::encode(outgoing);
            // This is intentionally not save(): save() commits live drafts first.
            // A failed write here must leave live revision/dirty/history untouched.
            storage_.write(target,bytes);
        }
        if(webImport_!=session||!session->base.matches(project_)||session->editEpoch!=importEditEpoch_) {
            webImport_.reset();webImportFailure(QStringLiteral("STALE_RESULT: 저장 도중 편집 상태가 바뀌어 교체하지 않았습니다."));return false;
        }
    } catch(const std::exception& e) {
        auto message=QString::fromUtf8(e.what());
        if(!message.startsWith("SAVE_FAILED")&&!message.startsWith("SOURCE_OVERWRITE_BLOCKED"))message="SAVE_FAILED: "+message;
        webImportFailure(message);return false;
    }
    auto ready=session->ready;
    // Finish old-session work before replacing the document. QML can read
    // properties synchronously whenever these cancellation signals fire.
    cancelPreview();
    // All data/indices/views/projection are already validated and allocated.
    cancelWorldBootstrap();
    project_=std::move(ready->project);projection_=std::move(ready->projection);
    protectedWebSource_.swap(nextProtection);filePath_.clear();selectedLayer_.swap(nextLayer);
    importedDirty_=true;webImport_.reset();webImportError_.clear();
    // Reconcile selection before notifying the view of new geometry.
    publish(false);emit geometryChanged();emit webImportChanged();return true;

}
