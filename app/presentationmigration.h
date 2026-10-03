#pragma once
#include "losslessjson.h"
#include <pandoeditor/document.h>
#include <algorithm>

namespace presentationmigration {
inline bool group(const std::string& key){return key=="countries"||key=="subunits"||key=="regions";}
inline bool contentGroup(const std::string& key){return key=="labels"||key=="rivers"||key=="lakes"||key=="distributions"||key=="genericFeatures";}
inline bool symbol(const std::string& key){return key=="basemapLabels"||key=="countryFlags"||key=="subunitLabels"||key=="subunitFlags"||key=="regionLabels"||key=="regionFlags";}
inline pandoeditor::PresentationStyle style(losslessjson::Value& v) {
    using V=losslessjson::Value;pandoeditor::PresentationStyle s;
    losslessjson::require(v.kind==V::Object,"INVALID_PRESENTATION_STYLE");
    auto take=[&](const char* key,auto assign){auto i=v.object.find(key);if(i!=v.object.end()){assign(i->second);v.object.erase(i);}};
    take("opacity",[&](const V& n){losslessjson::require(n.kind==V::Number,"INVALID_PRESENTATION_OPACITY");s.opacity=n.raw.toDouble();});
    take("boundaryVisible",[&](const V& n){losslessjson::require(n.kind==V::Bool,"INVALID_PRESENTATION_BOUNDARY");s.boundaryVisible=n.raw=="true";});
    take("colorVisible",[&](const V& n){losslessjson::require(n.kind==V::Bool,"INVALID_PRESENTATION_COLOR");s.colorVisible=n.raw=="true";});
    take("labelsVisible",[&](const V& n){losslessjson::require(n.kind==V::Bool,"INVALID_PRESENTATION_LABELS");s.labelsVisible=n.raw=="true";});
    take("boundaryWidth",[&](const V&){s.boundaryWidth=1;});
    take("blendMode",[&](const V& n){s.blendMode=n.string=="multiply"?"multiply":"normal";});return s;
}
inline std::optional<pandoeditor::ObjectRef> labelOwner(const pandoeditor::ProjectDocument& d,const std::string& key) {
    const auto colon=key.find(':');if(colon==std::string::npos)return {};
    const auto type=key.substr(0,colon),id=key.substr(key.rfind(':')+1);
    if(type=="label"&&std::any_of(d.labels.begin(),d.labels.end(),[&](const auto& v){return v.id==id;}))return pandoeditor::ObjectRef{"label",id};
    if((type=="country"||type=="territorial")&&std::any_of(d.units.begin(),d.units.end(),[&](const auto& v){return v.id==id;}))return pandoeditor::territorialRef(id);
    return {};
}
// Promote supported leaves only; the remainder stays in its original lossless
// extension, with the migration archive kept strictly read-only.
inline void promote(pandoeditor::ProjectDocument& d,bool promoteLabels=true,bool promoteDistribution=true) {
    using namespace pandoeditor;using V=losslessjson::Value;
    auto& p=d.presentation.webPresentation;
    for(auto i=d.extensions.begin();i!=d.extensions.end();) {
        if(i->status!="unsupported"||(i->jsonPointer!="/layerVisibility"&&i->jsonPointer!="/itemVisibility"&&i->jsonPointer!="/layerPresentation"&&i->jsonPointer!="/labelSettings"&&i->jsonPointer!="/distributionSettings")){++i;continue;}
        auto root=losslessjson::parse(QByteArray::fromStdString(i->payload));losslessjson::require(root.kind==V::Object,"INVALID_PRESENTATION_ROOT");
        if(i->jsonPointer=="/layerVisibility") {
            for(auto v=root.object.begin();v!=root.object.end();)if(group(v->first)||contentGroup(v->first)||symbol(v->first)){losslessjson::require(v->second.kind==V::Bool,"INVALID_VISIBILITY");p.visibility[v->first]=v->second.raw=="true";v=root.object.erase(v);}else ++v;
        }else if(i->jsonPointer=="/itemVisibility") {
            for(auto v=root.object.begin();v!=root.object.end();)if(group(v->first)||contentGroup(v->first)){
                losslessjson::require(v->second.kind==V::Object,"INVALID_ITEM_VISIBILITY");for(const auto& [id,flag]:v->second.object){losslessjson::require(flag.kind==V::Bool,"INVALID_ITEM_VISIBILITY");if(flag.raw=="false")p.hiddenItems[v->first].insert(id);}v=root.object.erase(v);
            }else ++v;
        }else if(i->jsonPointer=="/layerPresentation") {
            for(auto name:{"styles","objectStyles"})if(auto entries=root.object.find(name);entries!=root.object.end()) {
                for(auto v=entries->second.object.begin();v!=entries->second.object.end();) {
                    const bool supported=std::string(name)=="styles"?(group(v->first)||contentGroup(v->first)):v->first.rfind("territorial:subunit:",0)==0||v->first.rfind("territorial:region:",0)==0;
                    if(supported){auto value=style(v->second);(std::string(name)=="styles"?p.styles:p.objectStyles)[v->first]=value;if(v->second.object.empty()){v=entries->second.object.erase(v);continue;}}++v;
                }
            }
            if(auto order=root.object.find("objectOrder");order!=root.object.end()) {
                auto& a=order->second.array;a.erase(std::remove_if(a.begin(),a.end(),[&](const V& key){if(key.kind==V::String&&(key.string.rfind("territorial:subunit:",0)==0||key.string.rfind("territorial:region:",0)==0)){p.objectOrder.push_back(key.string);return true;}return false;}),a.end());
            }
        }else if(i->jsonPointer=="/labelSettings") {
            if(!promoteLabels){++i;continue;}
            for(auto value=root.object.begin();value!=root.object.end();) {
                auto owner=labelOwner(d,value->first);if(!owner){++value;continue;}
                losslessjson::require(value->second.kind==V::Object,"INVALID_LABEL_SETTINGS");auto& raw=value->second;pandoeditor::LabelSettings s;
                auto takeNumber=[&](const char* key,std::optional<double>& target){auto n=raw.object.find(key);if(n!=raw.object.end()){losslessjson::require(n->second.kind==V::Number,"INVALID_LABEL_SETTINGS");target=n->second.raw.toDouble();raw.object.erase(n);}};
                takeNumber("priority",s.priority);takeNumber("minZoom",s.minZoom);takeNumber("maxZoom",s.maxZoom);
                if(auto n=raw.object.find("manualPosition");n!=raw.object.end()){losslessjson::require(n->second.kind==V::Array&&n->second.array.size()>=2,"INVALID_LABEL_SETTINGS");s.manualPosition=pandoeditor::Point{n->second.array[0].raw.toDouble(),n->second.array[1].raw.toDouble()};raw.object.erase(n);}
                if(auto n=raw.object.find("pinned");n!=raw.object.end()){losslessjson::require(n->second.kind==V::Bool,"INVALID_LABEL_SETTINGS");s.pinned=n->second.raw=="true";raw.object.erase(n);}
                if(auto n=raw.object.find("collisionGroup");n!=raw.object.end()){losslessjson::require(n->second.kind==V::String,"INVALID_LABEL_SETTINGS");s.collisionGroup=n->second.string.empty()?"map":n->second.string;raw.object.erase(n);}
                p.labelSettings[*owner]=std::move(s);if(raw.object.empty())value=root.object.erase(value);else ++value;
            }
        }else {
            if(!promoteDistribution){++i;continue;}
            if(auto mode=root.object.find("renderMode");mode!=root.object.end()){losslessjson::require(mode->second.kind==V::String&&(mode->second.string=="overlap"||mode->second.string=="single"||mode->second.string=="dominant"||mode->second.string=="intensity"),"INVALID_DISTRIBUTION_SETTINGS");p.distributionSettings.renderMode=mode->second.string=="single"?pandoeditor::DistributionRenderMode::Single:pandoeditor::DistributionRenderMode::Overlap;root.object.erase(mode);}
            if(auto active=root.object.find("activeLayerId");active!=root.object.end()){losslessjson::require(active->second.kind==V::String,"INVALID_DISTRIBUTION_SETTINGS");p.distributionSettings.activeLayerId=active->second.string;root.object.erase(active);}
            if(auto boundary=root.object.find("boundaryVisible");boundary!=root.object.end()){losslessjson::require(boundary->second.kind==V::Bool,"INVALID_DISTRIBUTION_SETTINGS");p.distributionSettings.boundaryVisible=boundary->second.raw=="true";root.object.erase(boundary);}
        }
        if(root.object.empty())i=d.extensions.erase(i);else{i->payload=root.encode().toStdString();++i;}
    }
    normalizePresentation(d);validatePresentation(d);
}
}
