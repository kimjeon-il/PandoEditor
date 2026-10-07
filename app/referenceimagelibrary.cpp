#include "referenceimagelibrary.h"
#include "referencetracing.h"

#include <algorithm>
#include <cmath>
#include <QJSEngine>
#include <QJSValue>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace {
QString normalizedBlend(QString value)
{
    value = value.toLower();
    return value == "multiply" || value == "screen" || value == "difference" ? value : QStringLiteral("normal");
}
QString normalizedWarp(QString value)
{
    value = value.toLower();
    return value == "similarity" || value == "affine" || value == "projective" || value == "tps" ? value : QStringLiteral("auto");
}
ReferenceWarpMode warpMode(const QString &mode)
{
    if (mode == "similarity") return ReferenceWarpMode::Similarity;
    if (mode == "affine") return ReferenceWarpMode::Affine;
    if (mode == "projective") return ReferenceWarpMode::Projective;
    if (mode == "tps") return ReferenceWarpMode::ThinPlateSpline;
    return ReferenceWarpMode::Auto;
}
}

ReferenceImageLibrary::ReferenceImageLibrary(QObject *parent) : QObject(parent), imageModel_(this)
{
    // Publish model changes before consumers of the existing images API observe
    // each preview, cancellation, reload, or history transition.
    connect(this, &ReferenceImageLibrary::imagesChanged, this, [this] { imageModel_.setRows(images()); });
    connect(this,&ReferenceImageLibrary::imagesChanged,this,&ReferenceImageLibrary::calibrationSessionChanged);
    reload();
}

QString ReferenceImageLibrary::directory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/reference-images");
}
QString ReferenceImageLibrary::indexPath() const { return directory() + QStringLiteral("/library.json"); }

QVariantMap ReferenceImageLibrary::variant(const Record &r, const QString &root)
{
    return {{"id",r.id},{"name",r.name},{"source",QUrl::fromLocalFile(root+"/"+r.fileName)},
        {"visible",r.visible},{"locked",r.locked},{"opacity",r.opacity},{"x",r.x},{"y",r.y},
        {"width",r.width},{"height",r.height},{"rotation",r.rotation},{"flipX",r.flipX},{"flipY",r.flipY},
        {"blend",r.blend},{"warpMode",r.warpMode},{"controlPoints",r.controlPoints},{"geographicPoints",r.geographicPoints}};
}
QVariantList ReferenceImageLibrary::images() const
{
    QVariantList result;
    for (const auto &item : state_) {
        auto value=variant(item,directory());
        if(!item.geographicPoints.empty()) {
            auto input=value;input["controlPoints"]=item.geographicPoints;
            const auto mapping=calibration(input);value["calibrationMesh"]=mapping["mesh"];
        }
        result.push_back(value);
    }
    return result;
}

QJsonObject ReferenceImageLibrary::json(const Record &r)
{
    return {{"id",r.id},{"name",r.name},{"fileName",r.fileName},{"visible",r.visible},{"locked",r.locked},
        {"opacity",r.opacity},{"x",r.x},{"y",r.y},{"width",r.width},{"height",r.height},{"rotation",r.rotation},
        {"flipX",r.flipX},{"flipY",r.flipY},{"blend",r.blend},{"warpMode",r.warpMode},
        {"controlPoints",QJsonArray::fromVariantList(r.controlPoints)},{"geographicPoints",QJsonArray::fromVariantList(r.geographicPoints)}};
}
std::optional<ReferenceImageLibrary::Record> ReferenceImageLibrary::record(const QJsonObject &value)
{
    Record r; r.id=value["id"].toString();r.name=value["name"].toString();r.fileName=value["fileName"].toString();
    if(r.id.isEmpty()||r.fileName.isEmpty()||QFileInfo(r.fileName).fileName()!=r.fileName)return std::nullopt;
    r.visible=value["visible"].toBool(true);r.locked=value["locked"].toBool();r.opacity=std::clamp(value["opacity"].toDouble(1),0.,1.);
    r.x=value["x"].toDouble();r.y=value["y"].toDouble();r.width=value["width"].toDouble();r.height=value["height"].toDouble();r.rotation=value["rotation"].toDouble();
    r.flipX=value["flipX"].toBool();r.flipY=value["flipY"].toBool();r.blend=normalizedBlend(value["blend"].toString());r.warpMode=normalizedWarp(value["warpMode"].toString());r.controlPoints=value["controlPoints"].toArray().toVariantList();r.geographicPoints=value["geographicPoints"].toArray().toVariantList();return r;
}

