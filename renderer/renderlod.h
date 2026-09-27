#pragma once
#include <pandoeditor/document.h>

enum class RenderLod { Coarse, Medium, High };
enum class LodPolicy { Exact, Independent };

RenderLod resolveRenderLod(RenderLod requested,LodPolicy policy,bool protectedGeometry) noexcept;
// Derived display geometry only. The source GeometryStore is never changed.
pandoeditor::Geometry prepareRenderGeometry(const pandoeditor::Geometry& source,
    RenderLod requested,LodPolicy policy,bool protectedGeometry=false,
    bool globeReady=false);
