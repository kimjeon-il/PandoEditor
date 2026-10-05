#pragma once
#include <pandoeditor/geometryoperations.h>
#include <QByteArray>
#include <QJsonObject>
#include <QString>
namespace pandoeditor {
enum class CutGeometryStatus { Completed, Cancelled, Failed };
struct CutGeometryResult {
    CutGeometryStatus status=CutGeometryStatus::Failed;
    QJsonObject result;
    QByteArray json;
    QString detail;
    bool inputUnchanged=false;
    bool succeeded() const noexcept {return status==CutGeometryStatus::Completed;}
};
// Fresh private engine on the calling worker thread. The complete pinned worker
// result is owned JSON, with raw candidate ordering/IDs/coordinates preserved.
// A completed result may be pending or invalid: inspect result.valid/status.
// Synchronous JS cannot be interrupted mid-call; cancellation wins at boundaries.
CutGeometryResult prepareCutGeometry(const QJsonObject& payload,
    const GeometryCancellation& cancelled={});
}
