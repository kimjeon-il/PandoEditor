#include "placenamedisplay.h"
#include <QRegularExpression>
#include <QSet>
#include <stdexcept>

PlaceLanguageSelection normalizedPlaceLanguages(const QVariantMap& values) {
    PlaceLanguageSelection selected{values.value("ko").toBool(),
        values.value("en").toBool(),values.value("native").toBool()};
    return selected.ko||selected.en||selected.native?selected:PlaceLanguageSelection{};
}
QVariantMap placeLanguagesToVariant(const PlaceLanguageSelection& selected) {
    return {{"ko",selected.ko},{"en",selected.en},{"native",selected.native}};
}
std::optional<PlaceLanguageSelection> toggledPlaceLanguage(
    const PlaceLanguageSelection& current,const QString& language,bool checked) {
    PlaceLanguageSelection next=current;
    bool* flag=nullptr;
    if(language=="ko")flag=&next.ko;
    else if(language=="en")flag=&next.en;
    else if(language=="native")flag=&next.native;
    else throw std::invalid_argument("Unknown place display language");
    if(*flag==checked)return {};
    *flag=checked;
    if(!next.ko&&!next.en&&!next.native)return {};
    return next;
}
std::vector<PlaceDisplayRow> resolvePlaceDisplayRows(
    const PlaceRecord& record,const PlaceLanguageSelection& selected,const QString& mapDate) {
    QString ko=record.name,en=record.nameEn,native=record.nameNative;
    if(!mapDate.isEmpty()) {
        static const QRegularExpression format("^[0-9]{4}-[0-9]{2}-[0-9]{2}$");
        if(!format.match(mapDate).hasMatch())throw std::invalid_argument("Invalid map label date");
        const int year=mapDate.left(4).toInt();
        for(const auto& transition:record.nameTimeline) {
            if(!transition.fromDate.isEmpty()?transition.fromDate>mapDate:transition.fromYear>year)break;
            if(!transition.ko.isEmpty())ko=transition.ko;
            if(!transition.en.isEmpty())en=transition.en;
            if(!transition.native.isEmpty())native=transition.native;
        }
    }
    QSet<QString> seen;std::vector<PlaceDisplayRow> rows;
    for(const auto& entry:std::vector<PlaceDisplayRow>{{"ko",ko},{"en",en},{"native",native}}) {
        if((entry.language=="ko"&&!selected.ko)||
           (entry.language=="en"&&!selected.en)||
           (entry.language=="native"&&!selected.native))continue;
        const auto name=entry.text.trimmed();
        const auto key=name.normalized(QString::NormalizationForm_KC).toLower();
        if(name.isEmpty()||seen.contains(key))continue;
        seen.insert(key);rows.push_back({entry.language,name});
    }
    return rows;
}
