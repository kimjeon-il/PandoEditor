#include <historicallibraryloader.h>
#include <geometrycalculator.h>
#include <pandoeditor/geometrypredicates.h>
#include <QCoreApplication>
#include <QFile>
#include <cassert>
#include <stdexcept>

using namespace pandoeditor;
namespace {
Geometry box(double x) {
    return {"Polygon",{}, {},{{{{x,0},{x+1,0},{x+1,1},{x,1},{x,0}}}}};
}
bool rejected(const std::function<void()>& action) {
    try {action();}catch(const std::invalid_argument&){return true;}
    return false;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    QFile input(QStringLiteral(M6_HISTORICAL_PILOT_SAMPLE));
    assert(input.open(QIODevice::ReadOnly));
    const auto bytes=input.readAll();
    const auto source=parseHistoricalLibrarySource(bytes);
    assert(source.entities.size()==4 && source.snapshots.size()==1);
    assert(source.entities[0].versions[0].memberCountryIds.size()==2);
    auto lookup=[](const std::string& id)->std::optional<Geometry>{
        if(id=="CZE")return box(0);
        if(id=="SVK")return box(1);
        return std::nullopt;
    };
    GeometryCalculator kernel=[](const GeometryOperationRequest& request,
                                 const GeometryCancellation& cancel){
        return calculateGeometry(request,cancel);
    };
    const auto built=materializeHistoricalSource(source,lookup,kernel);
    assert(built.library.get("historical-country:czechoslovakia"));
    assert(built.library.get("historical-country:nagorno-karabakh"));
    assert(!built.library.get("historical-country:yugoslavia"));
    assert(built.library.getSnapshot("pilot-1991"));
    assert(built.missingEntityIds.size()==2);
    const auto copy=built.library.instantiate("historical-country:czechoslovakia","1991");
    assert(copy.geometry.polygons.size()==1);
    assert(rejected([&]{auto invalid=bytes;invalid.replace(0,invalid.indexOf('2')+1,
        QByteArray("{\"schemaVersion\":3"));parseHistoricalLibrarySource(invalid);}));
    assert(rejected([]{parseHistoricalLibrarySource("{not JSON");}));
    const QByteArray maskSource=R"({"schemaVersion":2,"entities":[{
      "libraryId":"historical-country:mask","type":"country","canonicalName":"Masked",
      "geometryVersions":[{"id":"v","geometry":{"type":"Polygon","coordinates":[[[0,0],[4,0],[4,4],[0,4],[0,0]]]},
      "excludeGeometry":{"type":"Polygon","coordinates":[[[1,1],[2,1],[2,2],[1,2],[1,1]]]}}]}],"snapshots":[]})";
    const auto masked=materializeHistoricalSource(parseHistoricalLibrarySource(maskSource),lookup,kernel);
    assert(planarArea(masked.library.instantiate("historical-country:mask","").geometry)==15);
    if(qEnvironmentVariableIsSet("PANDOEDITOR_HISTORICAL_FULL_PILOT")) {
        QFile full(qEnvironmentVariable("PANDOEDITOR_HISTORICAL_FULL_PILOT"));
        assert(full.open(QIODevice::ReadOnly));
        const auto original=parseHistoricalLibrarySource(full.readAll());
        assert(original.entities.size()==25 && original.snapshots.size()==1);
        const auto noModernLookup=[](const std::string&)->std::optional<Geometry>{return std::nullopt;};
        const auto partial=materializeHistoricalSource(original,noModernLookup,kernel);
        assert(partial.library.get("historical-country:nagorno-karabakh"));
        assert(partial.library.getSnapshot("pilot-1991"));
        assert(!partial.missingEntityIds.empty());
    }
}
