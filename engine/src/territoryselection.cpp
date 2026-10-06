#include <pandoeditor/map/territoryselection.h>
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
struct CalculationCancelled {};
void checkpoint(const GeometryCancellation& cancelled) {
    if(cancelled&&cancelled())throw CalculationCancelled{};
}
MaybeGeometry calculate(GeometryOperation operation,std::vector<Geometry> operands,const TerritorySelectionCalculators& calculators,const GeometryCancellation& cancelled,bool riverDerived=false) {
    checkpoint(cancelled);
    if(operands.empty())return {};
    if(operands.size()==1)return std::move(operands.front());
    // The approved web component plan wraps only operands actually entering a
    // Boolean operation. Its one-operand identity path above stays byte-exact.
    for(auto& operand:operands){auto wrapped=calculators.wrap(operand,cancelled);if(wrapped.status==GeometryOperationStatus::Cancelled)throw CalculationCancelled{};if(!wrapped.succeeded())throw std::runtime_error(wrapped.detail);operand=std::move(wrapped.geometry);}
    GeometryOperationRequest request{operation,{},{}};request.operands=std::move(operands);
    GeometryOperationStatus status;Geometry geometry;std::string detail;
    if(riverDerived){auto result=calculators.clipRiverIntermediate(request,cancelled);status=result.status;geometry=std::move(result.geometry);detail=std::move(result.detail);}
    else{auto result=calculators.clip(request,cancelled);status=result.status;geometry=std::move(result.geometry);detail=std::move(result.detail);}
    if(status==GeometryOperationStatus::Cancelled)throw CalculationCancelled{};checkpoint(cancelled);
    if(status==GeometryOperationStatus::Empty)return {};
    if(status!=GeometryOperationStatus::Completed)throw std::runtime_error(detail.empty()?"TERRITORY_SELECTION_CALCULATION_FAILED":detail);
    auto normalized=calculators.normalizeClipped(geometry,cancelled);
    if(normalized.status==GeometryOperationStatus::Cancelled)throw CalculationCancelled{};checkpoint(cancelled);
    if(!normalized.succeeded())throw std::runtime_error(normalized.detail);
    if(normalized.status==GeometryOperationStatus::Empty)return {};
    return std::move(normalized.geometry);
}
MaybeGeometry unite(std::vector<Geometry> geometries,const TerritorySelectionCalculators& calculators,const GeometryCancellation& cancelled,bool riverDerived=false) {
    return calculate(GeometryOperation::Union,std::move(geometries),calculators,cancelled,riverDerived);
}
MaybeGeometry difference(const MaybeGeometry& base,const MaybeGeometry& removed,const TerritorySelectionCalculators& calculators,const GeometryCancellation& cancelled,bool riverDerived=false) {
    checkpoint(cancelled);
    if(!base||!removed)return base;
    return calculate(GeometryOperation::Difference,{*base,*removed},calculators,cancelled,riverDerived);
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
bool riverDerived(const State& state) {
    return state.useRiverBoundaries||std::any_of(state.parts.begin(),state.parts.end(),[](const auto& part){
        return part.component&&part.component->usesRiverBoundary;
    });
}
void prepare(State& state,const TerritorySelectionCalculators& calculators,const GeometryCancellation& cancelled) {
    checkpoint(cancelled);
    const bool normalize=riverDerived(state);
    std::vector<Geometry> archived;
    for(const auto& part:state.parts){checkpoint(cancelled);archived.push_back(part.geometry);}
    state.archivedGeometry=unite(std::move(archived),calculators,cancelled,normalize);
    if(!state.baseSourceGeometry) {
        std::vector<Geometry> source;
        for(const auto& feature:state.sources){checkpoint(cancelled);source.push_back(feature.geometry);}
        state.baseSourceGeometry=unite(std::move(source),calculators,cancelled,normalize);
    }
    state.workingSourceGeometry=difference(state.baseSourceGeometry,state.archivedGeometry,calculators,cancelled,normalize);
    state.components.clear();state.componentFeatures.clear();
    for(const auto& source:state.sources) {
        checkpoint(cancelled);
        TerritorySelectionComponentFeature feature;feature.source=source;
        feature.source.geometry={};feature.source.geometry.type="MultiPolygon";
        for(std::size_t originalIndex=0;originalIndex<source.geometry.polygons.size();++originalIndex) {
            checkpoint(cancelled);
            Geometry original;original.type="Polygon";original.polygons={source.geometry.polygons[originalIndex]};
            const auto remainder=difference(original,state.archivedGeometry,calculators,cancelled,normalize);
            if(!remainder)continue;
            for(std::size_t fragment=0;fragment<remainder->polygons.size();++fragment) {
                checkpoint(cancelled);
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
        state.currentGeometry=unite(operands(state),calculators,cancelled,normalize);
    std::vector<Geometry> combined;
    if(state.archivedGeometry)combined.push_back(*state.archivedGeometry);
    if(state.currentGeometry)combined.push_back(*state.currentGeometry);
    state.combinedGeometry=unite(std::move(combined),calculators,cancelled,normalize);
    // Deliberate web rule: live component selection does not consume the working
    // remainder. Components change workingSourceGeometry only once archived.
    state.remainingGeometry=state.activePhase==Phase::Components?state.workingSourceGeometry
        :difference(state.workingSourceGeometry,state.currentGeometry,calculators,cancelled,normalize);
}
void clear(State& state) {
    state.currentGeometry.reset();state.candidates.clear();state.selectedCandidateIds.clear();
    state.selectedComponentKeys.clear();state.activeMethod=Method::None;state.requestedMethod=Method::None;
    state.activePhase=Phase::None;state.methodChangeConfirmation.reset();
    state.useRiverBoundaries=false;state.riverStatus=TerritoryRiverStatus::Idle;
    state.riverComponents.clear();state.riverPreparationKey.clear();state.riverDetail.clear();
}
void activate(State& state,Method method) {
    clear(state);state.activeMethod=method;
    state.activePhase=method==Method::Components?Phase::Components:Phase::Drawing;
}
void invalidateRiverPreparation(State& state) {
    if(!state.useRiverBoundaries)return;
    state.riverComponents.clear();state.riverPreparationKey.clear();state.riverDetail.clear();
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
bool TerritorySelection::touch(bool rebuildSources) {
    ++state_.revision;state_.derivedRevision.reset();
    state_.currentGeometry.reset();state_.combinedGeometry.reset();state_.remainingGeometry.reset();
    state_.derivedRiverSliverContext.clear();
    if(rebuildSources) {
        state_.archivedGeometry.reset();state_.workingSourceGeometry.reset();
        state_.components.clear();state_.componentFeatures.clear();
    }
    lastError_.clear();return true;
}
bool TerritorySelection::installDerived(TerritorySelectionDerivedResult result) {
    if(result.inputRevision!=state_.revision)return false;
    if(!result.succeeded()) {
        if(result.status==GeometryOperationStatus::Failed)lastError_=std::move(result.detail);
        return false;
    }
    state_.componentFeatures=std::move(result.componentFeatures);state_.components=std::move(result.components);
    state_.baseSourceGeometry=std::move(result.baseSourceGeometry);state_.workingSourceGeometry=std::move(result.workingSourceGeometry);
    state_.archivedGeometry=std::move(result.archivedGeometry);state_.currentGeometry=std::move(result.currentGeometry);
    state_.combinedGeometry=std::move(result.combinedGeometry);state_.remainingGeometry=std::move(result.remainingGeometry);
    state_.derivedRiverSliverContext=std::move(result.riverSliverContext);state_.derivedRevision=state_.revision;
    lastError_.clear();return true;
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
    next.revision=state_.revision;state_=std::move(next);return touch();
}
TerritoryMethodChange TerritorySelection::requestMethod(Method method,bool draftHasWork) {
    if(method==Method::None||state_.sources.empty())return TerritoryMethodChange::Rejected;
    if(state_.activeMethod==method&&state_.activePhase!=Phase::None)return TerritoryMethodChange::Activated;
    state_.requestedMethod=method;
    if(state_.activePhase==Phase::Components&&!state_.selectedComponentKeys.empty()&&state_.activeMethod!=method) {
        lastError_.clear();return TerritoryMethodChange::AwaitingComponentArchive;
    }
    const bool currentWork=state_.currentGeometry||!state_.candidates.empty()||!state_.selectedComponentKeys.empty()||draftHasWork;
    if(state_.activeMethod!=Method::None&&state_.activeMethod!=method&&currentWork) {
        state_.methodChangeConfirmation=method;lastError_.clear();return TerritoryMethodChange::NeedsConfirmation;
    }
    activate(state_,method);touch();return TerritoryMethodChange::Activated;
}
bool TerritorySelection::confirmMethodChange() {
    if(!state_.methodChangeConfirmation)return false;
    const auto method=*state_.methodChangeConfirmation;activate(state_,method);return touch();
}
bool TerritorySelection::cancelMethodChange() {
    if(!state_.methodChangeConfirmation)return false;
    state_.methodChangeConfirmation.reset();state_.requestedMethod=state_.activeMethod;lastError_.clear();return true;
}
void TerritorySelection::cancelRequestedMethod() noexcept {if(!state_.methodChangeConfirmation)state_.requestedMethod=state_.activeMethod;}
bool TerritorySelection::finishArchivedDraft() {
    if(state_.activePhase!=Phase::Drawing||state_.parts.empty())return false;
    state_.activePhase=Phase::Result;state_.candidates.clear();state_.selectedCandidateIds.clear();return touch();
}
bool TerritorySelection::clearCurrent() {clear(state_);return touch();}
bool TerritorySelection::setCandidates(std::vector<TerritorySelectionCandidate> candidates) {
    if(state_.activeMethod!=Method::Line&&state_.activeMethod!=Method::Polygon)return false;
    auto sequence=nextId_;
    const auto calculationId=uid(sequence,"territory-candidates");std::size_t minimum=0;
    try {
        for(std::size_t index=0;index<candidates.size();++index) {
            validate(candidates[index].geometry);candidates[index].id=calculationId+":"+std::to_string(index);
            // Absent area reproduces JS undefined comparison. Equal areas retain
            // the first candidate; neither coordinates nor IDs break a tie.
            if(candidates[index].area&&candidates[minimum].area&&*candidates[index].area<*candidates[minimum].area)minimum=index;
        }
    } catch(const std::exception& error){lastError_=error.what();return false;}
    state_.candidates=std::move(candidates);state_.selectedCandidateIds.clear();state_.selectedComponentKeys.clear();
    if(!state_.candidates.empty())state_.selectedCandidateIds.push_back(state_.candidates[minimum].id);
    state_.activePhase=Phase::Candidate;nextId_=sequence;return touch();
}
TerritorySelectionDraftResult prepareTerritoryPolygonCandidates(const Geometry& drawn,
    const Geometry& workingSource,const Geometry& target,const TerritorySelectionCalculators& calculators,const GeometryCancellation& cancelled) {
    TerritorySelectionDraftResult result;
    try {
        checkpoint(cancelled);validate(drawn);validate(workingSource);validate(target);
        auto transfer=calculate(GeometryOperation::Intersection,{drawn,workingSource},calculators,cancelled);
        transfer=difference(transfer,target,calculators,cancelled);checkpoint(cancelled);
        result.status=transfer?GeometryOperationStatus::Completed:GeometryOperationStatus::Empty;
        if(transfer)result.candidates.push_back({{},std::move(*transfer),{}});
    } catch(const CalculationCancelled&) {result.status=GeometryOperationStatus::Cancelled;}
    catch(const std::exception& error) {result.status=GeometryOperationStatus::Failed;result.detail=error.what();}
    return result;
}
bool TerritorySelection::toggleCandidate(const std::string& id) {
    if(state_.activePhase!=Phase::Candidate||std::none_of(state_.candidates.begin(),state_.candidates.end(),
        [&](const auto& item){return item.id==id;}))return false;
    toggle(state_.selectedCandidateIds,id);return touch();
}
bool TerritorySelection::toggleComponent(const std::string& key) {
    const auto& items=activeItems(state_);
    if(state_.activePhase!=Phase::Components||std::none_of(items.begin(),items.end(),[&](const auto& item){return item.key==key;}))return false;
    toggle(state_.selectedComponentKeys,key);return touch();
}
bool TerritorySelection::toggleRiverBoundaries(bool enabled) {
    if(state_.activePhase!=Phase::Components)return false;
    if(state_.useRiverBoundaries==enabled)return true;
    state_.useRiverBoundaries=enabled;state_.selectedComponentKeys.clear();
    state_.riverStatus=enabled?TerritoryRiverStatus::Pending:TerritoryRiverStatus::Idle;
    state_.riverComponents.clear();state_.riverPreparationKey.clear();state_.riverDetail.clear();return touch();
}
bool TerritorySelection::setRiverStatus(TerritoryRiverStatus status,std::string detail) {
    if(state_.activePhase!=Phase::Components||!state_.useRiverBoundaries
        ||status==TerritoryRiverStatus::Ready)return false;
    state_.riverComponents.clear();state_.riverPreparationKey.clear();
    if(status!=TerritoryRiverStatus::Pending)state_.selectedComponentKeys.clear();
    state_.riverStatus=status;state_.riverDetail=std::move(detail);
    touch();
    if(status!=TerritoryRiverStatus::Pending)lastError_=state_.riverDetail;
    return true;
}
bool TerritorySelection::installRiverComponents(std::vector<TerritorySelectionComponent> items,std::string preparationKey) {
    if(state_.activePhase!=Phase::Components||!state_.useRiverBoundaries||preparationKey.empty())return false;
    if(items.empty()) {
        setRiverStatus(TerritoryRiverStatus::Error,"TERRITORY_SELECTION_NO_RIVER_COMPONENTS");return false;
    }
    std::set<std::string> keys;
    try {
        for(auto& item:items) {
            const auto base=std::find_if(state_.components.begin(),state_.components.end(),[&](const auto& component){
                return component.componentKey==item.componentKey&&component.countryId==item.countryId
                    &&component.sourcePolygonIndex==item.sourcePolygonIndex&&component.polygonIndex==item.polygonIndex;});
            if(item.key.empty()||!keys.insert(item.key).second||base==state_.components.end())
                throw std::invalid_argument("TERRITORY_SELECTION_INVALID_RIVER_PROVENANCE");
            validate(item.geometry);item.usesRiverBoundary=item.partitionKind=="river";item.snapshotId.clear();
        }
    } catch(const std::exception& error){setRiverStatus(TerritoryRiverStatus::Error,error.what());return false;}
    state_.riverComponents=std::move(items);state_.riverPreparationKey=std::move(preparationKey);
    state_.selectedComponentKeys.erase(std::remove_if(state_.selectedComponentKeys.begin(),state_.selectedComponentKeys.end(),
        [&](const auto& key){return !keys.count(key);}),state_.selectedComponentKeys.end());
    state_.riverStatus=TerritoryRiverStatus::Ready;state_.riverDetail.clear();return touch();
}
const std::vector<TerritorySelectionComponent>& TerritorySelection::activeComponents() const noexcept {return activeItems(state_);}
std::vector<Geometry> TerritorySelection::currentOperands() const {return operands(state_);}
TerritoryArchiveReadiness TerritorySelection::archiveReadiness() const noexcept {
    if(state_.activePhase==Phase::Components&&state_.useRiverBoundaries&&state_.riverStatus!=TerritoryRiverStatus::Ready)
        return TerritoryArchiveReadiness::RiverComponentsPending;
    if(!derivedReady())return TerritoryArchiveReadiness::CalculationPending;
    if(!state_.currentGeometry)return TerritoryArchiveReadiness::NoCurrentGeometry;
    if(state_.activePhase!=Phase::Candidate&&state_.activePhase!=Phase::Components)return TerritoryArchiveReadiness::InvalidPhase;
    // User-approved web correction: annex can absorb all remaining source.
    // Bounded creation retains its original nonempty-remainder requirement.
    if(state_.kind!=TerritorySelectionKind::Annex&&!state_.remainingGeometry)return TerritoryArchiveReadiness::BoundedSourceExhausted;
    return TerritoryArchiveReadiness::Ready;
}
bool TerritorySelection::candidateRequiresArchival() const noexcept {return state_.activePhase==Phase::Candidate&&!state_.selectedCandidateIds.empty();}
bool TerritorySelection::addPart() {
    if(archiveReadiness()!=TerritoryArchiveReadiness::Ready)return false;
    auto sequence=nextId_;
    if(state_.activePhase==Phase::Components) {
        const auto& items=activeItems(state_);const auto snapshotId=uid(sequence,"territory-component-snapshot");
        state_.componentSnapshots.push_back({snapshotId,items});
        for(const auto& item:items)if(contains(state_.selectedComponentKeys,item.key)) {
            auto archived=item;archived.snapshotId=snapshotId;
            state_.parts.push_back({uid(sequence,"territory-part"),Method::Components,item.geometry,std::move(archived)});
        }
    } else state_.parts.push_back({uid(sequence,"territory-part"),state_.activeMethod,*state_.currentGeometry,{}});
    nextId_=sequence;clear(state_);return touch(true);
}
bool TerritorySelection::removePart(const std::string& id) {
    const auto found=std::find_if(state_.parts.begin(),state_.parts.end(),[&](const auto& part){return part.id==id;});
    if(found==state_.parts.end())return false;
    state_.parts.erase(found);pruneSnapshots(state_);invalidateRiverPreparation(state_);return touch(true);
}
bool TerritorySelection::undoPart(bool draftHasWork) {
    if(draftHasWork)return false;
    if(!state_.currentGeometry&&state_.parts.empty()&&state_.selectedComponentKeys.empty()&&state_.selectedCandidateIds.empty())return false;
    bool rebuildSources=false;
    if(state_.activePhase==Phase::Candidate&&!state_.selectedCandidateIds.empty()) {
        state_.selectedCandidateIds.pop_back();return touch();
    }
    if(state_.activePhase==Phase::Components&&!state_.selectedComponentKeys.empty())state_.selectedComponentKeys.pop_back();
    else if(state_.currentGeometry)state_.currentGeometry.reset();
    else if(!state_.parts.empty()){state_.parts.pop_back();pruneSnapshots(state_);invalidateRiverPreparation(state_);rebuildSources=true;}
    state_.candidates.clear();state_.selectedCandidateIds.clear();return touch(rebuildSources);
}
std::size_t TerritorySelection::partCount() const noexcept {
    return state_.parts.size()+(state_.activePhase==Phase::Components?state_.selectedComponentKeys.size():state_.activePhase==Phase::Candidate&&!state_.selectedCandidateIds.empty()?1:0);
}
std::vector<std::string> TerritorySelection::donorIds() const {
    std::vector<std::string> result;
    const auto append=[&](const std::string& id){if(!id.empty()&&!contains(result,id))result.push_back(id);};
    for(const auto& source:state_.sources)append(source.ref.id);
    for(const auto& part:state_.parts)if(part.component)append(part.component->countryId);
    for(const auto& item:activeItems(state_))if(contains(state_.selectedComponentKeys,item.key))append(item.countryId);
    return result;
}
namespace {
std::vector<TerritorySelectionRiverSliverContext> sliverContext(const State& state,const TerritorySelectionCalculators& calculators,const GeometryCancellation& cancelled) {
    struct Group {TerritorySelectionRiverSliverContext context;std::set<std::pair<std::string,std::string>> seen;std::vector<Geometry> cells;};
    std::vector<Group> groups;
    std::vector<TerritorySelectionComponent> selected;
    for(const auto& part:state.parts)if(part.component)selected.push_back(*part.component);
    for(const auto& item:activeItems(state))if(contains(state.selectedComponentKeys,item.key))selected.push_back(item);
    for(const auto& item:selected) {
        checkpoint(cancelled);
        if(!item.usesRiverBoundary)continue;
        auto group=std::find_if(groups.begin(),groups.end(),[&](const auto& value){
            return value.context.donorId==item.countryId&&value.context.polygonIndex==item.sourcePolygonIndex;});
        if(group==groups.end()) {groups.push_back({{item.countryId,item.sourcePolygonIndex,{}},{},{}});group=std::prev(groups.end());}
        const auto* items=&activeItems(state);
        if(!item.snapshotId.empty()) {
            const auto snapshot=std::find_if(state.componentSnapshots.begin(),state.componentSnapshots.end(),[&](const auto& value){return value.id==item.snapshotId;});
            if(snapshot==state.componentSnapshots.end())continue;
            items=&snapshot->items;
        }
        for(const auto& other:*items)if(other.countryId==item.countryId&&other.sourcePolygonIndex==item.sourcePolygonIndex
            &&other.key!=item.key&&group->seen.emplace(item.snapshotId,other.key).second)group->cells.push_back(other.geometry);
    }
    std::vector<TerritorySelectionRiverSliverContext> result;
    for(auto& group:groups) {
        for(const auto& cell:group.cells) {
            auto remaining=difference(cell,state.combinedGeometry,calculators,cancelled,true);
            if(remaining)group.context.unselectedGeometries.push_back(std::move(*remaining));
        }
        result.push_back(std::move(group.context));
    }
    return result;
}
}
TerritorySelectionDerivedResult rebuildTerritorySelection(const TerritorySelectionState& input,
    const TerritorySelectionCalculators& calculators,const GeometryCancellation& cancelled) {
    TerritorySelectionDerivedResult result;result.inputRevision=input.revision;
    try {
        checkpoint(cancelled);auto next=input;prepare(next,calculators,cancelled);
        auto context=sliverContext(next,calculators,cancelled);checkpoint(cancelled);
        result.componentFeatures=std::move(next.componentFeatures);result.components=std::move(next.components);
        result.baseSourceGeometry=std::move(next.baseSourceGeometry);result.workingSourceGeometry=std::move(next.workingSourceGeometry);
        result.archivedGeometry=std::move(next.archivedGeometry);result.currentGeometry=std::move(next.currentGeometry);
        result.combinedGeometry=std::move(next.combinedGeometry);result.remainingGeometry=std::move(next.remainingGeometry);
        result.riverSliverContext=std::move(context);result.status=GeometryOperationStatus::Completed;
    } catch(const CalculationCancelled&) {result.status=GeometryOperationStatus::Cancelled;}
    catch(const std::exception& error) {result.status=GeometryOperationStatus::Failed;result.detail=error.what();}
    return result;
}
std::vector<TerritorySelectionRiverSliverContext> TerritorySelection::riverSliverContext() const {
    if(!derivedReady())throw std::logic_error("TERRITORY_SELECTION_CALCULATION_PENDING");
    return state_.derivedRiverSliverContext;
}
}
