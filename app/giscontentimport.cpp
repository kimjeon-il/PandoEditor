#include "giscontentimport.h"
#include "webjson.h"
#include <QString>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace pandoeditor {
namespace {
using V=losslessjson::Value;
void require(bool ok,const char* error) {if(!ok)throw std::invalid_argument(error);}
std::string field(const V& object,const std::string& key) {
    if(key.empty())return {};
    const auto& value=webjson::at(object,key);
    if(value.kind==V::Null)return {};
    require(value.kind==V::String||value.kind==V::Number,"INVALID_GIS_FIELD");
    return webjson::text(value);
}
std::uint32_t color(const V& props,std::uint32_t fallback) {
    const auto input=field(props,"color");if(input.empty())return fallback;
    require(input.size()==7&&input[0]=='#'&&
        std::all_of(input.begin()+1,input.end(),[](unsigned char c){return std::isxdigit(c)!=0;}),
        "INVALID_GIS_COLOR");
    bool ok=false;const auto result=QString::fromStdString(input.substr(1)).toUInt(&ok,16);
    require(ok&&result<=0xffffff,"INVALID_GIS_COLOR");return result;
}
std::string id(const GisGeoJsonFeature& feature,const V& props,
    const GisContentMapping& mapping,const std::string& fallback) {
    if(mapping.idField=="__fid__")return feature.id;
    if(!mapping.idField.empty())return field(props,mapping.idField);
    if(!fallback.empty()) {
        const auto value=field(props,fallback);if(!value.empty())return value;
    }
    if(!feature.id.empty())return feature.id;
    return field(props,"id");
}
Validity validity(const V& props) {
    Validity value;auto from=field(props,"valid_from"),to=field(props,"valid_to");
    if(!from.empty())value.from=std::move(from);
    if(!to.empty())value.to=std::move(to);
    return value;
}
void target(const std::string& declared,GisExchangeTarget requested) {
    if(declared.empty())return; // Third-party ZIP without a Pando manifest.
    require(declared==(requested==GisExchangeTarget::Generic?"generic":"distribution"),
        "GIS_LAYER_TARGET_MISMATCH");
}
}
GisContentImport planGisContentImport(const ProjectSnapshot& project,
    const GisGeoJsonCollection& collection,std::string planId,GisSource source,
    const GisContentMapping& mapping) {
    require(mapping.target==GisExchangeTarget::Generic||
        mapping.target==GisExchangeTarget::Distribution,"INVALID_GIS_CONTENT_TARGET");
    if(mapping.target==GisExchangeTarget::Generic) {
        std::vector<GisGenericInput> rows;
        for(const auto& feature:collection.features) {
            const auto props=losslessjson::parse(QByteArray::fromStdString(feature.propertiesJson));
            require(props.kind==V::Object,"INVALID_GIS_PROPERTIES");
            auto identifier=id(feature,props,mapping,"");
            require(!identifier.empty(),"INVALID_GIS_PLAN: generic ID required");
            rows.push_back({std::move(identifier),field(props,mapping.nameField),feature.geometry,
                feature.propertiesJson,field(props,"notes"),color(props,0x888888)});
        }
        return planGenericGisImport(project,std::move(planId),std::move(source),std::move(rows));
    }
    require(mapping.distributionType=="language"||mapping.distributionType=="ethnicity"||
        mapping.distributionType=="religion","INVALID_GIS_DISTRIBUTION_TYPE");
    std::vector<DistributionLayer> layers;
    std::vector<GisDistributionInput> entries;
    std::map<std::string,std::string> layerTypes;
    for(const auto& feature:collection.features) {
        require(feature.geometry.type=="Polygon"||feature.geometry.type=="MultiPolygon",
            "INVALID_GIS_DISTRIBUTION_GEOMETRY");
        const auto props=losslessjson::parse(QByteArray::fromStdString(feature.propertiesJson));
        require(props.kind==V::Object,"INVALID_GIS_PROPERTIES");
        auto identifier=id(feature,props,mapping,"entry_id");
        auto layerId=mapping.layerId.empty()?field(props,mapping.layerIdField):mapping.layerId;
        require(!identifier.empty()&&!layerId.empty(),"INVALID_GIS_DISTRIBUTION_ID");
        const auto declared=field(props,"distribution_type");
        require(declared.empty()||declared==mapping.distributionType,"GIS_DISTRIBUTION_TYPE_MISMATCH");
        auto found=layerTypes.find(layerId);
        if(found==layerTypes.end()) {
            const auto prior=project.index().objects.find({"distributionLayer",layerId});
            if(prior!=project.index().objects.end()) {
                const auto& existing=project.document().distributionLayers.at(prior->second);
                require(existing.type==mapping.distributionType&&!existing.locked,
                    "GIS_DISTRIBUTION_LAYER_CONFLICT");
            } else {
                DistributionLayer layer;layer.id=layerId;layer.type=mapping.distributionType;
                layer.name=mapping.layerName.empty()?field(props,mapping.nameField):mapping.layerName;
                require(!layer.name.empty(),"INVALID_GIS_DISTRIBUTION_NAME");
                layer.color=color(props,0x3388cc);
                layers.push_back(std::move(layer));
            }
            layerTypes.emplace(layerId,mapping.distributionType);
        }
        GisDistributionInput input;input.entry.id=std::move(identifier);
        input.entry.layerId=std::move(layerId);
        const auto mode=field(props,mapping.sourceModeField);
        const auto territory=field(props,mapping.territoryField);
        require(mode.empty()||mode=="territorial"||mode=="geometry",
            "INVALID_GIS_DISTRIBUTION_MODE");
        if(mode=="territorial"||mode.empty()&&!territory.empty()) {
            require(!territory.empty()&&project.index().objects.count(territorialRef(territory)),
                "DANGLING_GIS_DISTRIBUTION_TERRITORY");
            input.entry.territory=territorialRef(territory);
        } else input.geometry=feature.geometry;
        const auto& share=webjson::at(props,"share");
        if(share.kind!=V::Null) {
            require(share.kind==V::Number,"INVALID_GIS_SHARE");
            input.entry.share=webjson::number(share);
        }
        const auto certainty=field(props,"certainty");
        if(!certainty.empty())input.entry.certainty=certainty;
        input.entry.validity=validity(props);
        input.entry.metadata=feature.propertiesJson;
        entries.push_back(std::move(input));
    }
    return planDistributionGisImport(project,std::move(planId),std::move(source),
        std::move(layers),std::move(entries));
}
GisContentImport planGisContentZipImport(const ProjectSnapshot& project,const GisGeoJsonZip& archive,
    std::size_t index,std::string planId,GisSource source,const GisContentMapping& mapping) {
    require(index<archive.layers.size(),"GIS_LAYER_MISSING");
    const auto& layer=archive.layers[index];target(layer.targetType,mapping.target);
    require(mapping.target!=GisExchangeTarget::Distribution||layer.distributionType.empty()||
        mapping.distributionType==layer.distributionType,"GIS_DISTRIBUTION_TYPE_MISMATCH");
    return planGisContentImport(project,layer.collection,std::move(planId),std::move(source),mapping);
}
GisContentImport planGisContentGeoPackageImport(const ProjectSnapshot& project,const GisGeoPackage& archive,
    std::size_t index,std::string planId,GisSource source,const GisContentMapping& mapping) {
    require(index<archive.layers.size(),"GIS_LAYER_MISSING");
    const auto& layer=archive.layers[index];target(layer.targetType,mapping.target);
    return planGisContentImport(project,layer.collection,std::move(planId),std::move(source),mapping);
}
}
