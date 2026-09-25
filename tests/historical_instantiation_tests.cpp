#include <pandoeditor/historicalinstantiation.h>
#include <pandoeditor/project.h>
#include <cassert>
#include <stdexcept>

using namespace pandoeditor;
namespace {
Geometry square(double x,double size=2) {
    Geometry g;g.type="Polygon";
    g.polygons.push_back(Polygon{Ring{{x,0},{x+size,0},{x+size,size},{x,size},{x,0}}});
    return g;
}
HistoricalEntity entity(std::string id,UnitKind kind,Geometry geometry) {
    HistoricalEntity e;e.libraryId=std::move(id);e.type=kind;e.canonicalName=e.libraryId;
    HistoricalGeometryVersion v;v.id="v1";v.geometry=std::move(geometry);v.sourceId="pilot";
    e.geometryVersions.push_back(std::move(v));return e;
}
Project project() {
    Project p;ProjectDocument d({{"A","Alpha",square(0,10).polygons,0x123456}},{{"countries","Countries"}});
    p.replace(std::move(d));return p;
}
}
int main() {
    WorldSnapshot snapshot;snapshot.id="snapshot:1945";snapshot.referenceDate="1945";
    snapshot.entityRefs={"historical-country:H","historical-country:I"};
    const HistoricalLibrary catalog(2,{entity("historical-country:H",UnitKind::Country,square(20)),
                                       entity("historical-country:I",UnitKind::Country,square(30)),
                                       entity("historical-subunit:S",UnitKind::Subunit,square(2))},{snapshot});
    auto p=project();
    auto plan=planIndependentHistorical(p.snapshot(),catalog,{{"historical-country:H","1945"},
                                                             {"historical-country:I","1945"}});
    assert(plan.additions.size()==2);
    CommandArguments args;args.action=plan;
    auto request=CommandProcessor::makeRequest(p,"historical.instantiate",args);
    auto prepared=CommandProcessor::prepare(p,request);
    assert(prepared.ok()&&prepared.preview && p.document().units.size()==1);
    assert(prepared.preview->change().after().units.size()==3);
    assert(CommandProcessor::confirm(p,*prepared.preview).changed());
    assert(p.index().objects.count(territorialRef("historical-country:H")));
    const auto& added=p.document().units.at(p.index().objects.at(territorialRef("historical-country:H")));
    assert(added.libraryOrigin&&added.libraryOrigin->libraryId==added.id);
    assert(*added.libraryOrigin->referenceDate=="1945");
    assert(catalog.get("historical-country:H")->geometryVersions.front().geometry.polygons.front().front().front().x==20);
    assert(p.undo()&&p.document().units.size()==1&&!p.index().objects.count(territorialRef("historical-country:H")));
    assert(p.redo()&&p.document().units.size()==3);
    assert(!CommandProcessor::prepare(p,request).ok());
    auto snapshotProject=project();
    auto fromSnapshot=planIndependentHistoricalSnapshot(snapshotProject.snapshot(),catalog,"snapshot:1945");
    assert(fromSnapshot.additions.size()==2);
    assert(fromSnapshot.additions.front().referenceDate==std::optional<std::string>{"1945"});
    args.action=fromSnapshot;
    auto snapshotPreview=CommandProcessor::prepare(snapshotProject,
        CommandProcessor::makeRequest(snapshotProject,"historical.instantiate",args));
    assert(snapshotPreview.ok()&&snapshotPreview.preview);
    assert(CommandProcessor::confirm(snapshotProject,*snapshotPreview.preview).changed());
    assert(snapshotProject.document().units.size()==3);
    assert(snapshotProject.undo()&&snapshotProject.document().units.size()==1);
    bool absentSnapshot=false;
    try { (void)planIndependentHistoricalSnapshot(snapshotProject.snapshot(),catalog,"missing"); }
    catch(const std::invalid_argument&){absentSnapshot=true;}
    assert(absentSnapshot);
    snapshot.entityRefs.push_back("historical-country:missing");
    HistoricalLibrary partial(2,{entity("historical-country:H",UnitKind::Country,square(20)),
                                 entity("historical-country:I",UnitKind::Country,square(30))},{snapshot});
    bool missingEntity=false;
    try { (void)planIndependentHistoricalSnapshot(snapshotProject.snapshot(),partial,"snapshot:1945"); }
    catch(const std::invalid_argument&){missingEntity=true;}
    assert(missingEntity&&snapshotProject.document().units.size()==1);
    auto partialEntity=entity("historical-country:partial",UnitKind::Country,square(40));
    partialEntity.geometryVersions.front().partial=true;
    partialEntity.geometryVersions.front().missingSourceIds={"current-country:missing"};
    HistoricalLibrary partialCatalog(2,{partialEntity},{});
    bool unapproved=false;
    try { (void)planIndependentHistorical(snapshotProject.snapshot(),partialCatalog,
        {{"historical-country:partial","1945"}}); }
    catch(const std::invalid_argument&){unapproved=true;}
    assert(unapproved);
    auto approved=planIndependentHistorical(snapshotProject.snapshot(),partialCatalog,
        {{"historical-country:partial","1945","",{},{},true}});
    assert(approved.additions.front().selection.partial);
    assert(approved.additions.front().selection.missingSourceIds.front()=="current-country:missing");
    args.action=approved;
    auto partialPreview=CommandProcessor::prepare(snapshotProject,
        CommandProcessor::makeRequest(snapshotProject,"historical.instantiate",args));
    assert(partialPreview.ok()&&partialPreview.preview);
    assert(CommandProcessor::confirm(snapshotProject,*partialPreview.preview).changed());
    const auto& partialOrigin=*snapshotProject.document().units.back().libraryOrigin;
    assert(partialOrigin.partial&&partialOrigin.missingLibraryRefs.front()=="current-country:missing");
    assert(snapshotProject.undo());
    auto missingOwnership=false;
    try { (void)planIndependentHistorical(p.snapshot(),catalog,{{"historical-subunit:S","1945"}}); }
    catch(const std::invalid_argument&){missingOwnership=true;}
    assert(missingOwnership);
    auto fresh=project();
    auto sub=planIndependentHistorical(fresh.snapshot(),catalog,
        {{"historical-subunit:S","1945","",territorialRef("A"),territorialRef("A")}});
    args.action=sub;auto subPreview=CommandProcessor::prepare(fresh,CommandProcessor::makeRequest(fresh,"historical.instantiate",args));
    assert(subPreview.ok()&&subPreview.preview);
    assert(CommandProcessor::confirm(fresh,*subPreview.preview).changed());
    assert(effectiveRelation(fresh.document(),"historical-subunit:S",19450101)->parent==territorialRef("A"));
    assert(fresh.undo()&&!fresh.index().objects.count(territorialRef("historical-subunit:S")));
    sub=planIndependentHistorical(fresh.snapshot(),catalog,
        {{"historical-subunit:S","1945","",territorialRef("A"),territorialRef("A")}});
    args.action=sub;
    auto cancelled=CommandProcessor::prepare(fresh,CommandProcessor::makeRequest(fresh,"historical.instantiate",args));
    assert(cancelled.preview);CommandProcessor::cancel(*cancelled.preview);
    assert(!fresh.index().objects.count(territorialRef("historical-subunit:S")));
    auto stale=CommandProcessor::prepare(fresh,CommandProcessor::makeRequest(fresh,"historical.instantiate",args));
    assert(stale.preview);assert(fresh.renameCountry("A","New"));
    assert(CommandProcessor::confirm(fresh,*stale.preview).error==CommandError::StaleRevision);
    auto other=project();args.action=plan;
    assert(CommandProcessor::prepare(other,CommandProcessor::makeRequest(other,"historical.instantiate",args)).error==CommandError::ProjectMismatch);
}
