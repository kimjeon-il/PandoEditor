#pragma once
#include <pandoeditor/document.h>
#include <QByteArray>
#include <QString>
#include <QVariantList>
#include <functional>

namespace webimport {
enum class FileKind { QtProject, WebFull };
struct Migration {
    QByteArray normalized;
    QString sourceFormat;
    int sourceSchema=0;
};
struct Candidate {
    pandoeditor::ProjectDocument document;
    QVariantList report;
    QString sourceFormat, sourceHash, candidateHash;
    int sourceSchema=0;
    int countries=0, subunits=0, regions=0;
};
FileKind classify(const QByteArray& bytes);
Migration migrate(const QByteArray& bytes);
// GeoPackage project IDs may predate the UUID rule used by complete JSON saves.
// Only the independently validated package path opts into legacy IDs.
Candidate prepare(const QByteArray& bytes, const std::function<bool()>& cancelled={},
                  bool allowLegacyIds=false);
}
