# M7.3 Qt Quick GPU renderer implementation status

M7.3 adds `GpuMapItem`, a scene graph consumer of the immutable M7.2
`MapSceneBridge`. It never reads `ProjectDocument` or modifies source
coordinates. `MapSceneNode` follows `RenderScene::drawSequence` and prepares
indexed polygon triangles, screen-pixel stroke quads and point marker quads.
QSB shaders project the same derived longitude/latitude packets in flat and
orthographic globe views. Globe fragment visibility and stroke horizon
clipping use the view uniform; pan, zoom and rotation update material uniforms
without replacing geometry when packet identity is unchanged.

The existing `MapView.qml` projection parameters are supplied to the GPU item
as a compatibility adapter, so picking and QML edit/label/flag overlays keep
their current screen positions. The QML safe-area label/flag layer remains
outside the GPU item. The GPU item has one render owner at a time with
`MapRenderItem`, controlled by `PANDOEDITOR_MAP_RENDERER=auto|gpu|cpu`.
Software/unknown graphics APIs and missing shaders use CPU fallback in auto
mode. Forced GPU displays its failure diagnostic instead of hiding it.

Built-in hydro runtime frames do not yet exist in the M7.2 typed scene. While
one is loaded, auto mode keeps the existing CPU renderer. This is an explicit
M7.4 integration seam and prevents disappearing rivers/lakes. The M7.2
interaction packet now carries chooser candidates and edit-target references.
GPU outlines and selected vertex markers are derived from immutable stroke
packets; complete M3.5 interaction parity and
QPainter multiply pixel equivalence still require the deferred validation
work. The render-thread material code is not a dependency on QRhi private API.

## Deferred evidence

| Check | Status |
| --- | --- |
| Qt ShaderTools/QSB build | NOT RUN — toolchain unavailable here |
| C++ build and tests | NOT RUN — cmake and Qt 6 unavailable here |
| GPU/CPU pixel comparison | NOT RUN |
| Windows D3D11/OpenGL visible run | NOT RUN |
| Android physical run | NOT RUN |

M7.3 release gate is open. No visual, shader compilation or platform pass is
claimed from source review alone. The subsequent validation work must check
the scene graph node lifetime, shaders on each backend, premultiplied alpha,
dateline/polar/hole imagery, selection order, resource packaging and the
fallback transition before promoting the renderer without qualification.
