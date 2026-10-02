#include <pandoeditor/map/builtinhydrochannel.h>
#include <pandoeditor/map/mapscenebuilder.h>
#include <pandoeditor/presentation.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace pandoeditor;

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool near(double a,double b,double eps=1e-6){return std::abs(a-b)<=eps;}

HydroPhysicalFeature riverFeature() {
    HydroPhysicalFeature feature;
    feature.fid=10;feature.logicalFid=100;feature.kind=1;feature.flags=1;
    feature.strokeWidth=2;
    feature.geometry.kind=1;
    feature.geometry.lines={{{0,0},{1000000,0},{2000000,0}}};
    feature.widths={{2,4,6}};
    return feature;
}

HydroPhysicalFeature lakeFeature() {
    HydroPhysicalFeature feature;
    feature.fid=20;feature.logicalFid=200;feature.kind=2;
    feature.geometry.kind=3;
    feature.geometry.polygons={{{{0,0},{2000000,0},{2000000,2000000},
                                 {0,2000000},{0,0}}}};
    return feature;
}

void preparationKeepsVariableWidthsAndPolygonBoundary() {
    const auto river=prepareBuiltinHydroFeature(
        {"hydroBuiltin","river-aw"},"river",riverFeature());
    require(river.stroke.segmentCount==2,"river segment count");
    require(river.stroke.endpointWidths&&river.stroke.endpointWidths->size()==4,
            "river endpoint widths");
    require(near(river.stroke.endpointWidths->at(0),2)&&
            near(river.stroke.endpointWidths->at(1),4)&&
            near(river.stroke.endpointWidths->at(2),4)&&
            near(river.stroke.endpointWidths->at(3),6),"variable widths preserved");
    require(river.borderAligned,"border-aligned flag preserved");

    const auto lake=prepareBuiltinHydroFeature(
        {"hydroBuiltin","lake-aw"},"lake",lakeFeature());
    require(lake.polygon.triangleCount>0,"lake triangulated");
    require(lake.stroke.segmentCount==4,"lake boundary packet");
    require(!lake.stroke.endpointWidths,"lake uses style width");
}

void builderPublishesHydroInM5Order() {
    GeometryPacketCache cache;MapSceneBuilder builder(cache);
    const auto river=prepareBuiltinHydroFeature(
        {"hydroBuiltin","river-aw"},"river",riverFeature());
    const auto lake=prepareBuiltinHydroFeature(
        {"hydroBuiltin","lake-aw"},"lake",lakeFeature());
    builder.setBuiltinHydro(makeBuiltinHydroRenderFrame(7,{river,lake}));

    ProjectDocument doc;
    PresentationStyle rivers;rivers.opacity=.5;
    doc.presentation.webPresentation.styles["rivers"]=rivers;
    PresentationStyle lakes;lakes.opacity=.75;lakes.boundaryVisible=false;
    doc.presentation.webPresentation.styles["lakes"]=lakes;

    MapViewState view;InteractionRenderPacket interaction;
    interaction.primary=ObjectRef{"hydroBuiltin","river-aw"};
    auto scene=builder.buildDocument(doc,1,view,interaction,{});
    require(scene->polygons.size()==1,"lake fill typed packet");
    require(scene->strokes.size()==2,"lake boundary retained plus river");
    require(scene->polygons.front().object==ObjectRef{"hydroBuiltin","lake-aw"},
            "lake logical object ref");
    const auto riverIt=std::find_if(scene->strokes.begin(),scene->strokes.end(),
        [](const auto& draw){return draw.object==ObjectRef{"hydroBuiltin","river-aw"};});
    require(riverIt!=scene->strokes.end(),"river typed packet");
    require(riverIt->drawOrder.pass==33,"border-aligned river pass");
    require(near(riverIt->style.alpha,.5)&&near(riverIt->style.width,0),
            "river opacity and variable-width base style");
    require(riverIt->geometryPacket.endpointWidths!=nullptr,"GPU variable width channel");

    bool lakeFill=false,lakeBoundary=false,riverDraw=false;
    for(const auto& draw:scene->drawSequence) {
        if(draw.primitive==PrimitiveKind::Polygon&&
           scene->polygons.at(draw.index).object==ObjectRef{"hydroBuiltin","lake-aw"})
            lakeFill=true;
        if(draw.primitive==PrimitiveKind::Stroke) {
            const auto& stroke=scene->strokes.at(draw.index);
            if(stroke.object==ObjectRef{"hydroBuiltin","lake-aw"})lakeBoundary=true;
            if(stroke.object==ObjectRef{"hydroBuiltin","river-aw"})riverDraw=true;
        }
    }
    require(lakeFill&&riverDraw&&!lakeBoundary,"hidden lake boundary stays highlight-only");
    for(std::size_t i=1;i<scene->drawSequence.size();++i)
        require(!(scene->drawSequence[i].order<scene->drawSequence[i-1].order),
                "hydro draw order sorted");
}

void visibilityAndRevisionInvalidateTypedChannel() {
    GeometryPacketCache cache;MapSceneBuilder builder(cache);
    const auto river=prepareBuiltinHydroFeature(
        {"hydroBuiltin","river-aw"},"river",riverFeature());
    builder.setBuiltinHydro(makeBuiltinHydroRenderFrame(1,{river}));
    ProjectDocument doc;MapViewState view;
    auto first=builder.buildDocument(doc,1,view,{},{});
    require(!first->strokes.empty(),"initial river visible");

    doc.physicalData.hiddenHydroIds={"river-aw"};
    auto hidden=builder.buildDocument(doc,1,view,{},first);
    require(hidden->strokes.empty(),"hidden built-in hydro omitted");

    doc.physicalData.hiddenHydroIds.clear();
    builder.setBuiltinHydro(makeBuiltinHydroRenderFrame(2,{river}));
    auto next=builder.buildDocument(doc,1,view,{},hidden);
    require(!next->strokes.empty(),"river restored");
    require(next->geometrySignature!=hidden->geometrySignature,
            "hydro frame revision changes geometry signature");
}
}

int main() {
    preparationKeepsVariableWidthsAndPolygonBoundary();
    builderPublishesHydroInM5Order();
    visibilityAndRevisionInvalidateTypedChannel();
}
