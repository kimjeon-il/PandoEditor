#include "builtinworldpolicy.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <stdexcept>

namespace {
using namespace pandoeditor;

struct SubunitPolicy {
    const char* source;
    const char* parent;
    const char* basis;
    const char* note;
    const char* controller;
    const char* lessee;
};
struct MergePolicy {
    const char* source;
    const char* controller;
};

constexpr std::array<SubunitPolicy,47> subunits{{
    {"MAF","FRA","default-map-belonging","","",""},
    {"SXM","NLD","kingdom-constituent","네덜란드 왕국 구성국.","",""},
    {"GIB","GBR","default-map-belonging","","",""},
    {"HKG","CHN","default-map-belonging","","",""},
    {"GRL","DNK","default-map-belonging","","",""},
    {"NCL","FRA","default-map-belonging","","",""},
    {"CUW","NLD","kingdom-constituent","네덜란드 왕국 구성국.","",""},
    {"ABW","NLD","kingdom-constituent","네덜란드 왕국 구성국.","",""},
    {"TCA","GBR","default-map-belonging","","",""},
    {"SPM","FRA","default-map-belonging","","",""},
    {"PCN","GBR","default-map-belonging","","",""},
    {"PYF","FRA","default-map-belonging","","",""},
    {"ATF","FRA","default-map-belonging","","",""},
    {"UMI","USA","default-map-belonging","","",""},
    {"MSR","GBR","default-map-belonging","","",""},
    {"VIR","USA","default-map-belonging","","",""},
    {"BLM","FRA","default-map-belonging","","",""},
    {"PRI","USA","default-map-belonging","","",""},
    {"AIA","GBR","default-map-belonging","","",""},
    {"VGB","GBR","default-map-belonging","","",""},
    {"CYM","GBR","default-map-belonging","","",""},
    {"BMU","GBR","default-map-belonging","","",""},
    {"HMD","AUS","default-map-belonging","","",""},
    {"SHN","GBR","default-map-belonging","","",""},
    {"JEY","GBR","crown-dependency","왕실속령. 영국의 일반 행정구역과 구분합니다.","",""},
    {"GGY","GBR","crown-dependency","왕실속령. 영국의 일반 행정구역과 구분합니다.","",""},
    {"IMN","GBR","crown-dependency","왕실속령. 영국의 일반 행정구역과 구분합니다.","",""},
    {"FRO","DNK","default-map-belonging","","",""},
    {"IOT","GBR","source-snapshot","기본 지도 기준 자료의 영국 소속을 유지합니다. 시점별 주권 관계는 별도 검토 대상입니다.","",""},
    {"NFK","AUS","default-map-belonging","","",""},
    {"WLF","FRA","default-map-belonging","","",""},
    {"SGS","GBR","default-map-belonging","","",""},
    {"FLK","GBR","source-control","기본 지도 분류상 영국 아래에 표시합니다. 영유권 주장을 확정하는 표시는 아닙니다.","",""},
    {"ASM","USA","default-map-belonging","","",""},
    {"GUM","USA","default-map-belonging","","",""},
    {"MNP","USA","default-map-belonging","","",""},
    {"MAC","CHN","default-map-belonging","","",""},
    {"ALD","FIN","default-map-belonging","","",""},
    {"IOA","AUS","default-map-belonging","","",""},
    {"CSI","AUS","default-map-belonging","","",""},
    {"CLP","FRA","default-map-belonging","","",""},
    {"ATC","AUS","default-map-belonging","","",""},
    {"ESB","GBR","sovereign-base-area","영국 주권기지구역.","",""},
    {"WSB","GBR","sovereign-base-area","영국 주권기지구역.","",""},
    {"USG","CUB","legal-belonging","쿠바 귀속. 미국의 통제·임차 관계는 법적 소속과 별도로 기록합니다.","USA","USA"},
    {"KAB","KAZ","legal-belonging","카자흐스탄 귀속. 러시아의 임차 관계는 법적 소속과 별도로 기록합니다.","","RUS"},
    {"PGA","USA","default-map-belonging","","",""},
}};
constexpr std::array<MergePolicy,4> merges{{
    {"BRI","BRA"},{"BJN","COL"},{"SER","COL"},{"SCR","CHN"},
}};

const SubunitPolicy* subunitPolicy(const std::string& source) {
    const auto found=std::find_if(subunits.begin(),subunits.end(),
        [&](const auto& value){return source==value.source;});
    return found==subunits.end()?nullptr:&*found;
}
const MergePolicy* mergePolicy(const std::string& source) {
    const auto found=std::find_if(merges.begin(),merges.end(),
        [&](const auto& value){return source==value.source;});
    return found==merges.end()?nullptr:&*found;
}
void appendPolygons(Geometry& destination,const Geometry& source) {
    destination.type="MultiPolygon";
    destination.polygons.insert(destination.polygons.end(),source.polygons.begin(),source.polygons.end());
}
QJsonObject policyPayload() {
    QJsonArray subunitRows;
    for(const auto& row:subunits) {
        QJsonObject value{{"sourceCountryId",row.source},{"parentId",row.parent},
                          {"id",QString::fromStdString(builtinSubunitId(row.source))},
                          {"relationshipBasis",row.basis}};
        if(*row.controller)value.insert("controllerCountryId",row.controller);
        if(*row.lessee)value.insert("lesseeCountryId",row.lessee);
        subunitRows.append(value);
    }
    QJsonArray mergeRows;
    for(const auto& row:merges)mergeRows.append(QJsonObject{{"sourceId",row.source},{"controller",row.controller}});
    return {{"revision","builtin-subunits-2"},{"subunitCount",int(subunits.size())},
            {"subunits",subunitRows},{"merges",mergeRows}};
}
}

