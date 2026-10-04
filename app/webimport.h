#pragma once
#include <pandoeditor/document.h>
#include <QByteArray>
#include <QString>
#include <QVariantList>
#include <functional>

namespace webimport {
enum class FileKind { QtProject, WebFull, WebDelta };
struct Candidate {
    pandoeditor::ProjectDocument document;
    QVariantList report;
    QString sourceFormat, sourceHash, candidateHash;
    int sourceSchema=0;
    int countries=0, subunits=0, regions=0;
};
FileKind classify(const QByteArray& bytes);
Candidate prepare(const QByteArray& bytes, const std::function<bool()>& cancelled={});
}
