#include "gisdocumentexport.h"
#include "webjson.h"
#include <pandoeditor/objectproperties.h>
#include <QString>
#include <algorithm>
#include <stdexcept>

namespace pandoeditor {
namespace {
using V=losslessjson::Value;
V nullable(const std::optional<std::string>& value) {
    return value?V::str(*value):V{};
}
std::string color(std::uint32_t rgb) {
    return QString("#%1").arg(rgb,6,16,QChar('0')).toStdString();
}
Geometry geometry(const ProjectDocument& document,const GeometryRef& ref) {
    const auto stored=document.geometries.get(ref);
    if(!stored)throw std::invalid_argument("DANGLING_GIS_GEOMETRY");
    return *stored;
}
void append(GisGeoJsonCollection& out,std::string id,Geometry shape,V properties) {
    out.features.push_back({std::move(id),std::move(shape),properties.encode().toStdString()});
}
}
GisGeoJsonCollection exportGisDocumentLayer(const ProjectDocument& document,
                                            const std::string& layer) {
    validateDocument(document);
    GisGeoJsonCollection out;
    if(layer=="countries"||layer=="subunits"||layer=="regions") {
        const auto kind=layer=="countries"?UnitKind::Country:layer=="subunits"?UnitKind::Subunit:UnitKind::Region;
        for(const auto& unit:document.units)if(unit.kind==kind) {
            const auto ref=territorialRef(unit.id);
            const auto relation=baseRelation(document,ref);
            auto properties=webjson::obj({
                {"id",V::str(unit.id)},{"name",V::str(objectDisplayName(unit))},
                {"type",V::str(kind==UnitKind::Country?"country":kind==UnitKind::Subunit?"subunit":"region")},
                {"parent_id",relation&&relation->parent?V::str(relation->parent->id):V{}},
                {"sovereign_id",relation&&relation->sovereign?V::str(relation->sovereign->id):V{}},
                {"valid_from",nullable(unit.validity.from)},{"valid_to",nullable(unit.validity.to)},
                {"color",V::str(color(effectiveObjectColor(document,ref)))},
                {"source_library_id",unit.libraryOrigin?V::str(unit.libraryOrigin->libraryId):V{}},
                {"source_geometry_version",unit.libraryOrigin?V::str(unit.libraryOrigin->geometryVersionId):V{}}
            });
            append(out,unit.id,geometry(document,unit.geometry),std::move(properties));
        }
    } else if(layer=="distributions") {
        for(const auto& entry:document.distributionEntries) {
            const auto layerIt=std::find_if(document.distributionLayers.begin(),document.distributionLayers.end(),
                [&](const auto& item){return item.id==entry.layerId;});
            if(layerIt==document.distributionLayers.end())throw std::invalid_argument("DANGLING_GIS_LAYER");
            Geometry shape;
            if(entry.territory) {
                const auto ref=std::find_if(document.units.begin(),document.units.end(),
                    [&](const auto& unit){return territorialRef(unit.id)==*entry.territory;});
                if(ref==document.units.end())throw std::invalid_argument("DANGLING_GIS_TERRITORY");
                shape=geometry(document,ref->geometry);
            } else if(entry.geometry)shape=geometry(document,*entry.geometry);
            else throw std::invalid_argument("DANGLING_GIS_GEOMETRY");
            auto properties=webjson::obj({
                {"entry_id",V::str(entry.id)},{"layer_id",V::str(entry.layerId)},
                {"distribution_type",V::str(layerIt->type)},
                {"source_mode",V::str(entry.territory?"territorial":"geometry")},
                {"territorial_unit_id",entry.territory?V::str(entry.territory->id):V{}},
                {"share",V::num(entry.share)},{"certainty",V::str(entry.certainty)},
                {"valid_from",nullable(entry.validity.from)},{"valid_to",nullable(entry.validity.to)}
            });
            append(out,entry.id,std::move(shape),std::move(properties));
        }
    } else if(layer=="genericFeatures") {
        for(const auto& item:document.genericFeatures) {
            auto properties=webjson::obj({{"id",V::str(item.id)},{"name",V::str(item.name)},
                {"notes",V::str(item.notes)},{"color",V::str(color(item.color))}});
            properties.object["source_metadata"]=losslessjson::parse(
                QByteArray::fromStdString(item.source.details));
            append(out,item.id,geometry(document,item.geometry),std::move(properties));
        }
    } else if(layer=="labels") {
        for(const auto& item:document.labels) {
            auto properties=webjson::obj({{"id",V::str(item.id)},
                {"name",V::str(item.name)},{"kind",V::str(item.kind)}});
            append(out,item.id,geometry(document,item.geometry),std::move(properties));
        }
    } else throw std::invalid_argument("UNSUPPORTED_GIS_EXPORT_LAYER");
    return out;
}
}
