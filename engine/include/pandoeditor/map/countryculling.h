#pragma once
#include <pandoeditor/map/countrymesh.h>
#include <pandoeditor/map/drawplan.h>
#include <pandoeditor/map/mapviewstate.h>
#include <cstddef>

enum class CountryRangeKind {Triangle,Boundary};

CountryDrawPlan countryDrawRangesForView(const CountryBaseMesh&,const MapViewState&,
    CountryRangeKind kind=CountryRangeKind::Triangle,double paddingPixels=64,
    double fullRangeThreshold=.7,std::size_t maxRanges=96);

WorldRenderPlan worldRenderPlanForView(const CountryBaseMesh&,const MapViewState&);
