#pragma once
#include <pandoeditor/project.h>
#include <QByteArray>

namespace projectcodec {
pandoeditor::ProjectDocument decode(const QByteArray& data);
QByteArray encode(const pandoeditor::Project& project);
}
