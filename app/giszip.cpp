#include "giszip.h"
#include "webjson.h"
#include <pandoeditor/giszip.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <stdexcept>

namespace pandoeditor {
namespace {
using V=losslessjson::Value;
void require(bool valid,const char* error) {
    if(!valid)throw std::invalid_argument(error);
}
bool endsWith(const std::string& path,const std::string& suffix) {
    if(path.size()<suffix.size())return false;
    return std::equal(suffix.rbegin(),suffix.rend(),path.rbegin(),[](unsigned char a,unsigned char b) {
        return std::tolower(a)==std::tolower(b);
    });
}
std::string fileName(const std::string& path) {
    return path.substr(path.find_last_of('/')==std::string::npos?0:path.find_last_of('/')+1);
}
}

GisGeoJsonZip parseGisGeoJsonZip(const QByteArray& bytes) {
    const auto archive=readGisZipArchive(
        std::string_view(bytes.constData(),std::size_t(bytes.size())));
    std::map<std::string,const GisZipEntry*> files;
    std::vector<const GisZipEntry*> layerFiles;
    const GisZipEntry* manifestEntry=nullptr;
    for(const auto& entry:archive.entries) {
        if(endsWith(fileName(entry.path),".json")&&
           fileName(entry.path).size()==std::string("manifest.json").size()&&
           endsWith(fileName(entry.path),"manifest.json")) {
            require(!manifestEntry,"DUPLICATE_GIS_MANIFEST");
            manifestEntry=&entry;
        } else if(endsWith(entry.path,".geojson")||endsWith(entry.path,".json")) {
            files.emplace(entry.path,&entry);
            layerFiles.push_back(&entry);
        }
    }
    GisGeoJsonZip result;
    std::map<std::string,V> declared;
    if(manifestEntry) {
        // The browser ignores malformed third-party manifest JSON but rejects
        // unsupported versions of an identified PandoLab export.
        try {
            const auto root=losslessjson::parse(QByteArray::fromStdString(manifestEntry->bytes));
            if(webjson::isTrue(webjson::at(root,"pandolabExport"))) {
                result.webManifest=true;
                require(webjson::number(webjson::at(root,"schemaVersion"))==3,
                        "UNSUPPORTED_GIS_MANIFEST_SCHEMA");
                require(webjson::text(webjson::at(root,"crs"))=="EPSG:4326",
                        "UNSUPPORTED_GIS_CRS");
                for(const auto& layer:webjson::array(webjson::at(root,"layers"),"GIS layers")) {
                    require(layer.kind==V::Object,"INVALID_GIS_MANIFEST_LAYER");
                    auto path=webjson::text(webjson::at(layer,"file"));
                    require(!path.empty()&&files.count(path),"MISSING_GIS_ZIP_LAYER");
                    require(declared.emplace(path,layer).second,"DUPLICATE_GIS_ZIP_LAYER");
                    require(webjson::text(webjson::at(layer,"crs"))=="EPSG:4326",
                            "UNSUPPORTED_GIS_CRS");
                }
            }
        } catch(const std::invalid_argument&) {
            if(result.webManifest)throw;
            // An unrelated, malformed manifest is not an import specification.
        }
    }
    for(const auto* entry:layerFiles) {
        GisZipLayer layer;
        layer.path=entry->path;
        layer.collection=parseGisGeoJson(QByteArray::fromStdString(entry->bytes));
        if(auto it=declared.find(entry->path);it!=declared.end()) {
            const auto& row=it->second;
            layer.category=webjson::text(webjson::at(row,"category"));
            layer.targetType=webjson::text(webjson::at(row,"targetType"));
            layer.distributionType=webjson::text(webjson::at(row,"distributionType"));
            const auto& count=webjson::at(row,"featureCount");
            const double number=webjson::number(count);
            require(count.kind==V::Number&&std::isfinite(number)&&number>=0&&
                    std::floor(number)==number&&number==layer.collection.features.size(),
                    "GIS_ZIP_FEATURE_COUNT_MISMATCH");
        }
        result.layers.push_back(std::move(layer));
    }
    require(!result.layers.empty(),"GIS_ZIP_NO_GEOJSON");
    return result;
}
}
