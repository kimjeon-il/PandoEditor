#include "cutgeometrycalculator.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonParseError>
#include <cstdio>
// One JSON payload, or an array of payloads, on stdin. Full owned worker JSON on
// stdout. No fixture lookup, coordinate rounding or canonicalization occurs here.
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);QFile in,out;in.open(stdin,QIODevice::ReadOnly);out.open(stdout,QIODevice::WriteOnly);
    QJsonParseError error;const auto input=QJsonDocument::fromJson(in.readAll(),&error);
    if(error.error!=QJsonParseError::NoError||(!input.isObject()&&!input.isArray()))return 2;
    QJsonArray rows;const bool list=input.isArray();const auto values=list?input.array():QJsonArray{input.object()};
    for(const auto& value:values){const auto result=pandoeditor::prepareCutGeometry(value.toObject());
        if(!result.succeeded()){fprintf(stderr,"%s\n",qPrintable(result.detail));return 3;}
        rows.append(QJsonObject{{"result",result.result},{"inputUnchanged",result.inputUnchanged}});
    }
    out.write((list?QJsonDocument(rows):QJsonDocument(rows[0].toObject())).toJson(QJsonDocument::Compact));out.write("\n");return 0;
}
