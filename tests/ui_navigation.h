#pragma once
#include <QQuickWindow>
#include <QQuickItem>
#include <QTest>
inline QQuickItem* navigationItem(QQuickItem* root,const QString& name) {
 if(root->objectName()==name)return root;
 for(auto child:root->childItems())if(auto found=navigationItem(child,name))return found;
 return nullptr;
}
inline bool navigationClick(QQuickWindow* window,const QString& name) {
 auto c=navigationItem(window->contentItem(),name);if(!c||!c->isVisible()||!c->isEnabled())return false;
 QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,c->mapToScene({c->width()/2,c->height()/2}).toPoint());QTest::qWait(100);return true;
}
inline void enterExistingControlRoute(QQuickWindow* window,const QString& name) {
 // Compatibility tests still exercise real pointer events and all old assertions.
 // The web metadata entry now lives on the selection toolbar, not the native panel.
 if(name=="countryMemo"||name=="subunitMemo"||name=="regionMemo") {
  auto memo=navigationItem(window->contentItem(),name);
  if(!memo||!memo->isVisible())navigationClick(window,"objectNotesTrigger");
 }
 const QStringList legacy={"countryPicker","countryColor","countryOpacity","layerName","layerLocked","layerVisible","layerOpacity","layersTab","countryTab","addLayer","removeLayer","applyEdits","cancelEdits","cancelBackgroundWork"};
 if(legacy.contains(name)||name.startsWith("swatch")) {
  auto c=navigationItem(window->contentItem(),name);
  if(!c||!c->isVisible()) {
   auto notes=navigationItem(window->contentItem(),"closeObjectNotes");if(notes&&notes->isVisible())navigationClick(window,"closeObjectNotes");
   navigationClick(window,"legacyPanelButton");
  }
 }
 if(name=="focusSelection") {
  auto c=navigationItem(window->contentItem(),name);if(!c||!c->isVisible())navigationClick(window,"openObjectEditor");
 }
}
