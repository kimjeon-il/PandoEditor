#pragma once
#include <pandoeditor/project.h>
#include <pandoeditor/picking.h>
#include <QVariantList>

// View-only projection and paths; never owns or mutates the project.
class MapProjection {
public:
    void rebuild(const std::vector<pandoeditor::CountryView>& countries);
    void rebuild(const pandoeditor::ProjectDocument& document);
    void setWorldExtent();
    pandoeditor::Point project(pandoeditor::Point point) const;
    pandoeditor::Point unproject(double x,double y) const;
    QVariantMap hydroParameters() const{return {{"cosLatitude",cosLatitude},{"minX",minX},{"maxLatitude",maxLatitude}};}
    QVariantList paths;
    double width=1,height=1;
private:
    double cosLatitude=1,minX=0,maxLatitude=0;
};
