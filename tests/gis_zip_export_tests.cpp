#include <pandoeditor/giszip.h>
#include <cassert>
#include <functional>
#include <stdexcept>

using namespace pandoeditor;
int main() {
    GisZipArchive source{{{"countries.geojson",std::string(4096,'A')},
                          {"manifest.json","{\"schemaVersion\":3}"}}};
    auto bytes=writeGisZipArchive(source);
    assert(bytes.size()<4096);
    auto parsed=readGisZipArchive(bytes);
    assert(parsed.entries.size()==2);
    assert(parsed.entries[0].path==source.entries[0].path);
    assert(parsed.entries[0].bytes==source.entries[0].bytes);
    assert(parsed.entries[1].bytes==source.entries[1].bytes);
    for(const auto& bad:{"../escape.geojson","/absolute.geojson","C:/drive.geojson"}) {
        bool rejected=false;
        try {writeGisZipArchive({{{bad,"x"}}});}
        catch(const std::invalid_argument&){rejected=true;}
        assert(rejected);
    }
    bool rejected=false;
    try {writeGisZipArchive({{{"a.geojson","x"},{"A.geojson","y"}}});}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
}
