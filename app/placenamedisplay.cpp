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
    QString ko=record.name,en=record.nameEn;
    QStringList nativeNames{record.nameNative};nativeNames.append(record.nameNativeExtras);
    if(!mapDate.isEmpty()) {
        static const QRegularExpression format("^[0-9]{4}-[0-9]{2}-[0-9]{2}$");
        if(!format.match(mapDate).hasMatch())throw std::invalid_argument("Invalid map label date");
        const int year=mapDate.left(4).toInt();
        for(const auto& transition:record.nameTimeline) {
            if(!transition.fromDate.isEmpty()?transition.fromDate>mapDate:transition.fromYear>year)break;
            if(!transition.ko.isEmpty())ko=transition.ko;
            if(!transition.en.isEmpty())en=transition.en;
            if(!transition.native.isEmpty()) {
                nativeNames={transition.native};
                if(transition.nativeExtras)nativeNames.append(*transition.nativeExtras);
            }
        }
    }
    QSet<QString> seen;std::vector<PlaceDisplayRow> rows;
    const auto add=[&](const QString& language,const QString& value) {
        const auto name=value.trimmed();
        const auto key=name.normalized(QString::NormalizationForm_KC).toLower();
        if(name.isEmpty()||seen.contains(key))return;
        seen.insert(key);rows.push_back({language,name});
    };
    if(selected.ko)add("ko",ko);
    if(selected.en)add("en",en);
    if(selected.native)for(const auto& name:nativeNames)add("native",name);
    return rows;
}
