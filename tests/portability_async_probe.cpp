#include "commandjobrunner.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSemaphore>
#include <QTimer>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace pandoeditor;
static ProjectDocument document(const std::string& name) {
    return ProjectDocument({{"A",name,{{{{0,0},{8,0},{8,8},{0,8},{0,0}}}},0xcccccc}},{{"countries","Countries"}});
}
struct Gate {QSemaphore started,release;};
struct Release {std::shared_ptr<Gate> gate;~Release(){gate->release.release();}};
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);QFile input;if(!input.open(stdin,QIODevice::ReadOnly))return 2;
    QJsonParseError error;const auto parsed=QJsonDocument::fromJson(input.readAll(),&error);
    if(error.error!=QJsonParseError::NoError||!parsed.isArray())return 3;
    QJsonArray output;
    try {
        for(const auto value:parsed.array()) {
            const auto row=value.toObject();const auto interruption=row["interruption"].toString();
            Project project;project.replace(document("Alpha"));
            CommandJobRunner runner([&]()->const Project&{return project;});
            auto gate=std::make_shared<Gate>();Release release{gate};
            std::string outcome="pending";
            const auto observe=[&]{return QJsonObject{{"outcome",QString::fromStdString(outcome)},
                {"name",QString::fromStdString(project.country("A")->name)},{"undo",project.canUndo()}};};
            CommandArguments args;args.action=TerritorialFieldEdit{territorialRef("A"),TerritorialField::Name,"Beta"};
            const auto request=CommandProcessor::makeRequest(project,"territorial.field",args);
            const auto ticket=runner.submit(project.snapshot(),"rename",[gate,request,interruption](const ProjectSnapshot& snapshot,const JobToken&)->PrepareResult {
                gate->started.release();gate->release.acquire();
                if(interruption=="failure")throw std::runtime_error("fixture worker failure");
                return CommandProcessor::prepare(snapshot,request);
            },[&](auto,auto disposition,PrepareResult result){
                outcome=jobDispositionCode(disposition);
                if(disposition==JobDisposition::Accepted) {
                    if(!result.preview||!CommandProcessor::confirm(project,*result.preview).ok())outcome="failed";
                }
            });
            if(!gate->started.tryAcquire(1,5000))throw std::runtime_error("Worker start timeout");
            QJsonArray steps{observe()};
            if(interruption=="cancel")runner.cancel(ticket.id());
            else if(interruption=="revision")project.setMemo("A","new revision");
            else if(interruption=="replace")project.replace(document("Replacement"));
            else if(interruption!="none"&&interruption!="failure")throw std::runtime_error("Unknown async interruption");
            QEventLoop loop;QTimer watchdog;watchdog.setSingleShot(true);bool expired=false;
            QObject::connect(&watchdog,&QTimer::timeout,&loop,[&]{expired=true;loop.quit();});
            QObject::connect(&runner,&CommandJobRunner::changed,&loop,[&]{if(!runner.runningCount())loop.quit();});
            gate->release.release();watchdog.start(5000);if(runner.runningCount())loop.exec();
            if(expired)throw std::runtime_error("Worker completion timeout");
            steps.append(observe());output.append(QJsonObject{{"id",row["id"]},{"steps",steps}});
        }
    }catch(const std::exception& error){std::cerr<<error.what();return 4;}
    std::cout<<QJsonDocument(output).toJson(QJsonDocument::Compact).constData();
}
