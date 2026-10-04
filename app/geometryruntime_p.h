#pragma once
class QJSEngine;
namespace pandoeditor {
// App-private shared loader. Verifies the original resource hash and exactly one
// Qt comma-return correction. Does not alter any engine's math or platform globals.
void loadPinnedPolygonClipping(QJSEngine& engine);
}
