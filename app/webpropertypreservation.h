#pragma once
#include "webjson.h"

// Shape checks for the pinned web's presentation-only data. Nothing is rewritten:
// a recognized payload remains retained with explicit dependency/effect bounds.
// Unknown keys or nested extension values retain the original opaque barrier.
namespace webimport::propertypreservation {
using webjson::V;
inline bool scalar(const V& v){return v.kind!=V::Object&&v.kind!=V::Array;}
inline bool empty(const V& v){return v.kind==V::Null||(v.kind==V::Array&&v.array.empty())||(v.kind==V::Object&&v.object.empty());}
inline bool keys(const V& v,const std::set<std::string>& allowed){
 if(v.kind!=V::Object)return false;
 for(const auto& [k,value]:v.object){(void)value;if(!allowed.count(k))return false;}return true;
}
inline bool scalarFields(const V& v,const std::set<std::string>& allowed){
 if(!keys(v,allowed))return false;for(const auto& [k,x]:v.object){(void)k;if(!scalar(x))return false;}return true;
}
inline bool booleanMap(const V& v){if(v.kind!=V::Object)return false;for(const auto& [k,x]:v.object){(void)k;if(x.kind!=V::Bool)return false;}return true;}
inline bool styles(const V& v){return scalarFields(v,{"opacity","boundaryVisible","boundaryWidth","labelsVisible","blendMode"});}
inline bool sourceInfo(const std::string& key,const V& v){
 if(empty(v))return true;
 if(key=="baseDataset")return v.kind==V::String;
 if(key!="physicalSourceInfo"||!keys(v,{"terrain","hydro"}))return false;
 for(const auto& [domain,x]:v.object){
  const auto allowed=domain=="terrain"?std::set<std::string>{"dataset","version"}:std::set<std::string>{"dataset","version","coordinatePolicy","selection"};
  if(!keys(x,allowed))return false;
  for(const auto& [k,y]:x.object)if(k=="selection"?!empty(y):!scalar(y))return false;
 }return true;
}
inline bool recognized(const std::string& key,const V& v){
 const std::set<std::string> collections={"labels","hydroEdits","genericFeatures","distributionLayers","distributionEntries"};
 if(collections.count(key))return empty(v);
 if(key=="distributionSettings")return empty(v)||scalarFields(v,{"renderMode","boundaryVisible"});
 if(key=="layerVisibility")return keys(v,{"countries","subunits","regions","languages","ethnicities","religions","rivers","lakes","genericFeatures","labels","basemapLabels","countryFlags","subunitLabels","subunitFlags","regionLabels","regionFlags"})&&booleanMap(v);
 if(key=="itemVisibility"){
  if(!keys(v,{"countries","subunits","regions","languages","ethnicities","religions","hydro","genericFeatures","labels","countryLabels"}))return false;
  for(const auto& [k,x]:v.object){(void)k;if(!booleanMap(x))return false;}return true;
 }
 if(key=="physicalSettings"){
  if(empty(v))return true;
  if(!keys(v,{"terrainVisible","terrainStyle","hydroLayers","userFeaturesVisible","hiddenHydroIds","dataset"}))return false;
  for(const auto& [k,x]:v.object)if(k=="hydroLayers"||k=="hiddenHydroIds"){if(!booleanMap(x))return false;}else if(!scalar(x))return false;
  return true;
 }
 if(key=="labelSettings"){
  if(empty(v))return true;if(v.kind!=V::Object)return false;
  for(const auto& [id,x]:v.object){
   (void)id;if(!keys(x,{"priority","minZoom","maxZoom","manualPosition","pinned","collisionGroup"}))return false;
   for(const auto& [k,y]:x.object)if(k=="manualPosition"&&y.kind!=V::Null){
    if(y.kind!=V::Array||y.array.size()!=2)return false;for(const auto& n:y.array)if(n.kind!=V::Number)return false;
   }else if(!scalar(y))return false;
  }return true;
 }
 if(key!="layerPresentation"||!keys(v,{"schemaVersion","overlayOrder","styles","objectStyles","objectOrder"}))return false;
 const std::set<std::string> groups={"countries","subunits","regions","languages","ethnicities","religions","rivers","lakes","hydro","genericFeatures","labels","countryLabels","terrain"};
 for(const auto& [k,x]:v.object){
  if(k=="schemaVersion"){if(x.kind!=V::Number)return false;continue;}
  if(k=="styles"||k=="objectStyles"){
   if(x.kind!=V::Object)return false;
   for(const auto& [id,y]:x.object){if(k=="styles"&&!groups.count(id))return false;if(!styles(y))return false;}
  }else{
   if(x.kind!=V::Array)return false;for(const auto& y:x.array)if(y.kind!=V::String)return false;
  }
 }return true;
}
}
