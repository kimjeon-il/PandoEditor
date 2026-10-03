#pragma once
#include <QQuickWindow>
#include <QQuickItem>
#include <QTest>
#include <algorithm>
inline QQuickItem* navigationItem(QQuickItem* root,const QString& name) {
 if(root->objectName()==name)return root;
 for(auto child:root->childItems())if(auto found=navigationItem(child,name))return found;
 return nullptr;
}
inline void navigationEnsureVisible(QQuickItem* c){
 if(!c)return;
 for(auto p=c->parentItem();p;p=p->parentItem()){
  if(!p->property("contentY").isValid())continue;
  auto content=qvariant_cast<QQuickItem*>(p->property("contentItem"));if(!content)continue;
  const auto y=c->mapToItem(content,QPointF()).y();
  const auto maximum=std::max(0.,p->property("contentHeight").toDouble()-p->height());
  p->setProperty("contentY",std::clamp(y-8.,0.,maximum));
 }
}
inline bool navigationClick(QQuickWindow* window,const QString& name) {
 auto c=navigationItem(window->contentItem(),name);if(!c||!c->isVisible()||!c->isEnabled())return false;
 navigationEnsureVisible(c);QTest::qWait(50);
 QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,c->mapToScene({c->width()/2,c->height()/2}).toPoint());QTest::qWait(100);return true;
}
inline void enterExistingControlRoute(QQuickWindow* window,const QString& name) {
 if(name=="detailObjectName"||name=="detailObjectNotes"){
  auto panel=navigationItem(window->contentItem(),"objectPropertyPanel");
  if(!panel||!panel->isVisible())navigationClick(window,"openObjectEditor");
  navigationClick(window,"objectInfoTab");
 }
 const bool memo=name=="countryMemo"||name=="subunitMemo"||name=="regionMemo";
 if(name=="objectNotesTrigger"||name=="objectColorTrigger"||memo) {
  auto c=navigationItem(window->contentItem(),memo?"objectNotesTrigger":name);
  if(!c||!c->isVisible())navigationClick(window,"openObjectEditor");
 }
 const QStringList fileActions={"importButton","deviceSaveButton","exportButton","saveAsButton","webImportButton","gisImportButton","gisExportButton","projectGpkgExportButton","legacyPanelButton","layerManagementButton","newProjectButton"};
 if(fileActions.contains(name)) {
  auto c=navigationItem(window->contentItem(),name);
  if(!c||!c->isVisible())navigationClick(window,"fileMenuButton");
 }
 if(name=="contentPanelButton"||name=="historicalLibraryButton"||name.startsWith("addDistribution")||name=="addPlaceLabel"||name=="addRiver"||name=="addLake") {
  auto c=navigationItem(window->contentItem(),name);
  if(!c||!c->isVisible())navigationClick(window,"createMenuButton");
 }
 // Compatibility tests still exercise real pointer events and all old assertions.
 // The web metadata entry now lives on the selection toolbar, not the native panel.
 if(name=="countryMemo"||name=="subunitMemo"||name=="regionMemo") {
  auto memo=navigationItem(window->contentItem(),name);
  if(!memo||!memo->isVisible())navigationClick(window,"objectNotesTrigger");
 }
 const QStringList layerControls={"layerName","layerLocked","layerVisible","layerOpacity","addLayer","removeLayer","layersTab"};
 const QStringList advancedControls={"countryPicker","countryColor","countryOpacity","countryTab","applyEdits","cancelEdits","cancelBackgroundWork"};
 if(layerControls.contains(name)){
  auto c=navigationItem(window->contentItem(),name);
  if(!c||!c->isVisible()){enterExistingControlRoute(window,"layerManagementButton");navigationClick(window,"layerManagementButton");}
 }else if(advancedControls.contains(name)||name.startsWith("swatch")){
  auto c=navigationItem(window->contentItem(),name);
  if(!c||!c->isVisible()){
   auto notes=navigationItem(window->contentItem(),"closeObjectNotes");if(notes&&notes->isVisible())navigationClick(window,"closeObjectNotes");
   enterExistingControlRoute(window,"legacyPanelButton");navigationClick(window,"legacyPanelButton");
   navigationClick(window,"objectInfoTab");
   if(name!="countryTab")navigationClick(window,"countryTab");
  }
 }
 if(name=="projectionFlatButton"||name=="projectionGlobeButton"||name.startsWith("terrain")){
  auto display=window->findChild<QObject*>("mapDisplayPopup");
  if(!display||!display->property("visible").toBool())navigationClick(window,"mapDisplayButton");
  const auto wanted=name.startsWith("projection")?QString("projection"):QString("terrain");
  if(display&&display->property("section").toString()!=wanted){
   if(!display->property("section").toString().isEmpty())navigationClick(window,"viewMenuBack");
   navigationClick(window,wanted=="projection"?"viewProjectionMenu":"viewTerrainMenu");
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
 navigationEnsureVisible(navigationItem(window->contentItem(),name));
}
