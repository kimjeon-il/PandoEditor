#include "editorcontroller.h"
#include <QGuiApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

void checkAt(bool value,int line) { if (!value) throw std::runtime_error("check failed at line "+std::to_string(line)); }
#define check(value) checkAt((value),__LINE__)
int main(int argc,char** argv)
{
    QGuiApplication application(argc,argv);
    try {
        QFile input(":/assets/sample.pando.json"); check(input.open(QIODevice::ReadOnly));
        auto sample=input.readAll();
        pandoeditor::Project project;
        project.replace(projectcodec::decode(sample));
        check(project.countries().size()==5);
        auto encoded=projectcodec::encode(project);
        pandoeditor::Project restored; restored.replace(projectcodec::decode(encoded));
        check(projectcodec::encode(restored)==encoded);
        MapProjection projection; projection.rebuild(project.countries());
        EditorController editor;
        // Locate a guaranteed German interior point via the shared inverse projection.
        bool selected=false;
        for (int y=0;y<100 && !selected;++y) for(int x=0;x<100 && !selected;++x) {
            editor.selectAt(editor.mapWidth()*x/100,editor.mapHeight()*y/100);
            selected=editor.selectedId()=="DEU";
        }
        check(selected);
        editor.setColor("#ff0000"); check(editor.dirty());
        QTemporaryDir dir; check(dir.isValid());
        const auto path=QUrl::fromLocalFile(dir.path()+QString::fromUtf8("/지도 프로젝트.pando.json"));
        check(editor.saveFile(path) && !editor.dirty());
        editor.undo(); check(editor.dirty()); editor.redo(); check(!editor.dirty());
        editor.setColor("#00ff00"); check(editor.dirty());
        const auto before=editor.colors();
        const auto invalid=QUrl::fromLocalFile(dir.path()+"/invalid.json");
        auto reject=[&](QByteArray bytes) {
            QFile file(invalid.toLocalFile()); check(file.open(QIODevice::WriteOnly)); file.write(bytes); file.close();
            check(!editor.openFile(invalid)); check(editor.colors()==before && editor.dirty() && editor.canUndo());
        };
        reject("broken");
        auto root=QJsonDocument::fromJson(sample).object(); root["version"]=99; reject(QJsonDocument(root).toJson());
        root=QJsonDocument::fromJson(sample).object();
        auto countries=root["countries"].toArray(); auto c=countries[1].toObject(); c["id"]=countries[0].toObject()["id"]; countries[1]=c; root["countries"]=countries; reject(QJsonDocument(root).toJson());
        root=QJsonDocument::fromJson(sample).object(); countries=root["countries"].toArray(); c=countries[0].toObject(); c["color"]="#xyzxyz"; countries[0]=c; root["countries"]=countries; reject(QJsonDocument(root).toJson());
        root=QJsonDocument::fromJson(sample).object(); countries=root["countries"].toArray(); c=countries[0].toObject();
        c["geometry"]=QJsonObject{{"type","MultiPolygon"},{"coordinates",QJsonArray{QJsonArray{QJsonArray{QJsonArray{181,0},QJsonArray{1,1},QJsonArray{2,0},QJsonArray{181,0}}}}}};
        countries[0]=c; root["countries"]=countries; reject(QJsonDocument(root).toJson());
        check(!editor.saveFile(QUrl::fromLocalFile(dir.path()+"/missing/project.json")) && editor.dirty());
        check(editor.openFile(path));
        check(!editor.dirty() && !editor.canUndo() && editor.selectedId().isEmpty());
        check(editor.colors()["DEU"].toString()=="#ff0000");
        editor.selectCountry("DEU");
        editor.setNameDraft("  새 독일  ");
        editor.setMemoDraft("한글 메모\n둘째 줄");
        editor.setColorDraft("#123456");
        editor.previewCountryOpacity(0.6);
        editor.previewCountryOpacity(0.35);
        check(editor.dirty() && !editor.canUndo()); // drafts do not create commands
        check(editor.saveFile(path));
        check(editor.selectedName()=="새 독일" && editor.countryOpacity()==0.35);
        editor.undo(); check(editor.countryOpacity()==1 && editor.dirty());
        editor.redo(); check(editor.countryOpacity()==0.35 && !editor.dirty());
        editor.addLayer();
        auto layerId=editor.selectedLayerId();
        check(layerId!="countries" && editor.canDeleteLayer());
        editor.setLayerNameDraft("  테스트 레이어  ");
        editor.previewLayerOpacity(0.45);
        check(editor.commitPendingEdits());
        editor.moveCountry(layerId);
        check(editor.countryLayerId()==layerId && !editor.canDeleteLayer());
        editor.setLayerLocked(true);
        check(editor.selectedId()=="DEU" && !editor.selectedEditable()); // lock is not deselection
        editor.selectCountry("DEU");
        check(!editor.selectedEditable());
        editor.setColor("#ffffff");
        check(editor.colors()["DEU"].toString()=="#123456");
        editor.setLayerLocked(false);
        editor.selectCountry("DEU");
        editor.previewCountryOpacity(0);
        check(editor.commitPendingEdits());
        editor.selectCountry("DEU"); check(editor.selectedEditable() && editor.countryOpacity()==0);
        editor.setLayerVisible(false); check(editor.selectedId()=="DEU" && !editor.countryVisuals()["DEU"].toMap()["visible"].toBool());
        editor.setLayerVisible(true);
        check(editor.saveFile(path));
        QFile saved(path.toLocalFile()); check(saved.open(QIODevice::ReadOnly));
        auto v2=QJsonDocument::fromJson(saved.readAll()).object(); saved.close();
        check(v2["version"].toInt()==7 && v2["presentation"].toObject()["userLayers"].toArray().size()==2);
        editor.selectCountry("DEU"); editor.setMemoDraft("changed"); check(editor.commitPendingEdits());
        const auto protectedColors=editor.colors();
        auto rejectV2=[&](QJsonObject obj) {
            QFile file(invalid.toLocalFile()); check(file.open(QIODevice::WriteOnly));
            file.write(QJsonDocument(obj).toJson()); file.close();
            check(!editor.openFile(invalid) && editor.colors()==protectedColors && editor.dirty());
            check(editor.memoDraft()=="changed");
        };
        auto badV2=v2; auto presentation=badV2["presentation"].toObject();
        auto members=presentation["membership"].toArray();
        auto member=members[0].toObject(); member["layerId"]="missing"; members[0]=member;
        presentation["membership"]=members; badV2["presentation"]=presentation; rejectV2(badV2);
        badV2=v2; presentation=badV2["presentation"].toObject();
        auto layerArray=presentation["userLayers"].toArray();
        auto layerObject=layerArray[1].toObject(); layerObject["id"]=layerArray[0].toObject()["id"]; layerArray[1]=layerObject;
        presentation["userLayers"]=layerArray; badV2["presentation"]=presentation; rejectV2(badV2);
        badV2=v2; presentation=badV2["presentation"].toObject(); layerArray=presentation["userLayers"].toArray();
        layerObject=layerArray[0].toObject(); layerObject["opacity"]=1.5; layerArray[0]=layerObject;
        presentation["userLayers"]=layerArray; badV2["presentation"]=presentation; rejectV2(badV2);
        badV2=v2; presentation=badV2["presentation"].toObject();
        auto styles=presentation["objectStyles"].toObject(); auto territorial=styles["territorial"].toObject();
        auto style=territorial["DEU"].toObject(); style["opacity"]=-0.2; territorial["DEU"]=style;
        styles["territorial"]=territorial; presentation["objectStyles"]=styles; badV2["presentation"]=presentation; rejectV2(badV2);
        check(editor.openFile(path)); editor.selectCountry("DEU");
        check(editor.memoDraft()=="한글 메모\n둘째 줄" && editor.selectedName()=="새 독일");
        check(editor.countryLayerId()==layerId && editor.countryOpacity()==0);
        check(!editor.dirty());
        // Empty names are valid in the source web; invalid legacy layer name tests failed save.
        editor.setLayerNameDraft("   ");
        check(!editor.saveFile(path));
        check(editor.selectedName()=="새 독일");
        std::cout << "JSON round trip, Korean path, failed open/save and editor tests passed\n";
    } catch(const std::exception& e) { std::cerr << e.what(); return 1; }
}
