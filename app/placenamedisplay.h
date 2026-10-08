#pragma once
#include "placeruntimestore.h"
#include <QVariantMap>
#include <QString>
#include <optional>
#include <vector>

struct PlaceLanguageSelection { bool ko=true,en=false,native=false; };
struct PlaceDisplayRow { QString language,text; };

// Pure platform-adapter functions mirroring Web place-contract.js.
PlaceLanguageSelection normalizedPlaceLanguages(const QVariantMap& value);
QVariantMap placeLanguagesToVariant(const PlaceLanguageSelection& value);
std::optional<PlaceLanguageSelection> toggledPlaceLanguage(
    const PlaceLanguageSelection& current,const QString& language,bool checked);
std::vector<PlaceDisplayRow> resolvePlaceDisplayRows(
    const PlaceRecord& record,const PlaceLanguageSelection& selected,const QString& mapDate={});