void ReferenceImageLibrary::fail(const QString &message) { if(lastError_==message)return;lastError_=message;emit lastErrorChanged(); }
bool ReferenceImageLibrary::save(const State &state)
{
    QDir().mkpath(directory()); QJsonArray records; for(const auto &item:state)records.append(json(item));
    QSaveFile file(indexPath());if(!file.open(QIODevice::WriteOnly)){fail(file.errorString());return false;}
    file.write(QJsonDocument(QJsonObject{{"schemaVersion",1},{"images",records}}).toJson(QJsonDocument::Indented));
    if(!file.commit()){fail(file.errorString());return false;} fail({});return true;
}
bool ReferenceImageLibrary::setState(State state,bool history)
{
    if(!save(state))return false;if(history){undo_.push_back(state_);if(undo_.size()>50)undo_.removeFirst();redo_.clear();}
    state_=std::move(state);emit imagesChanged();emit historyChanged();return true;
}
bool ReferenceImageLibrary::reload()
{
    QFile file(indexPath());if(!file.exists()){state_.clear();emit imagesChanged();return true;}
    if(!file.open(QIODevice::ReadOnly)){fail(file.errorString());return false;}QJsonParseError error;const auto document=QJsonDocument::fromJson(file.readAll(),&error);if(error.error!=QJsonParseError::NoError||!document.isObject()){fail(QStringLiteral("참조 이미지 목록이 손상되었습니다."));return false;}
    State loaded;for(const auto &value:document.object()["images"].toArray())if(auto parsed=record(value.toObject());parsed&&QFile::exists(directory()+"/"+parsed->fileName))loaded.push_back(*parsed);
    state_=std::move(loaded);undo_.clear();redo_.clear();fail({});emit imagesChanged();emit historyChanged();return true;
}
bool ReferenceImageLibrary::importImage(const QUrl &source,const QString &requestedName)
{
    const QString path=source.toLocalFile();QImageReader reader(path);reader.setAutoTransform(true);const QImage image=reader.read();if(image.isNull()){fail(QStringLiteral("PNG/JPEG/WebP 이미지를 읽을 수 없습니다."));return false;}
    const QString id=QUuid::createUuid().toString(QUuid::WithoutBraces),extension=QFileInfo(path).suffix().toLower();if(extension!="png"&&extension!="jpg"&&extension!="jpeg"&&extension!="webp"){fail(QStringLiteral("지원하지 않는 이미지 형식입니다."));return false;}
    QDir().mkpath(directory());const QString fileName=id+"."+extension,destination=directory()+"/"+fileName;QSaveFile copy(destination);QFile input(path);if(!input.open(QIODevice::ReadOnly)||!copy.open(QIODevice::WriteOnly)||copy.write(input.readAll())<0||!copy.commit()){fail(QStringLiteral("이미지를 안전하게 복사하지 못했습니다."));return false;}
    Record record;record.id=id;record.name=requestedName.trimmed().isEmpty()?QFileInfo(path).completeBaseName():requestedName.trimmed();record.fileName=fileName;record.width=image.width();record.height=image.height();auto next=state_;next.push_back(record);if(!setState(std::move(next),true)){QFile::remove(destination);return false;}return true;
}
bool ReferenceImageLibrary::removeImage(const QString &id)
{
    auto next=state_;const auto found=std::find_if(next.begin(),next.end(),[&](const auto&i){return i.id==id;});if(found==next.end()||found->locked)return false;next.erase(found);return setState(std::move(next),true);
}
bool ReferenceImageLibrary::updateImage(const QString &id,const QVariantMap &changes)
{
    auto next=state_;const auto found=std::find_if(next.begin(),next.end(),[&](const auto&i){return i.id==id;});if(found==next.end()||(found->locked&&changes.keys()!=QStringList{"locked"}))return false;
    if(changes.contains("name"))found->name=changes["name"].toString().trimmed();if(changes.contains("visible"))found->visible=changes["visible"].toBool();if(changes.contains("locked"))found->locked=changes["locked"].toBool();if(changes.contains("opacity"))found->opacity=std::clamp(changes["opacity"].toDouble(),0.,1.);if(changes.contains("x"))found->x=changes["x"].toDouble();if(changes.contains("y"))found->y=changes["y"].toDouble();if(changes.contains("width"))found->width=std::max(1.,changes["width"].toDouble());if(changes.contains("height"))found->height=std::max(1.,changes["height"].toDouble());if(changes.contains("rotation"))found->rotation=changes["rotation"].toDouble();if(changes.contains("flipX"))found->flipX=changes["flipX"].toBool();if(changes.contains("flipY"))found->flipY=changes["flipY"].toBool();if(changes.contains("blend"))found->blend=normalizedBlend(changes["blend"].toString());if(changes.contains("warpMode"))found->warpMode=normalizedWarp(changes["warpMode"].toString());if(changes.contains("controlPoints"))found->controlPoints=changes["controlPoints"].toList();return setState(std::move(next),true);
}
bool ReferenceImageLibrary::moveImage(const QString &id,int destination)
{
    auto next=state_;const auto found=std::find_if(next.begin(),next.end(),[&](const auto&i){return i.id==id;});if(found==next.end()||found->locked)return false;const int from=int(found-next.begin());destination=std::clamp(destination,0,int(next.size())-1);if(from==destination)return true;const auto value=*found;next.removeAt(from);next.insert(destination,value);return setState(std::move(next),true);
}
bool ReferenceImageLibrary::beginGesture(const QString &id)
{if(gestureBefore_)return false;const auto item=find(id);if(!item||item->locked)return false;gestureBefore_=state_;gestureId_=id;return true;}
bool ReferenceImageLibrary::updateGesture(const QVariantMap &changes)
{if(!gestureBefore_)return false;const auto found=std::find_if(state_.begin(),state_.end(),[&](const auto& item){return item.id==gestureId_;});if(found==state_.end())return false;if(changes.contains("x"))found->x=changes["x"].toDouble();if(changes.contains("y"))found->y=changes["y"].toDouble();if(changes.contains("width"))found->width=std::max(1.,changes["width"].toDouble());if(changes.contains("height"))found->height=std::max(1.,changes["height"].toDouble());if(changes.contains("rotation"))found->rotation=changes["rotation"].toDouble();emit imagesChanged();return true;}
bool ReferenceImageLibrary::commitGesture()
{if(!gestureBefore_)return false;const auto before=std::move(*gestureBefore_);gestureBefore_.reset();gestureId_.clear();if(!save(state_)){state_=before;emit imagesChanged();return false;}undo_.push_back(before);if(undo_.size()>50)undo_.removeFirst();redo_.clear();emit historyChanged();return true;}
void ReferenceImageLibrary::cancelGesture(){if(!gestureBefore_)return;state_=std::move(*gestureBefore_);gestureBefore_.reset();gestureId_.clear();emit imagesChanged();}
bool ReferenceImageLibrary::undo(){if(undo_.isEmpty())return false;auto previous=undo_.last();if(!save(previous))return false;undo_.removeLast();redo_.push_back(state_);state_=std::move(previous);emit imagesChanged();emit historyChanged();return true;}
bool ReferenceImageLibrary::redo(){if(redo_.isEmpty())return false;auto next=redo_.last();if(!save(next))return false;redo_.removeLast();undo_.push_back(state_);state_=std::move(next);emit imagesChanged();emit historyChanged();return true;}
QVariantMap ReferenceImageLibrary::solveWarp(const QString &id,const QVariantList &values,const QString &mode) const
{
    if(std::none_of(state_.begin(),state_.end(),[&](const auto&i){return i.id==id;}))return {{"valid",false},{"error",QStringLiteral("이미지를 찾을 수 없습니다.")}};QVector<ReferenceControlPoint> points;for(const auto &value:values){const auto map=value.toMap();points.push_back({{map["sourceX"].toDouble(),map["sourceY"].toDouble()},{map["destinationX"].toDouble(),map["destinationY"].toDouble()}});}const auto result=solveReferenceWarp(warpMode(normalizedWarp(mode)),points);return {{"valid",result.valid},{"error",result.error},{"rms",result.rmsError},{"maximum",result.maximumError},{"resolvedMode",int(result.resolvedMode)}};
}
const ReferenceImageLibrary::Record *ReferenceImageLibrary::find(const QString &id) const
{const auto found=std::find_if(state_.begin(),state_.end(),[&](const auto& item){return item.id==id;});return found==state_.end()?nullptr:&*found;}
QVariantList ReferenceImageLibrary::traceLine(const QString &id,double startX,double startY,double endX,double endY) const
{QVariantList result;const auto item=find(id);if(!item)return result;QImageReader reader(directory()+"/"+item->fileName);reader.setAutoTransform(true);const auto image=reader.read();for(const auto& point:referenceLiveWire(image,{qRound(startX),qRound(startY)},{qRound(endX),qRound(endY)}))result.push_back(QVariantMap{{"x",point.x()},{"y",point.y()}});return result;}

