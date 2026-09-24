#include "hydroloadscheduler.h"
#include <QFutureWatcher>
#include <QtConcurrent>
#include <exception>
#include <new>

void HydroLoadScheduler::resetDataset(const QString& projectInstance) {
    ++generation_;revision_=0;projectInstance_=projectInstance;
    frame_.reset();
}
quint64 HydroLoadScheduler::requestViewport(Job job) {
    const auto generation=generation_,revision=++revision_;
    const auto project=projectInstance_;
    auto* watcher=new QFutureWatcher<std::shared_ptr<const HydroRuntimeFrame>>(this);
    connect(watcher,&QFutureWatcher<std::shared_ptr<const HydroRuntimeFrame>>::finished,this,
            [this,watcher,generation,revision,project]{
        const auto next=watcher->result();watcher->deleteLater();
        if(generation!=generation_||revision!=revision_||project!=projectInstance_)return;
        if(!next||!next->error.isEmpty()){
            emit loadFailed(next?next->error:QStringLiteral("수계 로드 결과가 없습니다."));return;
        }
        frame_=next;emit frameAccepted();
    });
    watcher->setFuture(QtConcurrent::run([job=std::move(job)]() -> std::shared_ptr<const HydroRuntimeFrame> {
        try{return job();}
        catch(const std::bad_alloc&){return {};}
        catch(const std::exception& exception){
            try{auto failed=std::make_shared<HydroRuntimeFrame>();failed->error=QString::fromUtf8(exception.what());return failed;}
            catch(const std::bad_alloc&){return {};}
        }catch(...){
            try{auto failed=std::make_shared<HydroRuntimeFrame>();failed->error=QStringLiteral("수계 디코딩에 실패했습니다.");return failed;}
            catch(const std::bad_alloc&){return {};}
        }
    }));
    return revision;
}
