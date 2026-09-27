#include "mapprojection.h"
#include <QCoreApplication>
#include <stdexcept>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    pandoeditor::ProjectDocument document;document.documentId="lifetime-test";
    document.geometries.insert({"country",1},{"Polygon",{},{},{{
        {{0,0},{2,0},{2,2},{0,2},{0,0}}
    }}});
    document.units.push_back({"C","Country",{},pandoeditor::UnitKind::Country,{"country",1}});
    document.presentation.userLayers={{"custom","Custom"}};
    document.presentation.membership[pandoeditor::territorialRef("C")]="custom";
    document.presentation.objectStyles[pandoeditor::territorialRef("C")]={0x556677,1};
    document.geometries.insert({"point",1},{"Point",{{10,10}},{},{}});
    document.genericFeatures.push_back({"P","Point",{}, {"point",1}});
    pandoeditor::validateDocument(document);
    MapProjection projection;projection.rebuild(document);
    if(projection.paths.size()!=2||projection.width<=0||projection.height<=0)
        throw std::runtime_error("bounds-only view path or geometry lifetime");
}
