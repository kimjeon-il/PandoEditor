#include <projectgeopackage.h>
#include <projectcodec.h>
#include <pandoeditor/project.h>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cassert>

using namespace pandoeditor;
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    Project source;
    source.replace(ProjectDocument({{"A","Alpha",{{{{0,0},{2,0},{2,2},{0,2},{0,0}}}},0x123456},
                                    {"B","Beta",{{{{3,0},{5,0},{5,2},{3,2},{3,0}}}},0x654321},
                                    {"C","Gamma",{{{{6,0},{8,0},{8,2},{6,2},{6,0}}}},0xabcdef}},
                                   {{"countries","Countries"}}));
    auto doc=source.document();
    doc.units.front().libraryOrigin=LibraryOrigin{"history:A","v1","1945","archive","2","high","year",false,{}};
    doc.symbols[territorialRef("B")].policy=FlagPolicy::None;
    auto& embedded=doc.symbols[territorialRef("C")];
    embedded.policy=FlagPolicy::Embedded;
    embedded.embeddedDataUrl="data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciLz4=";
    source.replace(doc);
    const auto original=projectcodec::encode(source);
    QTemporaryDir directory;assert(directory.isValid());
    const auto path=directory.filePath("project.gpkg");
    const auto bytes=exportProjectGeoPackage(source);
    QFile output(path);assert(output.open(QIODevice::WriteOnly));
    assert(output.write(bytes)==bytes.size());output.close();
    const auto restored=readProjectGeoPackage(path);
    Project decoded;decoded.replace(projectcodec::decode(restored));
    assert(projectcodec::encode(decoded)==original);
}
