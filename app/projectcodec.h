#pragma once
#include <pandoeditor/project.h>
#include <QByteArray>

namespace projectcodec {
pandoeditor::ProjectDocument decode(const QByteArray& data);
pandoeditor::ProjectDocument decodeWeb(const QByteArray& data);
QByteArray encode(const pandoeditor::Project& project);
QByteArray encode(const pandoeditor::ProjectSnapshot& snapshot);
QByteArray encodeWeb(const pandoeditor::ProjectSnapshot& snapshot);
}
