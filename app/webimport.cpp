#include "webimport.h"
#include "projectcodec.h"
#include "losslessjson.h"
#include <pandoeditor/project.h>
#include <QCryptographicHash>

namespace webimport {
FileKind classify(const QByteArray& bytes) {
    const auto value=losslessjson::parse(bytes);
    const auto format=value.object.find("format");
    losslessjson::require(format!=value.object.end()&&format->second.kind==losslessjson::Value::String,"UNSUPPORTED_FORMAT");
    if(format->second.string=="pandoeditor-project") {
        const auto version=value.object.find("version");
        losslessjson::require(version!=value.object.end()&&version->second.kind==losslessjson::Value::Number&&version->second.raw=="9","UNSUPPORTED_VERSION: expected Qt v9");
        return FileKind::QtProject;
    }
    losslessjson::require(format->second.string=="pandolab-project-state"||format->second.string=="pandolab-autosave-full"||format->second.string=="pandolab-autosave-delta","UNSUPPORTED_FORMAT");
    const auto version=value.object.find("schemaVersion");
    losslessjson::require(version!=value.object.end()&&version->second.kind==losslessjson::Value::Number&&version->second.raw=="9","UNSUPPORTED_VERSION: expected web v9");
    return format->second.string=="pandolab-autosave-delta"?FileKind::WebDelta:FileKind::WebFull;
}
Candidate prepare(const QByteArray& bytes,const std::function<bool()>& cancelled) {
    auto check=[&](){if(cancelled&&cancelled())throw std::invalid_argument("CANCELLED");};check();
    losslessjson::require(bytes.size()<=64ll*1024*1024,"LIMIT_EXCEEDED: web input exceeds 64 MiB");
    Candidate result;result.document=projectcodec::decodeWeb(bytes);check();
    result.sourceSchema=9;result.sourceFormat="pandolab-project-state";
    result.sourceHash=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
    for(const auto& unit:result.document.units) {
        if(unit.kind==pandoeditor::UnitKind::Regional)++result.regions;
        else if(pandoeditor::isStaticTimeline(result.document)&&!pandoeditor::staticParentRelation(result.document,unit.id).parentId.empty())++result.subunits;
        else ++result.countries;
    }
    pandoeditor::Project validated;validated.replace(result.document);
    const auto encoded=projectcodec::encode(validated.snapshot());
    losslessjson::require(encoded.size()<=64ll*1024*1024,"LIMIT_EXCEEDED: native candidate exceeds 64 MiB");
    result.candidateHash=QString::fromLatin1(QCryptographicHash::hash(encoded,QCryptographicHash::Sha256).toHex());
    result.report.push_back(QVariantMap{{"path",""},{"status","mapped"},{"message",QStringLiteral("웹 v9 객체·시간 기록·전체 형상 archive를 검증했습니다.")}});
    check();return result;
}
}
