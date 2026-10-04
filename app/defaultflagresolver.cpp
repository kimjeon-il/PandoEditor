#include "defaultflagresolver.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
QString currentFlag(const QString& countryId) {
    const auto id=countryId.trimmed().toUpper();
    if(id=="CYN"||id=="SOL")return QStringLiteral("qrc:/defaults/flags/political/")+id.toLower()+".svg";
    const auto code=codes().find(id);if(code==codes().end())return {};
    static const std::set<QString> legacy={"cd","sm","ga","pg"};
    return QStringLiteral("qrc:/defaults/flags/")+(legacy.count(code->second)?"legacy/":"native/")+code->second+".svg";
}
QString normalizedFlag(const QJsonValue& value) {
    return value.isString()&&!value.toString().trimmed().isEmpty()?value.toString():QString{};
}
QJsonObject flagMetadata(const ProjectDocument& document,const ObjectRef& ref) {
    QJsonObject metadata;
    for(const auto& extension:document.extensions) {
        if(extension.status!="unsupported"||
           std::find(extension.dependencies.begin(),extension.dependencies.end(),ref)==extension.dependencies.end())continue;
        const auto path=QString::fromStdString(extension.jsonPointer);
        const auto parsed=QJsonDocument::fromJson("["+QByteArray::fromStdString(extension.payload)+"]").array();
        if(parsed.isEmpty())continue;
        if(path.endsWith("/metadata")&&parsed[0].isObject()) {
            const auto object=parsed[0].toObject();
            for(auto it=object.begin();it!=object.end();++it)metadata[it.key()]=it.value();
        } else for(const auto* field:{"flagDataUrl","defaultFlagDataUrl","convertedFromCountry","builtinSubunit"})
            if(path.endsWith(QStringLiteral("/")+field))metadata[field]=parsed[0];
    }
    return metadata;
}
}
DefaultFlagResult resolveDefaultFlag(const ProjectDocument& document,const ObjectRef& ref) {
    if(ref.domain!="territorial")return {{},QStringLiteral("대상 없음"),false};
    const auto owner=document.symbols.find(ref);
    if(owner!=document.symbols.end()) {
        if(owner->second.policy==FlagPolicy::None)return {{},QStringLiteral("사용 안 함"),false};
        if(owner->second.policy==FlagPolicy::Embedded)return {QString::fromStdString(owner->second.embeddedDataUrl),{},true};
    }
    const auto unit=std::find_if(document.units.begin(),document.units.end(),[&](const auto& value){return ref==territorialRef(value.id);});
    if(unit==document.units.end())return {{},QStringLiteral("대상 없음"),false};
    const auto metadata=flagMetadata(document,ref);
    QString source;
    // Native symbols own an explicitly edited flag; preserved metadata supplies
    // the same defaults as effectiveTerritorialFlagUrl in the web application.
    if(owner==document.symbols.end()&&metadata.contains("flagDataUrl"))source=normalizedFlag(metadata["flagDataUrl"]);
    else if(unit->kind==UnitKind::Country)source=currentFlag(QString::fromStdString(unit->id));
    else if(owner!=document.symbols.end()&&owner->second.defaultFlagDataUrl)source=QString::fromStdString(*owner->second.defaultFlagDataUrl);
    else if(owner!=document.symbols.end()&&!owner->second.defaultCountryId.empty())source=currentFlag(QString::fromStdString(owner->second.defaultCountryId));
    else if(!metadata["defaultFlagDataUrl"].toString().isEmpty())source=normalizedFlag(metadata["defaultFlagDataUrl"]);
    else if(metadata["convertedFromCountry"].isObject()) {
        const auto converted=metadata["convertedFromCountry"].toObject();
        const auto override=converted["override"].toObject();
        source=override.contains("flagDataUrl")?normalizedFlag(override["flagDataUrl"]):currentFlag(converted["countryId"].toString());
    } else source=currentFlag(metadata["builtinSubunit"].toObject()["sourceCountryId"].toString());
    return {source,source.isEmpty()?QStringLiteral("기본 국기 자료 없음"):QString{},!source.isEmpty()};
}
