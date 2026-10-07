#include "territorialcatalogadapter.h"
#include "historicaltransaction.h"
#include "geometrycalculator.h"
#include "projectcodec.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <iostream>

// Diagnostic of the unchanged actual snapshot transaction, not acceptance or
// an alternate backend. No fixtures/expected/timeout are rewritten.
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        if(argc!=3&&argc!=4)throw std::runtime_error("INDEX SAMPLE_PROJECT [DIAGNOSTIC_OUTPUT] required");
        const auto read=[](const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("input unreadable");return f.readAll();};
        const QString path=QString::fromLocal8Bit(argv[1]);
        const auto bytes=read(path);
        const QByteArray pin="63c072095fd6c95034365f6d7f9d4e4ff99f71684890bffc8e678fe741b4334c";
        pandoeditor::TerritorialLibraryCatalog catalog(bytes,pin,QFileInfo(path).absolutePath());
        pandoeditor::Project project;project.replace(projectcodec::decode(read(QString::fromLocal8Bit(argv[2]))));
        QElapsedTimer total;total.start();QElapsedTimer stage;stage.start();
        auto additions=pandoeditor::territorialCatalogSelections(catalog,project.snapshot(),
            {"state:czechoslovakia","state:soviet-union"},"1991","none",{});
        for(std::size_t i=0;i<additions.size();++i)additions[i].instanceId="timing-instance-"+std::to_string(i);
        std::cout<<"actual catalog adapter ms="<<stage.nsecsElapsed()/1e6<<" additions="<<additions.size()<<std::endl;
        int operations=0;const auto actual=pandoeditor::makeTransactionGeometryCalculator();
        const pandoeditor::GeometryCalculator measured=[&](const auto& request,const auto& cancelled){
            const auto operands=request.operands.empty()?std::vector<pandoeditor::Geometry>{request.left,request.right}:request.operands;
            QElapsedTimer validation;validation.start();std::size_t points=0;
            for(const auto& operand:operands) {
                for(const auto& polygon:operand.polygons)for(const auto& ring:polygon)points+=ring.size();
                pandoeditor::GeometryStore check;check.insert({"diagnostic",1},operand);
            }
            std::cout<<"independent operand validation ms="<<validation.nsecsElapsed()/1e6<<" points="<<points<<std::endl;
            QElapsedTimer elapsed;elapsed.start();auto result=actual(request,cancelled);
            std::cout<<"actual boolean stage="<<++operations<<" operation="<<int(request.operation)
                <<" ms="<<elapsed.nsecsElapsed()/1e6<<" status="<<int(result.status)<<std::endl;return result;
        };
        stage.restart();auto plan=pandoeditor::prepareHistoricalTransaction(project.snapshot(),std::move(additions),measured);
        std::cout<<"actual transaction total ms="<<stage.nsecsElapsed()/1e6<<std::endl;
        pandoeditor::CommandArguments args;args.action=plan;stage.restart();
        auto prepared=pandoeditor::CommandProcessor::prepare(project,pandoeditor::CommandProcessor::makeRequest(project,"historical.instantiate",std::move(args)));
        std::cout<<"actual command prepare ms="<<stage.nsecsElapsed()/1e6<<" total ms="<<total.nsecsElapsed()/1e6
            <<" processed=1 failed="<<(!prepared.ok()||!prepared.preview)<<" skip=0 detail="<<prepared.detail<<std::endl;
        if(argc==4&&prepared.ok()&&prepared.preview) {
            const auto applied=pandoeditor::CommandProcessor::confirm(project,*prepared.preview);
            if(!applied.ok())throw std::runtime_error(applied.detail);
            QFile output(QString::fromLocal8Bit(argv[3]));
            if(output.exists()||!output.open(QIODevice::WriteOnly))throw std::runtime_error("Preserve diagnostic output");
            const auto canonical=projectcodec::encode(project);
            if(output.write(canonical)!=canonical.size())throw std::runtime_error("Diagnostic write failed");
        }
        return prepared.ok()&&prepared.preview?0:1;
    }catch(const std::exception& e){std::cerr<<"snapshot diagnostic FAIL: "<<e.what()<<std::endl;return 1;}
}
