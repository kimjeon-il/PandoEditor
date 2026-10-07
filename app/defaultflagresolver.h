#pragma once
#include <pandoeditor/document.h>
#include <QString>

struct DefaultFlagResult { QString source, reason; bool available=false; };
DefaultFlagResult resolveDefaultFlag(const pandoeditor::ProjectDocument&,const pandoeditor::ObjectRef&);
QString resolveDefaultFlagSource(const QString& source);
