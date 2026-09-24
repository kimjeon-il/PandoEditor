#pragma once
#include <pandoeditor/project.h>
#include <QByteArray>

namespace projectcodec {
// Atomically promotes each supported retained dependency group. Failed groups
// stay byte-for-byte retained; diagnostics explain why capability stays limited.
std::vector<std::string> promoteContent(pandoeditor::ProjectDocument& document);
pandoeditor::ProjectDocument decode(const QByteArray& data);
QByteArray encode(const pandoeditor::Project& project);
}
