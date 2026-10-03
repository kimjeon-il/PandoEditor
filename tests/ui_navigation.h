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
 const bool memo=name=="countryMemo"||name=="subunitMemo"||name=="regionMemo";
 if(name=="objectNotesTrigger"||name=="objectColorTrigger"||memo) {
  auto c=navigationItem(window->contentItem(),memo?"objectNotesTrigger":name);
  if(!c||!c->isVisible())navigationClick(window,"toggleObjectEditor");
 }
 const QStringList fileActions={"importButton","deviceSaveButton","exportButton","saveAsButton","webImportButton","gisImportButton","gisExportButton","projectGpkgExportButton","legacyPanelButton"};
 if(fileActions.contains(name)) {
  auto c=navigationItem(window->contentItem(),name);
  if(!c||!c->isVisible())navigationClick(window,"fileMenuButton");
 }
 if(name=="contentPanelButton"||name=="historicalLibraryButton") {
  auto c=navigationItem(window->contentItem(),name);
  if(!c||!c->isVisible())navigationClick(window,"createMenuButton");
 }
 // Compatibility tests still exercise real pointer events and all old assertions.
 // The web metadata entry now lives on the selection toolbar, not the native panel.
 if(name=="countryMemo"||name=="subunitMemo"||name=="regionMemo") {
  auto memo=navigationItem(window->contentItem(),name);
  if(!memo||!memo->isVisible())navigationClick(window,"objectNotesTrigger");
 }
  const QStringList layerControls={"layerName","layerLocked","layerVisible","layerOpacity","addLayer","removeLayer"};
  // Content Undo/Redo must not rely on whichever tab happened to be open.
  // Route legacy layer controls through the real tab before locating them.
  if(layerControls.contains(name)) {
   auto layersTab=navigationItem(window->contentItem(),"layersTab");
   if(layersTab&&layersTab->isVisible()&&layersTab->isEnabled()) navigationClick(window,"layersTab");
  }
  const QStringList legacy={"countryPicker","countryColor","countryOpacity","layerName","layerLocked","layerVisible","layerOpacity","layersTab","countryTab","addLayer","removeLayer","applyEdits","cancelEdits","cancelBackgroundWork"};
 if(legacy.contains(name)||name.startsWith("swatch")) {
  auto c=navigationItem(window->contentItem(),name);
  if(!c||!c->isVisible()) {
   auto notes=navigationItem(window->contentItem(),"closeObjectNotes");if(notes&&notes->isVisible())navigationClick(window,"closeObjectNotes");
   enterExistingControlRoute(window,"legacyPanelButton");
   navigationClick(window,"legacyPanelButton");
  }
 }
 if(name=="focusSelection"||name=="objectLockButton") {
  auto c=navigationItem(window->contentItem(),name);
  if(!c||!c->isVisible()) {
   auto panel=navigationItem(window->contentItem(),"objectPropertyPanel");
   if(!panel||!panel->isVisible())navigationClick(window,"openObjectEditor");
   navigationClick(window,"objectActionsTab");
  }
 }
}