QVariantMap ReferenceImageLibrary::calibration(const QVariantMap &record) const
{
    // One immutable production module shared with the pinned Web, with a fresh
    // engine per calculation. No script functions escape this thread.
    QJSEngine engine;
    QFile script(":/reference-web/runtime.js");
    if(!script.open(QIODevice::ReadOnly))return {{"ok",false},{"reason",script.errorString()}};
    const auto module=engine.evaluate(QString::fromUtf8(script.readAll())+";ReferenceWeb",script.fileName());
    if(module.isError())return {{"ok",false},{"reason",module.toString()}};
    auto function=module.property("calibration");
    const auto json=QString::fromUtf8(QJsonDocument::fromVariant(record).toJson(QJsonDocument::Compact));
    const auto argument=engine.globalObject().property("JSON").property("parse").call({json});
    const auto result=function.call({argument});
    if(result.isError())return {{"ok",false},{"reason",result.toString()}};
    return result.toVariant().toMap();
}
bool ReferenceImageLibrary::setCalibration(const QString &id,const QVariantList &points,const QString &mode)
{
    auto next=state_;
    auto item=std::find_if(next.begin(),next.end(),[&](const auto &r){return r.id==id;});
    if(item==next.end()||item->locked||gestureBefore_)return false;
    item->geographicPoints=points;item->warpMode=normalizedWarp(mode);
    return setState(std::move(next),true);
}

