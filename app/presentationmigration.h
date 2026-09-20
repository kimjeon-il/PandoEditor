#pragma once
#include "losslessjson.h"
#include <pandoeditor/document.h>
#include <algorithm>

namespace presentationmigration {
inline bool group(const std::string& key){return key=="countries"||key=="subunits"||key=="regions";}
inline bool symbol(const std::string& key){return key=="basemapLabels"||key=="countryFlags"||key=="subunitLabels"||key=="subunitFlags"||key=="regionLabels"||key=="regionFlags";}
inline pandoeditor::PresentationStyle style(losslessjson::Value& v) {
    using V=losslessjson::Value;pandoeditor::PresentationStyle s;
    losslessjson::require(v.kind==V::Object,"INVALID_PRESENTATION_STYLE");
    auto take=[&](const char* key,auto assign){auto i=v.object.find(key);if(i!=v.object.end()){assign(i->second);v.object.erase(i);}};
    take("opacity",[&](const V& n){losslessjson::require(n.kind==V::Number,"INVALID_PRESENTATION_OPACITY");s.opacity=n.raw.toDouble();});
    take("boundaryVisible",[&](const V& n){losslessjson::require(n.kind==V::Bool,"INVALID_PRESENTATION_BOUNDARY");s.boundaryVisible=n.raw=="true";});
    take("labelsVisible",[&](const V& n){losslessjson::require(n.kind==V::Bool,"INVALID_PRESENTATION_LABELS");s.labelsVisible=n.raw=="true";});
    take("boundaryWidth",[&](const V&){s.boundaryWidth=1;});
    take("blendMode",[&](const V& n){s.blendMode=n.string=="multiply"?"multiply":"normal";});return s;
}
// Promote supported leaves only; the remainder stays in its original lossless
// extension, with the migration archive kept strictly read-only.
inline void promote(pandoeditor::ProjectDocument& d) {
    using namespace pandoeditor;using V=losslessjson::Value;
    auto& p=d.presentation.webPresentation;
    for(auto i=d.extensions.begin();i!=d.extensions.end();) {
        if(i->status!="unsupported"||(i->jsonPointer!="/layerVisibility"&&i->jsonPointer!="/itemVisibility"&&i->jsonPointer!="/layerPresentation")){++i;continue;}
        auto root=losslessjson::parse(QByteArray::fromStdString(i->payload));losslessjson::require(root.kind==V::Object,"INVALID_PRESENTATION_ROOT");
        if(i->jsonPointer=="/layerVisibility") {
            for(auto v=root.object.begin();v!=root.object.end();)if(group(v->first)||symbol(v->first)){losslessjson::require(v->second.kind==V::Bool,"INVALID_VISIBILITY");p.visibility[v->first]=v->second.raw=="true";v=root.object.erase(v);}else ++v;
        }else if(i->jsonPointer=="/itemVisibility") {
            for(auto v=root.object.begin();v!=root.object.end();)if(group(v->first)){
                losslessjson::require(v->second.kind==V::Object,"INVALID_ITEM_VISIBILITY");for(const auto& [id,flag]:v->second.object){losslessjson::require(flag.kind==V::Bool,"INVALID_ITEM_VISIBILITY");if(flag.raw=="false")p.hiddenItems[v->first].insert(id);}v=root.object.erase(v);
            }else ++v;
        }else {
            for(auto name:{"styles","objectStyles"})if(auto entries=root.object.find(name);entries!=root.object.end()) {
                for(auto v=entries->second.object.begin();v!=entries->second.object.end();) {
                    const bool supported=std::string(name)=="styles"?group(v->first):v->first.rfind("territorial:subunit:",0)==0||v->first.rfind("territorial:region:",0)==0;
                    if(supported){auto value=style(v->second);(std::string(name)=="styles"?p.styles:p.objectStyles)[v->first]=value;if(v->second.object.empty()){v=entries->second.object.erase(v);continue;}}++v;
                }
            }
            if(auto order=root.object.find("objectOrder");order!=root.object.end()) {
                auto& a=order->second.array;a.erase(std::remove_if(a.begin(),a.end(),[&](const V& key){if(key.kind==V::String&&(key.string.rfind("territorial:subunit:",0)==0||key.string.rfind("territorial:region:",0)==0)){p.objectOrder.push_back(key.string);return true;}return false;}),a.end());
            }
        }
        if(root.object.empty())i=d.extensions.erase(i);else{i->payload=root.encode().toStdString();++i;}
    }
    normalizePresentation(d);validatePresentation(d);
}
}
