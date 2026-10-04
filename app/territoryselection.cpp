#include "territoryselection.h"
#include "geometrycalculator.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <utility>

namespace pandoeditor {
namespace {
using State=TerritorySelectionState;
using Method=TerritorySelectionMethod;
using Phase=TerritorySelectionPhase;
using MaybeGeometry=std::optional<Geometry>;

bool contains(const std::vector<std::string>& ids,const std::string& id) {
    return std::find(ids.begin(),ids.end(),id)!=ids.end();
}
void toggle(std::vector<std::string>& ids,const std::string& id) {
    const auto found=std::find(ids.begin(),ids.end(),id);
    if(found==ids.end())ids.push_back(id);else ids.erase(found);
}
std::string trim(std::string value) {
    const auto nonspace=[](unsigned char c){return !std::isspace(c);};
    const auto first=std::find_if(value.begin(),value.end(),nonspace);
    const auto last=std::find_if(value.rbegin(),value.rend(),nonspace).base();
    return first<last?std::string(first,last):std::string{};
}
void validate(const Geometry& geometry) {
    if(geometry.type!="Polygon"&&geometry.type!="MultiPolygon")
        throw std::invalid_argument("TERRITORY_SELECTION_POLYGON_REQUIRED");
    GeometryStore validator;validator.insert({"selection",1},geometry);
}
MaybeGeometry calculate(GeometryOperation operation,std::vector<Geometry> operands) {
    if(operands.empty())return {};
    if(operands.size()==1)return std::move(operands.front());
    GeometryOperationRequest request{operation,{},{}};request.operands=std::move(operands);
    auto result=calculateGeometry(request);
    if(!result.succeeded())throw std::runtime_error(result.detail.empty()?"TERRITORY_SELECTION_CALCULATION_FAILED":result.detail);
    if(result.status==GeometryOperationStatus::Empty)return {};
    return std::move(result.geometry);
}
MaybeGeometry unite(std::vector<Geometry> geometries) {
    return calculate(GeometryOperation::Union,std::move(geometries));
}
MaybeGeometry difference(const MaybeGeometry& base,const MaybeGeometry& removed) {
    if(!base||!removed)return base;
    return calculate(GeometryOperation::Difference,{*base,*removed});
}
const std::vector<TerritorySelectionComponent>& activeItems(const State& state) {
    static const std::vector<TerritorySelectionComponent> empty;
    if(state.activePhase!=Phase::Components)return empty;
    if(!state.useRiverBoundaries)return state.components;
    return state.riverStatus==TerritoryRiverStatus::Ready?state.riverComponents:empty;
}
std::vector<Geometry> operands(const State& state) {
    std::vector<Geometry> result;
    if(state.activePhase==Phase::Candidate) {
        for(const auto& item:state.candidates)
            if(contains(state.selectedCandidateIds,item.id))result.push_back(item.geometry);
    } else if(state.activePhase==Phase::Components) {
        for(const auto& item:activeItems(state))
            if(contains(state.selectedComponentKeys,item.key))result.push_back(item.geometry);
    }
    return result;
}
void prepare(State& state) {
    std::vector<Geometry> archived;
    for(const auto& part:state.parts)archived.push_back(part.geometry);
    state.archivedGeometry=unite(std::move(archived));
    if(!state.baseSourceGeometry) {
        std::vector<Geometry> source;
        for(const auto& feature:state.sources)source.push_back(feature.geometry);
        state.baseSourceGeometry=unite(std::move(source));
    }
    state.workingSourceGeometry=difference(state.baseSourceGeometry,state.archivedGeometry);
    state.components.clear();state.componentFeatures.clear();
    for(const auto& source:state.sources) {
        TerritorySelectionComponentFeature feature;feature.source=source;
        feature.source.geometry={};feature.source.geometry.type="MultiPolygon";
        for(std::size_t originalIndex=0;originalIndex<source.geometry.polygons.size();++originalIndex) {
            Geometry original;original.type="Polygon";original.polygons={source.geometry.polygons[originalIndex]};
            const auto remainder=difference(original,state.archivedGeometry);
            if(!remainder)continue;
            for(std::size_t fragment=0;fragment<remainder->polygons.size();++fragment) {
                TerritorySelectionComponent item;
                item.countryId=source.ref.id;item.countryName=source.name;
                item.polygonIndex=feature.source.geometry.polygons.size();item.sourcePolygonIndex=originalIndex;
                item.key="component:"+source.ref.id+":"+std::to_string(originalIndex)+":"+std::to_string(fragment);
                item.componentKey=source.ref.id+":"+std::to_string(item.polygonIndex);
                item.geometry.type="Polygon";item.geometry.polygons={remainder->polygons[fragment]};
                feature.source.geometry.polygons.push_back(remainder->polygons[fragment]);
                feature.sourcePolygonIndices.push_back(originalIndex);
                state.components.push_back(std::move(item));
            }
        }
        if(!feature.source.geometry.polygons.empty())state.componentFeatures.push_back(std::move(feature));
    }
    if(state.activePhase==Phase::Candidate||state.activePhase==Phase::Components)
        state.currentGeometry=unite(operands(state));
    std::vector<Geometry> combined;
    if(state.archivedGeometry)combined.push_back(*state.archivedGeometry);
    if(state.currentGeometry)combined.push_back(*state.currentGeometry);
    state.combinedGeometry=unite(std::move(combined));
    // Deliberate web rule: live component selection does not consume the working
    // remainder. Components change workingSourceGeometry only once archived.
    state.remainingGeometry=state.activePhase==Phase::Components?state.workingSourceGeometry
        :difference(state.workingSourceGeometry,state.currentGeometry);
}
void clear(State& state) {
    state.currentGeometry.reset();state.candidates.clear();state.selectedCandidateIds.clear();
    state.selectedComponentKeys.clear();state.activeMethod=Method::None;state.requestedMethod=Method::None;
    state.activePhase=Phase::None;state.methodChangeConfirmation.reset();
    state.useRiverBoundaries=false;state.riverStatus=TerritoryRiverStatus::Idle;
    state.riverComponents.clear();state.riverPreparationKey.clear();
}
void activate(State& state,Method method) {
    clear(state);state.activeMethod=method;
    state.activePhase=method==Method::Components?Phase::Components:Phase::Drawing;
}
void invalidateRiverPreparation(State& state) {
    if(!state.useRiverBoundaries)return;
    state.riverComponents.clear();state.riverPreparationKey.clear();
    state.riverStatus=TerritoryRiverStatus::Pending;state.currentGeometry.reset();
}
void pruneSnapshots(State& state) {
    state.componentSnapshots.erase(std::remove_if(state.componentSnapshots.begin(),state.componentSnapshots.end(),
        [&](const auto& snapshot){return std::none_of(state.parts.begin(),state.parts.end(),
            [&](const auto& part){return part.component&&part.component->snapshotId==snapshot.id;});}),state.componentSnapshots.end());
}
std::string uid(std::uint64_t& sequence,const char* prefix) {return std::string(prefix)+"-"+std::to_string(++sequence);}
}

TerritorySelection::TerritorySelection(TerritorySelectionKind kind) {state_.kind=kind;}
bool TerritorySelection::commit(State next,std::uint64_t nextId) {
    try {prepare(next);state_=std::move(next);nextId_=nextId;lastError_.clear();return true;}
    catch(const std::exception& error){lastError_=error.what();return false;}
}
bool TerritorySelection::resetSources(std::vector<TerritorySelectionSource> sources) {
    State next;next.kind=state_.kind;std::set<std::string> seen;
    try {
        for(auto& source:sources) {
            source.ref.id=trim(std::move(source.ref.id));
            if(source.ref.id.empty()||!seen.insert(source.ref.id).second)continue;
            validate(source.geometry);next.sources.push_back(std::move(source));
        }
    } catch(const std::exception& error){lastError_=error.what();return false;}
    return commit(std::move(next),nextId_);
}
TerritoryMethodChange TerritorySelection::requestMethod(Method method,bool draftHasWork) {
    if(method==Method::None||state_.sources.empty())return TerritoryMethodChange::Rejected;
    if(state_.activeMethod==method&&state_.activePhase!=Phase::None)return TerritoryMethodChange::Activated;
    auto next=state_;next.requestedMethod=method;
    if(next.activePhase==Phase::Components&&!next.selectedComponentKeys.empty()&&next.activeMethod!=method) {
        state_=std::move(next);lastError_.clear();return TerritoryMethodChange::AwaitingComponentArchive;
    }
    const bool currentWork=next.currentGeometry||!next.candidates.empty()||!next.selectedComponentKeys.empty()||draftHasWork;
    if(next.activeMethod!=Method::None&&next.activeMethod!=method&&currentWork) {
        next.methodChangeConfirmation=method;state_=std::move(next);lastError_.clear();
        return TerritoryMethodChange::NeedsConfirmation;
    }
    activate(next,method);
    return commit(std::move(next),nextId_)?TerritoryMethodChange::Activated:TerritoryMethodChange::Rejected;
}
bool TerritorySelection::confirmMethodChange() {
    if(!state_.methodChangeConfirmation)return false;
    auto next=state_;activate(next,*state_.methodChangeConfirmation);return commit(std::move(next),nextId_);
}
bool TerritorySelection::cancelMethodChange() {
    if(!state_.methodChangeConfirmation)return false;
    state_.methodChangeConfirmation.reset();state_.requestedMethod=state_.activeMethod;lastError_.clear();return true;
}
bool TerritorySelection::clearCurrent() {auto next=state_;clear(next);return commit(std::move(next),nextId_);}
bool TerritorySelection::setCandidates(std::vector<TerritorySelectionCandidate> candidates) {
    if(state_.activeMethod!=Method::Line&&state_.activeMethod!=Method::Polygon)return false;
    auto next=state_;auto sequence=nextId_;
    const auto calculationId=uid(sequence,"territory-candidates");std::size_t minimum=0;
    try {
        for(std::size_t index=0;index<candidates.size();++index) {
            validate(candidates[index].geometry);candidates[index].id=calculationId+":"+std::to_string(index);
            // Absent area reproduces JS undefined comparison. Equal areas retain
            // the first candidate; neither coordinates nor IDs break a tie.
            if(candidates[index].area&&candidates[minimum].area&&*candidates[index].area<*candidates[minimum].area)minimum=index;
        }
    } catch(const std::exception& error){lastError_=error.what();return false;}
    next.candidates=std::move(candidates);next.selectedCandidateIds.clear();next.selectedComponentKeys.clear();
    if(!next.candidates.empty())next.selectedCandidateIds.push_back(next.candidates[minimum].id);
    next.currentGeometry.reset();next.activePhase=Phase::Candidate;
    return commit(std::move(next),sequence);
}
bool TerritorySelection::setDrawnPolygon(const Geometry& drawn,const Geometry& target) {
    if(state_.activeMethod!=Method::Polygon||!state_.workingSourceGeometry)return false;
    try {
        validate(drawn);validate(target);
        auto transfer=calculate(GeometryOperation::Intersection,{drawn,*state_.workingSourceGeometry});
        transfer=difference(transfer,target);
        std::vector<TerritorySelectionCandidate> candidates;
        if(transfer)candidates.push_back({{},std::move(*transfer),{}});
        return setCandidates(std::move(candidates));
    } catch(const std::exception& error){lastError_=error.what();return false;}
}
bool TerritorySelection::toggleCandidate(const std::string& id) {
    if(state_.activePhase!=Phase::Candidate||std::none_of(state_.candidates.begin(),state_.candidates.end(),
        [&](const auto& item){return item.id==id;}))return false;
    auto next=state_;toggle(next.selectedCandidateIds,id);next.currentGeometry.reset();return commit(std::move(next),nextId_);
}
bool TerritorySelection::toggleComponent(const std::string& key) {
    const auto& items=activeItems(state_);
    if(state_.activePhase!=Phase::Components||std::none_of(items.begin(),items.end(),[&](const auto& item){return item.key==key;}))return false;
    auto next=state_;toggle(next.selectedComponentKeys,key);next.currentGeometry.reset();return commit(std::move(next),nextId_);
}
bool TerritorySelection::toggleRiverBoundaries(bool enabled) {
    if(state_.activePhase!=Phase::Components)return false;
    if(state_.useRiverBoundaries==enabled)return true;
    auto next=state_;next.useRiverBoundaries=enabled;next.selectedComponentKeys.clear();next.currentGeometry.reset();
    next.riverStatus=enabled?TerritoryRiverStatus::Pending:TerritoryRiverStatus::Idle;
    next.riverComponents.clear();next.riverPreparationKey.clear();return commit(std::move(next),nextId_);
}
bool TerritorySelection::installRiverComponents(std::vector<TerritorySelectionComponent> items,std::string preparationKey) {
    if(state_.activePhase!=Phase::Components||!state_.useRiverBoundaries||preparationKey.empty())return false;
    std::set<std::string> keys;
    try {
        for(auto& item:items) {
            const auto base=std::find_if(state_.components.begin(),state_.components.end(),[&](const auto& component){
                return component.componentKey==item.componentKey&&component.countryId==item.countryId
                    &&component.sourcePolygonIndex==item.sourcePolygonIndex;});
            if(item.key.empty()||!keys.insert(item.key).second||base==state_.components.end())
                throw std::invalid_argument("TERRITORY_SELECTION_INVALID_RIVER_PROVENANCE");
            validate(item.geometry);item.usesRiverBoundary=item.partitionKind=="river";item.snapshotId.clear();
        }
    } catch(const std::exception& error){lastError_=error.what();return false;}
    auto next=state_;next.riverComponents=std::move(items);next.riverPreparationKey=std::move(preparationKey);
    next.riverStatus=TerritoryRiverStatus::Ready;return commit(std::move(next),nextId_);
}
const std::vector<TerritorySelectionComponent>& TerritorySelection::activeComponents() const noexcept {return activeItems(state_);}
std::vector<Geometry> TerritorySelection::currentOperands() const {return operands(state_);}
TerritoryArchiveReadiness TerritorySelection::archiveReadiness() const noexcept {
    if(state_.activePhase==Phase::Components&&state_.useRiverBoundaries&&state_.riverStatus!=TerritoryRiverStatus::Ready)
        return TerritoryArchiveReadiness::RiverComponentsPending;
    if(!state_.currentGeometry)return TerritoryArchiveReadiness::NoCurrentGeometry;
    if(state_.activePhase!=Phase::Candidate&&state_.activePhase!=Phase::Components)return TerritoryArchiveReadiness::InvalidPhase;
    // User-approved web correction: annex can absorb all remaining source.
    // Bounded creation retains its original nonempty-remainder requirement.
    if(state_.kind!=TerritorySelectionKind::Annex&&!state_.remainingGeometry)return TerritoryArchiveReadiness::BoundedSourceExhausted;
    return TerritoryArchiveReadiness::Ready;
}
bool TerritorySelection::candidateRequiresArchival() const noexcept {return state_.activePhase==Phase::Candidate&&bool(state_.currentGeometry);}
bool TerritorySelection::addPart() {
    if(archiveReadiness()!=TerritoryArchiveReadiness::Ready)return false;
    auto next=state_;auto sequence=nextId_;
    if(next.activePhase==Phase::Components) {
        const auto items=activeItems(next);const auto snapshotId=uid(sequence,"territory-component-snapshot");
        next.componentSnapshots.push_back({snapshotId,items});
        for(auto item:items)if(contains(next.selectedComponentKeys,item.key)) {
            item.snapshotId=snapshotId;
            next.parts.push_back({uid(sequence,"territory-part"),Method::Components,item.geometry,std::move(item)});
        }
    } else next.parts.push_back({uid(sequence,"territory-part"),next.activeMethod,*next.currentGeometry,{}});
    clear(next);return commit(std::move(next),sequence);
}
bool TerritorySelection::removePart(const std::string& id) {
    auto next=state_;const auto found=std::find_if(next.parts.begin(),next.parts.end(),[&](const auto& part){return part.id==id;});
    if(found==next.parts.end())return false;
    next.parts.erase(found);pruneSnapshots(next);invalidateRiverPreparation(next);return commit(std::move(next),nextId_);
}
bool TerritorySelection::undoPart(bool draftHasWork) {
    if(draftHasWork)return false;
    if(!state_.currentGeometry&&state_.parts.empty()&&state_.selectedComponentKeys.empty()&&state_.selectedCandidateIds.empty())return false;
    auto next=state_;
    if(next.activePhase==Phase::Candidate&&!next.selectedCandidateIds.empty()) {
        next.selectedCandidateIds.pop_back();next.currentGeometry.reset();return commit(std::move(next),nextId_);
    }
    if(next.activePhase==Phase::Components&&!next.selectedComponentKeys.empty()) {
        next.selectedComponentKeys.pop_back();next.currentGeometry.reset();
    } else if(next.currentGeometry)next.currentGeometry.reset();
    else if(!next.parts.empty()){next.parts.pop_back();pruneSnapshots(next);invalidateRiverPreparation(next);}
    next.candidates.clear();next.selectedCandidateIds.clear();return commit(std::move(next),nextId_);
}
std::size_t TerritorySelection::partCount() const noexcept {
    return state_.parts.size()+(state_.activePhase==Phase::Components?state_.selectedComponentKeys.size():state_.currentGeometry?1:0);
}
std::vector<std::string> TerritorySelection::donorIds() const {
    std::vector<std::string> result;
    const auto append=[&](const std::string& id){if(!id.empty()&&!contains(result,id))result.push_back(id);};
    for(const auto& source:state_.sources)append(source.ref.id);
    for(const auto& part:state_.parts)if(part.component)append(part.component->countryId);
    for(const auto& item:activeItems(state_))if(contains(state_.selectedComponentKeys,item.key))append(item.countryId);
    return result;
}
std::vector<TerritorySelectionRiverSliverContext> TerritorySelection::riverSliverContext() const {
    struct Group {TerritorySelectionRiverSliverContext context;std::set<std::pair<std::string,std::string>> seen;std::vector<Geometry> cells;};
    std::vector<Group> groups;
    std::vector<TerritorySelectionComponent> selected;
    for(const auto& part:state_.parts)if(part.component)selected.push_back(*part.component);
    for(const auto& item:activeItems(state_))if(contains(state_.selectedComponentKeys,item.key))selected.push_back(item);
    for(const auto& item:selected) {
        if(!item.usesRiverBoundary)continue;
        auto group=std::find_if(groups.begin(),groups.end(),[&](const auto& value){
            return value.context.donorId==item.countryId&&value.context.polygonIndex==item.sourcePolygonIndex;});
        if(group==groups.end()) {groups.push_back({{item.countryId,item.sourcePolygonIndex,{}},{},{}});group=std::prev(groups.end());}
        const auto* items=&activeItems(state_);
        if(!item.snapshotId.empty()) {
            const auto snapshot=std::find_if(state_.componentSnapshots.begin(),state_.componentSnapshots.end(),[&](const auto& value){return value.id==item.snapshotId;});
            if(snapshot==state_.componentSnapshots.end())continue;
            items=&snapshot->items;
        }
        for(const auto& other:*items)if(other.countryId==item.countryId&&other.sourcePolygonIndex==item.sourcePolygonIndex
            &&other.key!=item.key&&group->seen.emplace(item.snapshotId,other.key).second)group->cells.push_back(other.geometry);
    }
    std::vector<TerritorySelectionRiverSliverContext> result;
    for(auto& group:groups) {
        for(const auto& cell:group.cells) {
            auto remaining=difference(cell,state_.combinedGeometry);
            if(remaining)group.context.unselectedGeometries.push_back(std::move(*remaining));
        }
        result.push_back(std::move(group.context));
    }
    return result;
}
}