std::string builtinSubunitId(const std::string& sourceId) {
    static constexpr char digits[]="0123456789abcdef";
    std::string suffix;
    suffix.reserve(sourceId.size()*2);
    for(const unsigned char value:sourceId) {
        suffix.push_back(digits[value>>4]);
        suffix.push_back(digits[value&0x0f]);
    }
    return "d34b00a1-9b13-8000-8000-000000"+suffix;
}

BuiltinWorldMaterialization materializeBuiltinWorld(const CanonicalCountryStore& packet) {
    using namespace pandoeditor;
    const auto ids=packet.ids();
    if(ids.size()!=258)throw std::runtime_error("built-in policy requires 258 canonical features");
    const std::set<std::string> available(ids.begin(),ids.end());
    for(const auto& row:subunits)
        if(!available.count(row.source)||!available.count(row.parent))
            throw std::runtime_error("built-in subunit source or parent is missing");
    for(const auto& row:merges)
        if(!available.count(row.source)||!available.count(row.controller))
            throw std::runtime_error("built-in merge source or controller is missing");

    std::map<std::string,TerritorialUnit> units;
    std::map<std::string,Geometry> geometries;
    std::vector<std::string> sourceOrder;
    sourceOrder.reserve(ids.size());
    for(std::size_t index=0;index<ids.size();++index) {
        auto item=packet.materializeUnit(index);
        auto geometry=packet.materializeGeometry(index);
        if(!packet.geometryEquals(index,geometry))
            throw std::runtime_error("canonical country coordinates changed during materialization");
        sourceOrder.push_back(item.id);
        geometries.emplace(item.id,std::move(geometry));
        units.emplace(item.id,std::move(item));
    }
    for(const auto& row:merges) {
        appendPolygons(geometries.at(row.controller),geometries.at(row.source));
        units.erase(row.source);
        geometries.erase(row.source);
    }

    ProjectDocument document({},{{"countries","국가"}});
    document.units.reserve(units.size());
    BuiltinWorldMaterialization result;
    result.ranges.reserve(sourceOrder.size());
    for(const auto& source:sourceOrder) {
        if(const auto* merge=mergePolicy(source)) {
            result.ranges.push_back({source,merge->controller,"world-country-"+std::string(merge->controller)});
            continue;
        }
        auto item=units.at(source);
        std::string owner=source;
        if(const auto* policy=subunitPolicy(source)) {
            owner=builtinSubunitId(source);
            item.id=owner;
            item.kind=UnitKind::General;
            item.baseName.clear();item.nameExplicit=true;
            item.notes=policy->note;
        }
        const auto ref=territorialRef(owner);
        const GeometryRef geometry{"world-country-"+source,1};
        document.geometries.insert(geometry,geometries.at(source));
        const auto* parentPolicy=subunitPolicy(source);
        addStaticTerritorialRecords(document,owner,geometry,parentPolicy?parentPolicy->parent:"","explicit");
        document.presentation.membership.emplace(ref,"countries");
        document.presentation.objectStyles.emplace(ref,ObjectStyle{});
        document.units.push_back(std::move(item));
        result.ranges.push_back({source,owner,"world-country-"+source});
    }
    PreservedExtension policy;
    policy.id="pandoeditor.builtin-territory-policy";
    policy.sourceFormat="world-map";
    policy.sourceSchema=2;
    policy.jsonPointer="/builtinTerritoryPolicy";
    policy.payload=QJsonDocument(policyPayload()).toJson(QJsonDocument::Compact).toStdString();
    // This is immutable provenance for the built-in snapshot.  The native
    // document model already contains the materialized units and relations,
    // so retaining the policy as an archive must not block normal edits.
    policy.status="migrationArchive";
    policy.dependencyKnowledge="known";
    for(const auto& item:document.units)policy.dependencies.push_back(territorialRef(item.id));
    document.extensions.push_back(std::move(policy));
    validateDocument(document);
    result.document=std::move(document);
    return result;
}
