#include "cutgeometrycalculator.h"
#include "geometryruntime_p.h"
#include "splitgeometrynormalizer.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QResource>
#include <stdexcept>

static void initializeCutResources() {
    static const bool initialized=[] {Q_INIT_RESOURCE(m973_cut);Q_INIT_RESOURCE(m972_river);return true;}();
    (void)initialized;
}
namespace pandoeditor {
namespace {
struct Cancelled {};
void check(const GeometryCancellation& cancelled) {if(cancelled&&cancelled())throw Cancelled{};}
QByteArray verified(const QString& path,const QByteArray& hash) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error(("CUT_RESOURCE_UNAVAILABLE: "+path).toStdString());
    const auto bytes=file.readAll();
    if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!=hash)
        throw std::runtime_error(("CUT_RESOURCE_HASH_MISMATCH: "+path).toStdString());
    return bytes;
}
void jsCheck(const QJSValue& value) {
    if(value.isError())throw std::runtime_error(("CUT_JAVASCRIPT_ERROR: "+value.toString()+"\n"+value.property("stack").toString()).toStdString());
}
void verifyModules(const GeometryCancellation& cancelled) {
    const auto bytes=verified(":/cut/provenance.json","9a447083454e9e382fe4fee7560956794ae42aafa85eec0a1b78fd2bd7204a43");
    const auto modules=QJsonDocument::fromJson(bytes).object()["modules"].toArray();
    if(modules.size()!=16)throw std::runtime_error("CUT_MANIFEST_MODULE_COUNT");
    for(const auto& value:modules){check(cancelled);const auto row=value.toObject();
        verified(":/cut/original/"+row["name"].toString(),row["originalSha256"].toString().toLatin1());
        verified(":/cut/adapted/"+row["name"].toString(),row["adaptedSha256"].toString().toLatin1());
        verified(":/cut/"+row["correctedOriginalPath"].toString(),row["correctedOriginalSha256"].toString().toLatin1());
        verified(":/cut/corrected-adapted/"+row["name"].toString(),row["correctedSha256"].toString().toLatin1());
    }
}
CutGeometryResult stopped() {CutGeometryResult output;output.status=CutGeometryStatus::Cancelled;return output;}
}
CutGeometryResult prepareCutGeometry(const QJsonObject& payload,const GeometryCancellation& cancelled) {
    try {
        check(cancelled);initializeCutResources();verifyModules(cancelled);
        // This is the previously browser-verified ES5 redundant-var D3 adapter.
        // Original D3, arithmetic, projection and pinned cut algorithms are intact.
        verified(":/river/original/d3.min.js","4cdf92091ed0cfdd8b862af1c6d4744bd0458e746b92c1bbd5403a1143ecd538");
        const auto d3=verified(":/river/adapted/d3.min.js","f273f409d8b1ba4c35d98fb06ca56f06726359d792ad719400d0e46a8c15cd85");
        check(cancelled);QJSEngine engine;engine.globalObject().setProperty("globalThis",engine.globalObject());
        jsCheck(engine.evaluate(QString::fromUtf8(verified(":/river/platform.js","0438e2d97b9869348f6b660bd94d45cc839e69fb1d3fb4db91c9a5650dc45412")),":/river/platform.js"));
        jsCheck(engine.evaluate(QString::fromUtf8(verified(":/cut/platform.js","59c674d9618b4f1f01627a78d54f222d2f69e0a5bd5705d71a5118eaf09d15cb")),":/cut/platform.js"));
        check(cancelled);jsCheck(engine.evaluate(QString::fromUtf8(d3),":/river/adapted/d3.min.js"));
        check(cancelled);loadPinnedPolygonClipping(engine);
        loadApprovedSplitPolygonGeometry(engine);
        check(cancelled);const auto module=engine.importModule(":/cut/corrected-adapted/cut-worker-preparation.js");check(cancelled);jsCheck(module);
        if(!module.property("prepareCutInWorker").isCallable())throw std::runtime_error("CUT_KERNEL_UNAVAILABLE");
        const auto json=engine.globalObject().property("JSON");
        const auto input=json.property("parse").call({QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))});
        check(cancelled);jsCheck(input);
        const auto before=json.property("stringify").call({input});jsCheck(before);
        const auto value=module.property("prepareCutInWorker").call({input,
            engine.globalObject().property("PandoLabPolygonGeometry"),engine.globalObject().property("d3"),
            engine.globalObject().property("polygonClipping")});
        check(cancelled);jsCheck(value);
        const auto after=json.property("stringify").call({input});jsCheck(after);
        if(before.toString()!=after.toString())throw std::runtime_error("CUT_INPUT_MUTATED");
        const auto serialized=json.property("stringify").call({value});check(cancelled);jsCheck(serialized);
        QJsonParseError error;const auto bytes=serialized.toString().toUtf8();const auto document=QJsonDocument::fromJson(bytes,&error);
        check(cancelled);if(error.error!=QJsonParseError::NoError||!document.isObject())throw std::runtime_error("CUT_INVALID_RESULT_JSON");
        const auto result=document.object();
        if(!result["line"].isArray()||!result["snaps"].isObject()||!result["status"].isString()||!result["valid"].isBool()||
           !result["message"].isString()||!result["issues"].isArray())throw std::runtime_error("CUT_INVALID_RESULT_SHAPE");
        CutGeometryResult output;output.result=result;output.json=bytes;output.inputUnchanged=true;
        check(cancelled);output.status=CutGeometryStatus::Completed;return output;
    }catch(const Cancelled&){return stopped();}
    catch(const std::exception& error){if(cancelled&&cancelled())return stopped();CutGeometryResult output;output.detail=QString::fromUtf8(error.what());return output;}
}
}
