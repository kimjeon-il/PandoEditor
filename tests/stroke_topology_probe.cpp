#include "../renderer/stroketopology.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <limits>
#include <iostream>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);if(argc!=2)return 2;
    QFile file(QString::fromLocal8Bit(argv[1]));if(!file.open(QIODevice::ReadOnly))return 3;
    QJsonParseError error;const auto input=QJsonDocument::fromJson(file.readAll(),&error);
    if(error.error!=QJsonParseError::NoError)return 4;
    QJsonArray results;
    for(const auto& value:input.object().value("cases").toArray()) {
        const auto row=value.toObject();std::vector<float> values;
        for(const auto& n:row.value("segments").toArray())values.push_back(n.isString()?std::numeric_limits<float>::quiet_NaN():float(n.toDouble()));
        const auto segments=stroke::chains(values);QJsonArray instances,nodes;int joins=0,caps=0,closed=0;
        const auto point=[](QJsonArray& out,stroke::Point p){out.append(double(p[0]));out.append(double(p[1]));};
        const auto node=[&](stroke::Point previous,stroke::Point at,stroke::Point next,float phase,int kind) {
            point(nodes,previous);point(nodes,at);point(nodes,next);nodes.append(double(phase));nodes.append(kind);if(kind==1)++joins;else ++caps;
        };
        for(std::size_t i=0;i<segments.size();++i) {
            const auto& s=segments[i];point(instances,s.previous);point(instances,s.start);point(instances,s.end);point(instances,s.next);instances.append(double(s.phase));instances.append(int(s.flags));
            node(s.previous,s.start,s.end,s.phase,(s.flags&1)?1:2);
            if(!(s.flags&2))node(s.start,s.end,s.end,s.phase,3);
            if((s.flags&16)&&(i+1==segments.size()||!stroke::equal(s.end,segments[i+1].start)))++closed;
        }
        results.append(QJsonObject{{"id",row.value("id")},{"instances",instances},{"nodes",nodes},{"segmentCount",int(segments.size())},{"nodeCount",nodes.size()/8},{"joinCount",joins},{"capCount",caps},{"closedChainCount",closed},{"invalidSegmentCount",int(values.size()/4-segments.size())}});
    }
    std::cout<<QJsonDocument(QJsonObject{{"cases",results},{"expected",results.size()},{"processed",results.size()}}).toJson(QJsonDocument::Compact).constData()<<'\n';return 0;
}