QVariantMap ReferenceImageLibrary::calibrationSession() const
{
    const auto item=find(calibrationId_);if(!item)return {};
    auto record=variant(*item,directory());
    record["controlPoints"]=item->geographicPoints;
    return {{"active",true},{"record",record},{"pendingMap",pendingUv_.has_value()},
        {"editingPointId",editingPointId_},{"result",calibration(record)}};
}
bool ReferenceImageLibrary::beginCalibration(const QString &id)
{
    const auto item=find(id);if(!item||item->locked||gestureBefore_)return false;
    calibrationId_=id;editingPointId_.clear();pendingUv_.reset();emit calibrationSessionChanged();return true;
}
void ReferenceImageLibrary::cancelCalibration()
{calibrationId_.clear();editingPointId_.clear();pendingUv_.reset();emit calibrationSessionChanged();}
bool ReferenceImageLibrary::pickImagePoint(double u,double v)
{
    const auto item=find(calibrationId_);
    if(!item||item->locked||!std::isfinite(u)||!std::isfinite(v)||u<0||u>1||v<0||v>1)return false;
    pendingUv_=QPointF(u,v);editingPointId_.clear();emit calibrationSessionChanged();return true;
}
bool ReferenceImageLibrary::pickMapCoordinate(double longitude,double latitude)
{
    const auto item=find(calibrationId_);
    if(!item||item->locked||!pendingUv_||!std::isfinite(longitude)||!std::isfinite(latitude)||longitude< -180||longitude>180||latitude< -90||latitude>90)return false;
    auto points=item->geographicPoints;
    const QString id=editingPointId_.isEmpty()?QUuid::createUuid().toString(QUuid::WithoutBraces):editingPointId_;
    QVariantMap point{{"id",id},{"image",QVariantList{pendingUv_->x(),pendingUv_->y()}},{"coordinate",QVariantList{longitude,latitude}}};
    bool replaced=false;for(auto &value:points)if(value.toMap()["id"].toString()==id){value=point;replaced=true;break;}
    if(!replaced)points.append(point);
    if(!setCalibration(calibrationId_,points,item->warpMode))return false;
    pendingUv_.reset();editingPointId_.clear();emit calibrationSessionChanged();return true;
}
bool ReferenceImageLibrary::editCalibrationPoint(const QString &id)
{
    const auto item=find(calibrationId_);if(!item||item->locked)return false;
    for(const auto &value:item->geographicPoints){const auto point=value.toMap();if(point["id"].toString()!=id)continue;
        const auto uv=point["image"].toList();if(uv.size()!=2)return false;
        editingPointId_=id;pendingUv_=QPointF(uv[0].toDouble(),uv[1].toDouble());emit calibrationSessionChanged();return true;}
    return false;
}
bool ReferenceImageLibrary::deleteCalibrationPoint(const QString &id)
{
    const auto item=find(calibrationId_);if(!item||item->locked)return false;
    auto points=item->geographicPoints;auto end=std::remove_if(points.begin(),points.end(),[&](const auto &v){return v.toMap()["id"].toString()==id;});
    if(end==points.end())return false;points.erase(end,points.end());
    if(!setCalibration(calibrationId_,points,item->warpMode))return false;
    pendingUv_.reset();editingPointId_.clear();emit calibrationSessionChanged();return true;
}
bool ReferenceImageLibrary::clearCalibrationPoints()
{const auto item=find(calibrationId_);if(!item||!setCalibration(calibrationId_,{},item->warpMode))return false;pendingUv_.reset();editingPointId_.clear();emit calibrationSessionChanged();return true;}
bool ReferenceImageLibrary::setCalibrationMode(const QString &mode)
{const auto item=find(calibrationId_);return item&&setCalibration(calibrationId_,item->geographicPoints,mode);}
