#include "world_diagnostic_helper.h"
#include "maprenderitem.h"
#include <pandoeditor/map/mapscenebuilder.h>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QVariantMap>
#include <algorithm>
#include <cstdio>
#include <set>
#include <stdexcept>

namespace {
using namespace pandoeditor;
struct Selection {
    std::set<std::string> countries;
    std::set<std::string> sentinels;
    bool composition=false,hydro=false;
};

Selection selection(const QString& scenario) {
    if(scenario==QStringLiteral("control-deu")) return {{{"DEU"}},{},true,false};
    if(scenario==QStringLiteral("dateline")) return {{{"RUS","FJI","KIR"}},{{"DATELINE"}},false,false};
    if(scenario==QStringLiteral("polar")) return {{{"ATA"}},{{"POLAR"}},false,false};
    if(scenario==QStringLiteral("mixed-content")) return {{{"DEU"}},{},true,true};
    if(scenario==QStringLiteral("corpus-fit"))
        return {{{"DEU","RUS","FJI","KIR","USA","FRA","IDN","PHL","ATA","ZAF","LSO","CHL","NOR"}},{},true,true};
    throw std::runtime_error("unknown scenario: "+scenario.toStdString());
}

qint64 coordinateJsonBytes(const Geometry& shape) {
    auto point=[](const Point& value) {return QJsonArray{value.x,value.y};};
    auto ring=[&](const Ring& values) {
        QJsonArray result;for(const auto& value:values) result.append(point(value));return result;
    };
    QJsonArray coordinates;
    if(shape.type=="Point") return QJsonDocument(point(shape.points.at(0))).toJson(QJsonDocument::Compact).size();
    if(shape.type=="LineString") coordinates=ring(shape.lines.at(0));
    else if(shape.type=="MultiLineString")
        for(const auto& line:shape.lines) coordinates.append(ring(line));
    else if(shape.type=="Polygon"||shape.type=="MultiPolygon") {
        for(const auto& polygon:shape.polygons) {
            QJsonArray part;for(const auto& line:polygon) part.append(ring(line));
            if(shape.type=="Polygon") coordinates=part;
            else coordinates.append(part);
        }
    } else throw std::runtime_error("unknown coordinate geometry type");
    return QJsonDocument(coordinates).toJson(QJsonDocument::Compact).size();
}

ProjectDocument scenarioDocument(const ProjectDocument& full,const Selection& wanted) {
    ProjectDocument d;d.documentId="m71-scenario";
    d.presentation.userLayers=full.presentation.userLayers;
    std::set<GeometryRef> used;
    auto member=[&](const ObjectRef& ref) {
        const auto style=full.presentation.objectStyles.find(ref);
        if(style!=full.presentation.objectStyles.end()) d.presentation.objectStyles.emplace(*style);
        const auto layer=full.presentation.membership.find(ref);
        if(layer!=full.presentation.membership.end()) d.presentation.membership.emplace(*layer);
    };
    for(const auto& unit:full.units) {
        if(isRootGeneral(full,unit) ? !wanted.countries.count(unit.id) : !wanted.composition) continue;
        const auto& binding=staticGeometryBinding(full,unit.id);const auto& parent=staticParentRelation(full,unit.id);
        appendTerritory(d,unit,binding.geometryRef,parent.parentId,parent.coverageMode);used.insert(binding.geometryRef);member(territorialRef(unit.id));
    }
    if(wanted.composition) {
        d.presentation.webPresentation=full.presentation.webPresentation;
        d.distributionLayers=full.distributionLayers;
        d.distributionEntries=full.distributionEntries;
        d.labels=full.labels;
        for(const auto& entry:d.distributionEntries) if(entry.geometry) used.insert(*entry.geometry);
        for(const auto& label:d.labels) used.insert(label.geometry);
    }
    if(wanted.hydro) {
        d.hydro=full.hydro;
        for(const auto& item:d.hydro) used.insert(item.geometry);
    }
    for(const auto& item:full.genericFeatures) {
        const bool synthetic=item.id=="DATELINE"||item.id=="POLAR";
        if(synthetic ? !wanted.sentinels.count(item.id) : !wanted.composition) continue;
        d.genericFeatures.push_back(item);used.insert(item.geometry);
    }
    for(const auto& ref:used) {
        const auto source=full.geometries.get(ref);
        if(!source) throw std::runtime_error("scenario references a missing geometry");
        d.geometries.insert(ref,*source);
    }
    validateDocument(d);
    return d;
}

QJsonObject diagnostics(const std::vector<m71fixture::ProjectionDiagnostic>& rows) {
    QJsonArray result;
    for(const auto& row:rows) {
        QJsonArray classes;for(const auto& name:row.classes) classes.append(QString::fromStdString(name));
        result.append(QJsonObject{
            {"id",QString::fromStdString(row.id)}, {"classes",classes},
            {"sourceLongitudeSpan",row.sourceLongitudeSpan}, {"projectedWidth",row.projectedWidth},
            {"maxProjectedSegmentJump",row.maxProjectedSegmentJump},
            {"projectedPathCharacters",double(row.projectedPathCharacters)}, {"hasNonFinite",row.hasNonFinite}});
    }
    return {{"rows",result}};
}

QJsonObject run(const QString& root,const QString& name) {
    const Selection wanted=selection(name);
    QElapsedTimer timer;timer.start();
    const auto full=m71fixture::loadWorldCorpusProject(root);
    const auto document=scenarioDocument(full,wanted);
    const double fixtureMs=timer.nsecsElapsed()/1e6;
    timer.restart();
    MapProjection projection;projection.rebuild(document);
    const double projectionMs=timer.nsecsElapsed()/1e6;
    const auto rows=m71fixture::diagnoseCurrentProjection(document);
    for(const auto& row:rows) if(row.hasNonFinite) throw std::runtime_error("non-finite projection: "+row.id);
    if(!m71fixture::projectionPathsFinite(projection))
        throw std::runtime_error("non-finite projection path or bounds");
    constexpr int imageWidth=1440,imageHeight=900;
    QImage image(imageWidth,imageHeight,QImage::Format_ARGB32_Premultiplied);
    if(image.isNull()) throw std::runtime_error("paint image allocation failed");
    MapRenderItem item;item.setWidth(imageWidth);item.setHeight(imageHeight);
    std::size_t pathChars=0;
    for(const auto& value:projection.paths)
        pathChars+=std::size_t(value.toMap().value(QStringLiteral("path")).toString().size());

    const double scale=std::min((imageWidth-48)/projection.width,
                                (imageHeight-48)/projection.height);
    MapViewState view;
    view.mode=ProjectionMode::Flat;
    view.viewportWidth=imageWidth;view.viewportHeight=imageHeight;
    view.scale=scale*projection.cosLatitudeValue()*180.0/3.14159265358979323846;
    view.translateX=24-projection.minXValue()*scale;
    view.translateY=24+projection.maxLatitudeValue()*scale;

    GeometryPacketCache packetCache;
    MapSceneBuilder sceneBuilder(packetCache);
    const auto scene=sceneBuilder.buildDocument(document,1,view,{},{});
    item.setSceneSnapshot(scene,view);
    auto paint=[&] {
        image.fill(Qt::white);
        QPainter painter(&image);
        if(!painter.isActive()) throw std::runtime_error("QPainter is inactive");
        item.paint(&painter);painter.end();
    };
    timer.restart();paint();const double firstMs=timer.nsecsElapsed()/1e6;
    timer.restart();paint();const double secondMs=timer.nsecsElapsed()/1e6;
    bool painted=false;
    for(int y=0;y<imageHeight&&!painted;++y) {
        const auto* pixels=reinterpret_cast<const QRgb*>(image.constScanLine(y));
        painted=std::any_of(pixels,pixels+imageWidth,[](QRgb pixel){return pixel!=qRgb(255,255,255);});
    }
    if(!painted) throw std::runtime_error("paint produced an empty image");
    std::size_t polygons=0,rings=0,holes=0,positions=0;
    qint64 coordinateSourceBytes=0;
    for(const auto& [ref,shape]:document.geometries.versions()) {
        const auto stats=m71fixture::geometryStats(*shape);
        polygons+=stats.polygonCount;rings+=stats.ringCount;
        holes+=stats.holeCount;positions+=stats.coordinateCount;
        coordinateSourceBytes+=coordinateJsonBytes(*shape);
    }
    qint64 fixtureBytes=0;
    for(const auto* file:{"manifest.json","countries.geojson","hydro-river.geojson","hydro-lake.geojson",
                          "sentinels.geojson","composition.json"}) {
        const auto bytes=QFileInfo(QDir(root).filePath(QString::fromLatin1(file))).size();
        fixtureBytes+=bytes;
    }
    const QJsonObject counts{{"territorial",int(document.units.size())},{"hydro",int(document.hydro.size())},
        {"distributionLayer",int(document.distributionLayers.size())},
        {"distributionEntry",int(document.distributionEntries.size())},
        {"generic",int(document.genericFeatures.size())},{"label",int(document.labels.size())}};
    const auto countryCount=std::count_if(document.units.begin(),document.units.end(),
        [](const auto& unit){return unit.kind==UnitKind::General;});
    const QJsonObject structure{{"objectCounts",counts},{"countryCount",int(countryCount)},
        {"polygonCount",double(polygons)},{"ringCount",double(rings)},{"holeCount",double(holes)},
        {"coordinateCount",double(positions)},{"geometryVersionCount",int(document.geometries.versions().size())},
        {"projectionPathCount",projection.paths.size()},{"totalPathCharacters",double(pathChars)},
        {"hydroCount",int(document.hydro.size())},{"genericCount",int(document.genericFeatures.size())},
        {"labelCount",int(document.labels.size())},{"imageWidth",imageWidth},{"imageHeight",imageHeight}};
    const QJsonObject timing{{"fixtureLoadMs",fixtureMs},{"projectionRebuildMs",projectionMs},
        {"firstPaintMs",firstMs},{"secondPaintMs",secondMs}};
    const QJsonObject buffers{{"coordinateSourceBytes",double(coordinateSourceBytes)},
        {"committedFixtureBytes",double(fixtureBytes)},
        {"utf16PathStringBytes",double(pathChars*sizeof(QChar))},
        {"qImageBackingBytes",double(image.sizeInBytes())}};
    return {{"schema",QStringLiteral("pandoeditor-m71-baseline")},{"version",1},
        {"scenario",name},{"structure",structure},{"timing",timing},
        {"buffers",buffers},{"diagnostics",diagnostics(rows).value("rows")}};
}
}

int main(int argc,char** argv) {
    qputenv("QT_QPA_PLATFORM","offscreen");
    QGuiApplication app(argc,argv);
    try {
        QString root,scenario;bool json=false;
        for(int i=1;i<argc;++i) {
            const QString arg=QString::fromLocal8Bit(argv[i]);
            if(arg==QStringLiteral("--fixture") && i+1<argc) root=QString::fromLocal8Bit(argv[++i]);
            else if(arg==QStringLiteral("--scenario") && i+1<argc) scenario=QString::fromLocal8Bit(argv[++i]);
            else if(arg==QStringLiteral("--json")) json=true;
            else throw std::runtime_error("unknown or incomplete command-line option");
        }
        if(root.isEmpty()||scenario.isEmpty()||!json) throw std::runtime_error("usage: world_render_baseline --fixture <root> --scenario <name> --json");
        const auto report=run(root,scenario);
        const auto bytes=QJsonDocument(report).toJson(QJsonDocument::Compact);
        fwrite(bytes.constData(),1,std::size_t(bytes.size()),stdout);
        fputc('\n',stdout);
        return 0;
    } catch(const std::exception& error) {
        fprintf(stderr,"M7.1 baseline failed: %s\n",error.what());
        return 1;
    }
}
