#include "defaultflagresolver.h"
#include <QFile>
#include <QRegularExpression>
#include <algorithm>
#include <map>
#include <set>

using namespace pandoeditor;
namespace {
const std::map<QString,QString>& codes() {
    static const auto values=[] {
        std::map<QString,QString> result;QFile source(":/defaults/flags/country-flags.js");
        if(!source.open(QIODevice::ReadOnly))return result;
        const QString text=QString::fromUtf8(source.readAll());
        QRegularExpression pattern(QStringLiteral("\\b([A-Z]{3}):\\s*'([a-z]{2})'"));auto matches=pattern.globalMatch(text);
        while(matches.hasNext()){const auto match=matches.next();result[match.captured(1)]=match.captured(2);}return result;
    }();return values;
}
}
DefaultFlagResult resolveDefaultFlag(const ProjectDocument& document,const ObjectRef& ref) {
    const auto owner=document.symbols.find(ref);
    if(owner!=document.symbols.end()) {
        if(owner->second.policy==FlagPolicy::None)return {{},QStringLiteral("사용 안 함"),false};
        if(owner->second.policy==FlagPolicy::Embedded)return {QString::fromStdString(owner->second.embeddedDataUrl),{},true};
    }
    const auto unit=std::find_if(document.units.begin(),document.units.end(),[&](const auto& value){return ref==territorialRef(value.id);});
    if(unit==document.units.end())return {{},QStringLiteral("대상 없음"),false};
    const auto id=QString::fromStdString(unit->id).toUpper();
    if(id=="CYN"||id=="SOL")return {QStringLiteral("qrc:/defaults/flags/political/")+id.toLower()+".svg",{},true};
    if(unit->kind!=UnitKind::Country)return {{},QStringLiteral("기본 국기 자료 없음"),false};
    const auto code=codes().find(id);if(code==codes().end())return {{},QStringLiteral("기본 국기 자료 없음"),false};
    static const std::set<QString> legacy={"cd","sm","ga","pg"};
    return {QStringLiteral("qrc:/defaults/flags/")+(legacy.count(code->second)?"legacy/":"native/")+code->second+".svg",{},true};
}
