#include "territorial_fixture.h"
#include "territorialgeometry.h"
#include "territorialpreviewruntime.h"
#include "splitgeometrynormalizer.h"
#include "riverareacalculator.h"
#include "projectcodec.h"
#include "geometrycalculator.h"
#include "geometryruntime_p.h"
#include "riverpartitioncalculator.h"
#include <pandoeditor/project.h>
#include <pandoeditor/geometrypredicates.h>
#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <cstring>
using namespace pandoeditor;
namespace {
bool exactPreviewGeometry(const Geometry& a,const Geometry& b) {
    const auto ring=[](const Ring& a,const Ring& b) {
        if(a.size()!=b.size())return false;
        for(std::size_t i=0;i<a.size();++i)if(std::memcmp(&a[i].x,&b[i].x,sizeof(double))||std::memcmp(&a[i].y,&b[i].y,sizeof(double)))return false;
        return true;
    };
    if(a.type!=b.type||!ring(a.points,b.points)||a.lines.size()!=b.lines.size()||a.polygons.size()!=b.polygons.size())return false;
    for(std::size_t i=0;i<a.lines.size();++i)if(!ring(a.lines[i],b.lines[i]))return false;
    for(std::size_t p=0;p<a.polygons.size();++p) {
        if(a.polygons[p].size()!=b.polygons[p].size())return false;
        for(std::size_t r=0;r<a.polygons[p].size();++r)if(!ring(a.polygons[p][r],b.polygons[p][r]))return false;
    }
    return true;
}
GeometryOperationStatus previewStatus(RiverPartitionStatus status) {
    switch(status) {
    case RiverPartitionStatus::Completed:return GeometryOperationStatus::Completed;
    case RiverPartitionStatus::Cancelled:return GeometryOperationStatus::Cancelled;
    case RiverPartitionStatus::Failed:return GeometryOperationStatus::Failed;
    }
    throw std::logic_error("invalid test status");
}
Geometry rectangle(double x,double y,double width,double height) {
    Geometry geometry;geometry.polygons={{{{x,y},{x+width,y},{x+width,y+height},{x,y+height},{x,y}}}};return geometry;
}
ProjectDocument fixture() {
    return ProjectDocument({{"A","A",rectangle(0,0,10,10).polygons,0xabcdef},
                            {"B","B",rectangle(10,0,10,10).polygons,0x123456}},{{"countries","Countries"}});
}
void addUnit(ProjectDocument& document,const std::string& id,Geometry geometry,const std::string& parent="") {
    const GeometryRef ref{id,1};document.geometries.insert(ref,std::move(geometry));
    appendTerritory(document,{id,id,"",UnitKind::General,false},ref,parent);
    document.presentation.objectStyles[territorialRef(id)]={};
}
AnnexGeometryPreviewResult preview(Project& project,const AnnexGeometryPreviewRequest& request) {
    JobScheduler jobs;const auto ticket=jobs.enqueue(project.snapshot(),"annex-preview");jobs.takeNext();
    return calculateAnnexGeometryPreview(project.snapshot(),request,territorialPreviewCalculators(),ticket.token());
}
PrepareResult prepare(Project& project,const AnnexGeometryPreviewResult& result) {
    JobScheduler jobs;const auto ticket=jobs.enqueue(project.snapshot(),"annex-commit");jobs.takeNext();
    return prepareAnnexGeometryCommit(project.snapshot(),result,ticket.token());
}
Geometry webSmall(double x,double areaM2) {
    const auto size=std::sqrt(areaM2)/111195.08;return rectangle(x,0.002,size,size);
}
// At the equator this dyadic width and exact binary height produce precisely
// 1.0 under the pinned ((width * height) * meters) * meters arithmetic. Dyadic
// longitude offsets preserve the width after clipping and local translation.
constexpr double squareMetreHeight=0x1.63b435411bb7ep-16;
Geometry exactSquareMetrePiece(int longitudeSlot,double height=squareMetreHeight) {
    return rectangle(std::ldexp(double(longitudeSlot),-10),-height/2,std::ldexp(1.,-18),height);
}
Geometry selectionWithoutPieces(const Geometry& source,const std::vector<Geometry>& pieces) {
    Geometry holes;for(const auto& piece:pieces)holes.polygons.insert(holes.polygons.end(),piece.polygons.begin(),piece.polygons.end());
    const auto selected=calculateRiverGeometryIntermediate({GeometryOperation::Difference,source,holes});
    if(!selected.succeeded())throw std::runtime_error(selected.detail);return selected.geometry;
}
bool samePolygonCoverage(const Geometry& left,const Geometry& right) {
    return calculateGeometry({GeometryOperation::Difference,left,right}).status==GeometryOperationStatus::Empty
        &&calculateGeometry({GeometryOperation::Difference,right,left}).status==GeometryOperationStatus::Empty;
}
AnnexGeometryPreviewRequest sliverRequest(const std::vector<Geometry>& leftovers,bool automatic=true) {
    Geometry holes;for(const auto& shape:leftovers)holes.polygons.insert(holes.polygons.end(),shape.polygons.begin(),shape.polygons.end());
    const auto clipped=calculateRiverGeometryIntermediate({GeometryOperation::Difference,rectangle(0,0,.01,.01),holes});
    if(!clipped.succeeded())throw std::runtime_error(clipped.detail);
    AnnexGeometryPreviewRequest request{territorialRef("T"),{territorialRef("D")},clipped.geometry};
    if(automatic)request.riverSliverContext.push_back({"D",0,{}});
    return request;
}
ProjectDocument sliverFixture(Geometry donor=rectangle(0,0,.01,.01)) {
    return ProjectDocument({{"D","D",donor.polygons,0x123456},
                            {"T","T",rectangle(-.02,0,.01,.01).polygons,0xabcdef}},{{"countries","Countries"}});
}
Geometry decodePolygon(const QJsonObject& row) {
    Geometry geometry;geometry.type=row["type"].toString().toStdString();auto polygons=row["coordinates"].toArray();
    if(geometry.type=="Polygon")polygons=QJsonArray{polygons};
    for(const auto& p:polygons) {Polygon polygon;for(const auto& r:p.toArray()) {Ring ring;
        for(const auto& v:r.toArray()) {const auto point=v.toArray();ring.push_back({point[0].toDouble(),point[1].toDouble()});}
        polygon.push_back(std::move(ring));}geometry.polygons.push_back(std::move(polygon));}return geometry;
}
QJsonObject encodePolygon(const Geometry& geometry) {
    QJsonArray polygons;for(const auto& polygon:geometry.polygons) {QJsonArray rings;for(const auto& ring:polygon) {
        QJsonArray points;for(const auto point:ring)points.append(QJsonArray{point.x,point.y});rings.append(points);}polygons.append(rings);}
    return {{"type",QString::fromStdString(geometry.type)},{"coordinates",geometry.type=="Polygon"?polygons[0]:QJsonValue(polygons)}};
}
// Explicit evidence mode, not a skipped/optional CTest. Replays genuine native
// kernels from the pinned payload against actual Chromium annex observations.
int compareBrowserAnnex(const QString& reportPath,const QString& payloadPath,const QString& outputPath) {
    QJsonArray observations;QJsonObject identity;bool all=true;
    try {
        const auto read=[](const QString& path) {QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("EVIDENCE_INPUT_UNAVAILABLE");
            QJsonParseError error;const auto json=QJsonDocument::fromJson(file.readAll(),&error);if(error.error!=QJsonParseError::NoError)throw std::runtime_error("INVALID_EVIDENCE_JSON");return json.object();};
        const auto browser=read(reportPath),payload=read(payloadPath);
        identity=payload["identity"].toObject();
        if(identity.isEmpty()||browser["identity"].toObject()!=identity)throw std::runtime_error("BROWSER_NATIVE_IDENTITY_MISMATCH");
        const auto world=QJsonDocument::fromJson(payload["world"].toObject()["source"].toString().toUtf8()).object()["features"].toArray();
        if(world.size()!=258||browser["annex"].toArray().size()!=4)throw std::runtime_error("INCOMPLETE_BROWSER_ANNEX_CORPUS");
        std::vector<Country> countries;for(const auto& value:world) {const auto feature=value.toObject();
            const auto id=feature["id"].toString().toStdString();countries.push_back({id,id,decodePolygon(feature["geometry"].toObject()).polygons,0xabcdef});}
        for(const auto& value:browser["annex"].toArray()) {
            const auto evidence=value.toObject(),scenario=evidence["selection"].toObject();QJsonObject row{{"name",evidence["name"]}};
            try {
                QJsonObject kernelInput;for(const auto& candidate:payload["cases"].toArray())if(candidate.toObject()["name"]==scenario["base"])kernelInput=candidate.toObject();
                if(kernelInput.isEmpty())throw std::runtime_error("KERNEL_CASE_MISSING");
                const auto kernel=calculateRiverPartitionsJson(QJsonDocument(kernelInput).toJson(QJsonDocument::Compact));
                if(!kernel.succeeded())throw std::runtime_error(kernel.detail.toStdString());
                const bool presentation=scenario["representation"].toString()=="normalized-filtered-presentation";
                const auto& cells=presentation?kernel.presentationCandidates:kernel.candidates;
                QStringList selectedKeys;for(const auto& selected:evidence["selectedCells"].toArray())selectedKeys.push_back(selected.toObject()["key"].toString());
                GeometryOperationRequest united;united.operation=GeometryOperation::Union;
                // Preserve the browser sample order, rather than the kernel's
                // component order, for the multi-operand clipping call.
                bool selectedExact=true;for(const auto& selected:evidence["selectedCells"].toArray()) {
                    const auto cell=std::find_if(cells.begin(),cells.end(),[&](const auto& c){return c.key==selected.toObject()["key"].toString();});
                    if(cell==cells.end())throw std::runtime_error("NATIVE_SELECTED_CELL_MISSING");
                    selectedExact=selectedExact&&cell->attributes==selected.toObject()
                        &&encodePolygon(cell->geometry)==selected.toObject()["geometry"].toObject();united.operands.push_back(cell->geometry);
                }
                // Clipper union accepts a single operand; the app's clip seam
                // intentionally accepts >=2, and union with empty is identical.
                if(united.operands.size()==1)united.operands.push_back(Geometry{});
                const auto selection=calculateRiverGeometryIntermediate(united);if(!selection.succeeded())throw std::runtime_error(selection.detail);
                AnnexGeometryPreviewRequest request;request.target=territorialRef(scenario["targetId"].toString().toStdString());
                request.donors={territorialRef(scenario["donorId"].toString().toStdString())};request.selection=selection.geometry;
                TerritorySelectionRiverSliverContext context;context.donorId=request.donors[0].id;
                for(const auto& cell:cells)if(!selectedKeys.contains(cell.key))context.unselectedGeometries.push_back(cell.geometry);
                request.riverSliverContext.push_back(context);
                QJsonArray unselected;for(const auto& geometry:context.unselectedGeometries)unselected.append(encodePolygon(geometry));
                const auto expectedInput=evidence["input"].toObject();const bool inputExact=encodePolygon(request.selection)==expectedInput["transferredGeometry"].toObject()
                    &&unselected==expectedInput["riverSliverContext"].toArray()[0].toObject()["unselectedGeometries"].toArray();
                Project project;project.replace(ProjectDocument(countries,{{"countries","Countries"}}));const auto before=projectcodec::encode(project);
                const auto result=preview(project,request);const auto expected=evidence["result"].toObject();
                row["selectedCellsExact"]=selectedExact;row["inputExact"]=inputExact;row["previewOk"]=result.ok();row["detail"]=QString::fromStdString(result.detail);
                row["count"]=int(result.autoIncludedSliverCount);row["areaM2"]=result.autoIncludedSliverAreaM2;
                row["countExact"]=int(result.autoIncludedSliverCount)==expected["autoIncludedSlivers"].toObject()["count"].toInt();
                row["areaExact"]=result.autoIncludedSliverAreaM2==expected["autoIncludedSlivers"].toObject()["areaM2"].toDouble();
                row["transferExact"]=encodePolygon(result.transferredGeometry)==expected["transferredGeometry"].toObject();
                bool rowsExact=result.rows.size()==std::size_t(expected["features"].toArray().size()+expected["removedIds"].toArray().size());
                for(const auto& changed:result.rows) {
                    if(!changed.after) {rowsExact=rowsExact&&expected["removedIds"].toArray().contains(QString::fromStdString(changed.owner.id));continue;}
                    QJsonObject expectedGeometry;for(const auto& feature:expected["features"].toArray())if(feature.toObject()["id"].toString().toStdString()==changed.owner.id)expectedGeometry=feature.toObject()["geometry"].toObject();
                    rowsExact=rowsExact&&encodePolygon(*changed.after)==expectedGeometry;
                }
                row["rowsExact"]=rowsExact;row["inputUnchanged"]=projectcodec::encode(project)==before;
                auto prepared=prepare(project,result);row["strictPrepareOk"]=prepared.ok();row["strictDetail"]=QString::fromStdString(prepared.detail);
                bool history=false;if(prepared.ok()&&CommandProcessor::confirm(project,*prepared.preview).ok()) {
                    const auto after=projectcodec::encode(project);history=!CommandProcessor::confirm(project,*prepared.preview).ok()&&project.undo()&&projectcodec::encode(project)==before
                        &&project.redo()&&projectcodec::encode(project)==after;
                }
                row["confirmOnceUndoRedoExact"]=history;
                row["passed"]=selectedExact&&inputExact&&result.ok()&&row["countExact"].toBool()&&row["areaExact"].toBool()
                    &&row["transferExact"].toBool()&&rowsExact&&row["inputUnchanged"].toBool()&&history;
            } catch(const std::exception& error) {row["passed"]=false;row["detail"]=error.what();}
            all=all&&row["passed"].toBool();observations.append(row);
        }
    } catch(const std::exception& error) {observations.append(QJsonObject{{"error",error.what()}});all=false;}
    QFile output(outputPath);if(!output.open(QIODevice::WriteOnly))return 2;
    output.write(QJsonDocument(QJsonObject{{"schema","native-browser-annex-preview-v1"},{"identity",identity},{"passed",all},{"observations",observations}}).toJson());
    return all?0:1;
}
}
class TerritorialGeometryPreviewTests:public QObject {
    Q_OBJECT
private slots:
    void sixteenAdjacentOwnersPreserveExactDrafts() {
        const Ring perimeter{{-4,-4},{-2,-4},{0,-4},{2,-4},{4,-4},{4,-2},{4,0},{4,2},{4,4},{2,4},{0,4},{-2,4},{-4,4},{-4,2},{-4,0},{-4,-2}};
        ProjectDocument document({},{});
        SharedBoundaryIntent intent;
        for(std::size_t i=0;i<perimeter.size();++i) {
            Geometry source;source.polygons={{{{0,0},perimeter[i],perimeter[(i+1)%16],{0,0}}}};
            const auto id="owner-"+std::to_string(i);addUnit(document,id,source);
            auto draft=source;draft.polygons[0][0].front()={1,0};draft.polygons[0][0].back()={1,0};
            intent.drafts.push_back({territorialRef(id),draft});
        }
        Project project;project.replace(document);const auto before=projectcodec::encode(project);
        JobScheduler jobs;auto ticket=jobs.enqueue(project.snapshot(),"boundary-sixteen");jobs.takeNext();
        const auto result=calculateBoundaryGeometryPreview(project.snapshot(),intent,territorialPreviewCalculators(),ticket.token());
        QVERIFY2(result.ok(),result.detail.c_str());QVERIFY(!result.blocking());
        QCOMPARE(result.patch().replacements.size(),std::size_t(16));
        for(std::size_t i=0;i<intent.drafts.size();++i) {
            QCOMPARE(result.patch().replacements[i].owner,intent.drafts[i].owner);
            QVERIFY(exactPreviewGeometry(result.patch().replacements[i].geometry,intent.drafts[i].geometry));
        }
        QCOMPARE(projectcodec::encode(project),before);
    }
    void runtimeFactoryExactlyForwardsActualPreviewKernels() {
        const auto& runtime=territorialPreviewCalculators();
        QVERIFY(&runtime==&territorialPreviewCalculators());
        QVERIFY(runtime.geometry.clip&&runtime.geometry.clipRiverIntermediate&&runtime.geometry.wrap&&runtime.geometry.normalizeClipped);
        QVERIFY(runtime.normalizeRaw&&runtime.normalizeRiver&&runtime.areaKm2);
        auto crossing=rectangle(179,-2,2,4);
        crossing.polygons[0][0][2].x=-179;crossing.polygons[0][0][3].x=-179;
        Geometry malformed;malformed.type="Polygon";malformed.polygons={{{{0,0},{1,1},{0,0}}}};
        const auto microscopic=rectangle(-0.0,0,1e-12,1e-12);
        for(const auto& input:{rectangle(-0.0,0,1,1),crossing,Geometry{},microscopic,malformed})for(const bool cancelled:{false,true}) {
            const auto before=input;
            const GeometryCancellation cancellation=cancelled?GeometryCancellation{[]{return true;}}:GeometryCancellation{};
            const auto raw=normalizeSplitRawGeometry(input,cancellation);
            const auto adaptedRaw=runtime.normalizeRaw(input,cancellation);
            QCOMPARE(adaptedRaw.status,raw.status);QCOMPARE(adaptedRaw.detail,raw.detail);QVERIFY(exactPreviewGeometry(adaptedRaw.geometry,raw.geometry));
            const auto river=normalizeRiverGeometry(input,cancellation);
            const auto adaptedRiver=runtime.normalizeRiver(input,cancellation);
            QCOMPARE(adaptedRiver.status,previewStatus(river.status));QCOMPARE(adaptedRiver.detail,river.detail.toStdString());
            QCOMPARE(adaptedRiver.geometry.has_value(),river.geometry.has_value());
            if(river.geometry)QVERIFY(exactPreviewGeometry(*adaptedRiver.geometry,*river.geometry));
            const auto area=calculateRiverAreaKm2(input,cancellation);
            const auto adaptedArea=runtime.areaKm2(input,cancellation);
            QCOMPARE(adaptedArea.status,previewStatus(area.status));QCOMPARE(adaptedArea.detail,area.detail.toStdString());
            QVERIFY(std::memcmp(&adaptedArea.areaKm2,&area.areaKm2,sizeof(double))==0);
            QVERIFY(exactPreviewGeometry(input,before));
            if(cancelled) {
                QCOMPARE(adaptedRaw.status,GeometryOperationStatus::Cancelled);
                QCOMPARE(adaptedRiver.status,GeometryOperationStatus::Cancelled);
                QCOMPARE(adaptedArea.status,GeometryOperationStatus::Cancelled);
            }
        }
        const auto dropped=runtime.normalizeRiver(microscopic,{});
        QCOMPARE(dropped.status,GeometryOperationStatus::Completed);QVERIFY(!dropped.geometry);
    }
    void rawRiverIntermediateRetainsMicroscopicPositiveRemnant() {
        const auto donor=rectangle(20,45,.01,.01);auto selected=donor;
        selected.polygons[0][0]={{20+1e-7,45},{20.01,45},{20.01,45.01},{20,45.01},{20,45+1e-7},{20+1e-7,45}};
        const GeometryOperationRequest request{GeometryOperation::Difference,donor,selected};
        const auto strict=calculateGeometry(request);QCOMPARE(strict.status,GeometryOperationStatus::Failed);
        const auto intermediate=calculateRiverGeometryIntermediate(request);
        QVERIFY2(intermediate.succeeded(),intermediate.detail.c_str());QCOMPARE(intermediate.geometry.polygons.size(),std::size_t(1));
        QVERIFY_EXCEPTION_THROWN((GeometryStore{}.insert({"tiny",1},intermediate.geometry)),std::invalid_argument);
        const auto reunited=calculateRiverGeometryIntermediate({GeometryOperation::Union,selected,intermediate.geometry});
        QVERIFY2(reunited.succeeded(),reunited.detail.c_str());
        const auto missing=calculateGeometry({GeometryOperation::Difference,donor,reunited.geometry});QCOMPARE(missing.status,GeometryOperationStatus::Empty);
    }
    void riverSliversAreAddedToAuthoritativeTransferAndStrictReceipt() {
        Project project;project.replace(sliverFixture());const auto before=projectcodec::encode(project);
        const auto result=preview(project,sliverRequest({webSmall(.002,.2),webSmall(.004,.3)}));
        QVERIFY2(result.ok(),result.detail.c_str());QCOMPARE(result.autoIncludedSliverCount,std::size_t(2));
        QVERIFY(std::abs(result.autoIncludedSliverAreaM2-.5)<.001);QCOMPARE(result.removedRoots,std::vector<ObjectRef>{territorialRef("D")});
        QCOMPARE(calculateGeometry({GeometryOperation::Difference,rectangle(0,0,.01,.01),result.transferredGeometry}).status,GeometryOperationStatus::Empty);
        QCOMPARE(projectcodec::encode(project),before);auto committed=prepare(project,result);QVERIFY2(committed.ok(),committed.detail.c_str());
        QVERIFY(CommandProcessor::confirm(project,*committed.preview).ok());QVERIFY(!CommandProcessor::confirm(project,*committed.preview).ok());
        const auto after=projectcodec::encode(project);QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),before);QVERIFY(project.redo());QCOMPARE(projectcodec::encode(project),after);
        Project control;control.replace(sliverFixture());const auto raw=preview(control,sliverRequest({webSmall(.002,.2),webSmall(.004,.3)},false));
        QVERIFY2(raw.ok(),raw.detail.c_str());QCOMPARE(raw.autoIncludedSliverCount,std::size_t(0));QVERIFY(raw.removedRoots.empty());
    }
    void riverContextClampsMeaningfulOverrunOnlyForSelectedDonors() {
        Project project;project.replace(fixture());AnnexGeometryPreviewRequest request{territorialRef("A"),{territorialRef("B")},rectangle(9,0,6,10)};
        request.riverSliverContext={{"unrelated",0,{}}};QCOMPARE(preview(project,request).detail,std::string("SELECTION_OUTSIDE_DONOR"));
        request.riverSliverContext={{"B",0,{}}};const auto result=preview(project,request);QVERIFY2(result.ok(),result.detail.c_str());
        QCOMPARE(planarArea(result.transferredGeometry),50.);QVERIFY(prepare(project,result).ok());
    }
    void rawRiverIntermediateRejectsInvalidShapesAndCancellation() {
        const auto good=rectangle(0,0,1,1);
        std::vector<Geometry> invalid;
        auto shape=good;shape.polygons[0][0].pop_back();invalid.push_back(shape);
        shape=good;shape.polygons[0][0][1].x=std::numeric_limits<double>::infinity();invalid.push_back(shape);
        shape=good;shape.polygons[0][0][1].y=std::numeric_limits<double>::quiet_NaN();invalid.push_back(shape);
        shape=good;shape.points.push_back({0,0});invalid.push_back(shape);
        shape=good;shape.lines.push_back({{0,0},{1,1}});invalid.push_back(shape);
        shape=good;shape.type="Point";invalid.push_back(shape);
        shape=good;shape.type="Polygon";shape.polygons.push_back(shape.polygons.front());invalid.push_back(shape);
        shape=good;shape.polygons[0].clear();invalid.push_back(shape);
        shape=good;shape.polygons[0][0]={{0,0},{1,0},{2,0},{0,0}};invalid.push_back(shape);
        for(const auto& bad:invalid) {
            const auto result=calculateRiverGeometryIntermediate({GeometryOperation::Intersection,bad,good});
            QCOMPARE(result.status,GeometryOperationStatus::Failed);QVERIFY(result.geometry.polygons.empty());
        }
        const auto cancelled=calculateRiverGeometryIntermediate({GeometryOperation::Union,good,good},[]{return true;});
        QCOMPARE(cancelled.status,GeometryOperationStatus::Cancelled);QVERIFY(cancelled.geometry.polygons.empty());
        int polls=0;const auto afterCall=calculateRiverGeometryIntermediate({GeometryOperation::Union,good,good},[&]{return ++polls>=3;});
        QCOMPARE(afterCall.status,GeometryOperationStatus::Cancelled);QVERIFY(afterCall.geometry.polygons.empty());
    }
    void rawRiverRetryCanBeCancelledWhileOrdinaryClippingStaysStrict() {
        // Genuine geographic sweep failure, upstream issue #115:
        // https://github.com/mfogel/polygon-clipping/issues/115
        const auto shapes=QJsonDocument::fromJson(R"([[[[35.183056,6.658889],[35.182044,6.658552],[35.182071,6.658686],[35.182208,6.658965],[35.182129,6.658979],[35.181683,6.659061],[35.181415,6.659655],[35.181377,6.660006],[35.181306,6.660653],[35.181309,6.661205],[35.181667,6.66125],[35.186649,6.664571],[35.189483,6.665317],[35.188333,6.664167],[35.188333,6.661984],[35.184514,6.660347],[35.183056,6.658889]]],[[[35.193178,6.670678],[35.1875,6.657896],[35.1875,6.659167],[35.188333,6.66],[35.188333,6.664167],[35.190833,6.666667],[35.190833,6.6675],[35.191667,6.668333],[35.191667,6.669167],[35.193178,6.670678]]]])").array();
        GeometryOperationRequest request{GeometryOperation::Union,
            decodePolygon({{"type","Polygon"},{"coordinates",shapes[0]}}),decodePolygon({{"type","Polygon"},{"coordinates",shapes[1]}})};
        const auto strict=calculateGeometry(request);QCOMPARE(strict.status,GeometryOperationStatus::Failed);
        QVERIFY(QString::fromStdString(strict.detail).contains("SweepLine tree"));
        const auto exhausted=calculateRiverGeometryIntermediate(request);QCOMPARE(exhausted.status,GeometryOperationStatus::Failed);
        QCOMPARE(exhausted.detail,strict.detail);
        auto transformed=request;
        for(auto* geometry:{&transformed.left,&transformed.right})for(auto& polygon:geometry->polygons)for(auto& ring:polygon)for(auto& point:ring) {
            point.x=(point.x-35)*.01-20;point.y=(point.y-6)*.01;
        }
        QCOMPARE(calculateGeometry(transformed).status,GeometryOperationStatus::Failed);
        const auto recovered=calculateRiverGeometryIntermediate(transformed);QVERIFY2(recovered.succeeded(),recovered.detail.c_str());
        // Precision 9 and 8 retain the genuine sweep failure; 7 is the first
        // successful upstream retry. This is not a mocked clipping response.
        GeometryOperationRequest rounded=transformed;
        for(auto* geometry:{&rounded.left,&rounded.right})for(auto& polygon:geometry->polygons)for(auto& ring:polygon)for(auto& point:ring) {
            point.x=std::floor(point.x*1e7+.5)/1e7;point.y=std::floor(point.y*1e7+.5)/1e7;
        }
        const auto expected=calculateGeometry(rounded);QVERIFY(expected.succeeded());QCOMPARE(encodePolygon(recovered.geometry),encodePolygon(expected.geometry));
        int polls=0;const auto cancelled=calculateRiverGeometryIntermediate(request,[&]{return ++polls>=5;});
        QCOMPARE(cancelled.status,GeometryOperationStatus::Cancelled);QVERIFY(cancelled.geometry.polygons.empty());
    }
    void originalIslandAndWrongOriginalPolygonCannotBecomeSlivers() {
        auto donor=rectangle(0,0,.01,.01);const auto island=webSmall(.02,.1);donor.polygons.push_back(island.polygons.front());
        Project project;project.replace(sliverFixture(donor));const auto piece=webSmall(.002,.2);
        auto request=sliverRequest({piece});request.riverSliverContext[0].polygonIndex=1;
        const auto wrong=preview(project,request);QVERIFY2(wrong.ok(),wrong.detail.c_str());QCOMPARE(wrong.autoIncludedSliverCount,std::size_t(0));
        QCOMPARE(wrong.rows[1].after->polygons.size(),std::size_t(2));
        request.riverSliverContext[0].polygonIndex=0;const auto actual=preview(project,request);QVERIFY2(actual.ok(),actual.detail.c_str());
        QCOMPARE(actual.autoIncludedSliverCount,std::size_t(1));QCOMPARE(actual.rows[1].after->polygons.size(),std::size_t(1));
        const auto& kept=actual.rows[1].after->polygons[0][0];const auto& original=island.polygons[0][0];QCOMPARE(kept.size(),original.size());
        for(std::size_t i=0;i<kept.size();++i){QCOMPARE(kept[i].x,original[i].x);QCOMPARE(kept[i].y,original[i].y);}
    }
    void unselectedOverlapAndPositiveEdgeProtectPiecesButPointContactDoesNot() {
        Project project;project.replace(sliverFixture());const auto piece=webSmall(.002,.2);
        const auto min=piece.polygons[0][0][0],max=piece.polygons[0][0][2];
        auto request=sliverRequest({piece});
        for(const auto& protectedGeometry:std::vector<Geometry>{piece,
            rectangle((min.x+max.x)/2,min.y,max.x-min.x,max.y-min.y),
            rectangle(max.x,min.y,.001,.001)}) {
            request.riverSliverContext[0].unselectedGeometries={protectedGeometry};
            const auto result=preview(project,request);QVERIFY2(result.ok(),result.detail.c_str());
            QCOMPARE(result.autoIncludedSliverCount,std::size_t(0));QVERIFY(result.rows[1].after);
        }
        request.riverSliverContext[0].unselectedGeometries={rectangle(max.x,max.y,.001,.001)};
        const auto pointOnly=preview(project,request);QVERIFY2(pointOnly.ok(),pointOnly.detail.c_str());
        QCOMPARE(pointOnly.autoIncludedSliverCount,std::size_t(1));
    }
    void pieceAndCumulativeSquareMeterCapsFollowPieceOrder() {
        Project project;project.replace(sliverFixture());std::vector<Geometry> pieces;
        for(int i=0;i<12;++i)pieces.push_back(webSmall(.001+i*.0005,i==11?1.1:.99));
        const auto result=preview(project,sliverRequest(pieces));QVERIFY2(result.ok(),result.detail.c_str());
        QCOMPARE(result.autoIncludedSliverCount,std::size_t(10));QVERIFY(result.autoIncludedSliverAreaM2<=10);
        QCOMPARE(result.rows[1].after->polygons.size(),std::size_t(2));
        for(int i=10;i<12;++i) {
            const auto retained=calculateGeometry({GeometryOperation::Intersection,*result.rows[1].after,pieces[i]});
            QVERIFY(retained.succeeded());QVERIFY(planarArea(retained.geometry)>0);
        }
    }
    void exactOneSquareMetreBoundary_data() {
        QTest::addColumn<int>("heightDirection");QTest::addColumn<double>("expectedArea");QTest::addColumn<int>("expectedCount");
        QTest::newRow("one-height-ulp-below")<<-1<<0x1.ffffffffffffep-1<<1;
        QTest::newRow("exactly-one-square-metre")<<0<<1.0<<1;
        QTest::newRow("one-height-ulp-above")<<1<<0x1.0000000000001p+0<<0;
    }
    void exactOneSquareMetreBoundary() {
        QFETCH(int,heightDirection);QFETCH(double,expectedArea);QFETCH(int,expectedCount);
        const auto height=heightDirection<0?std::nextafter(squareMetreHeight,0.)
            :heightDirection>0?std::nextafter(squareMetreHeight,std::numeric_limits<double>::infinity()):squareMetreHeight;
        const auto meters=6371008.8*std::acos(-1.)/180;
        // Exact equality, deliberately not QTest's fuzzy double comparison.
        QVERIFY(((std::ldexp(1.,-18)*height)*meters)*meters==expectedArea);
        const auto source=rectangle(0,-.001,.01,.002),piece=exactSquareMetrePiece(1,height);
        Project project;project.replace(sliverFixture(source));const auto before=projectcodec::encode(project);
        const auto result=preview(project,{territorialRef("T"),{territorialRef("D")},selectionWithoutPieces(source,{piece}),{{"D",0,{}}}});
        QVERIFY2(result.ok(),result.detail.c_str());QCOMPARE(result.autoIncludedSliverCount,std::size_t(expectedCount));
        QVERIFY(result.autoIncludedSliverAreaM2==(expectedCount?expectedArea:0.));
        if(expectedCount) {
            QCOMPARE(result.removedRoots,std::vector<ObjectRef>{territorialRef("D")});QVERIFY(!result.rows[1].after);
            QVERIFY(samePolygonCoverage(result.transferredGeometry,source));
        } else {
            QVERIFY(result.removedRoots.empty());QVERIFY(result.rows[1].after);QVERIFY(samePolygonCoverage(*result.rows[1].after,piece));
            QCOMPARE(calculateGeometry({GeometryOperation::Intersection,result.transferredGeometry,piece}).status,GeometryOperationStatus::Empty);
        }
        QVERIFY2(prepare(project,result).ok(),"The exact-boundary receipt must remain strict-committable");QCOMPARE(projectcodec::encode(project),before);
    }
    void exactTenSquareMetreBudgetIncludesEqualityAndKeepsNextPiece() {
        const auto source=rectangle(0,-.001,.02,.002);std::vector<Geometry> pieces;
        for(int i=1;i<=11;++i)pieces.push_back(exactSquareMetrePiece(i));
        Project project;project.replace(sliverFixture(source));
        const auto result=preview(project,{territorialRef("T"),{territorialRef("D")},selectionWithoutPieces(source,pieces),{{"D",0,{}}}});
        QVERIFY2(result.ok(),result.detail.c_str());QCOMPARE(result.autoIncludedSliverCount,std::size_t(10));
        QVERIFY(result.autoIncludedSliverAreaM2==10.);QVERIFY(result.rows[1].after);
        QCOMPARE(result.rows[1].after->polygons.size(),std::size_t(1));QVERIFY(samePolygonCoverage(*result.rows[1].after,pieces[10]));
        QVERIFY(samePolygonCoverage(calculateGeometry({GeometryOperation::Difference,source,result.transferredGeometry}).geometry,pieces[10]));
        const auto tenth=calculateGeometry({GeometryOperation::Intersection,result.transferredGeometry,pieces[9]});
        QVERIFY(tenth.succeeded());QVERIFY(samePolygonCoverage(tenth.geometry,pieces[9]));
    }
    void exactBudgetCrossesDonorAndOriginalPolygonBoundaries_data() {
        QTest::addColumn<bool>("reverseDonors");QTest::addColumn<bool>("reverseOriginalPolygons");
        QTest::newRow("A-B-original-order")<<false<<false;
        QTest::newRow("B-A-original-order")<<true<<false;
        QTest::newRow("A-B-reversed-original-order")<<false<<true;
        QTest::newRow("B-A-reversed-original-order")<<true<<true;
    }
    void exactBudgetCrossesDonorAndOriginalPolygonBoundaries() {
        QFETCH(bool,reverseDonors);QFETCH(bool,reverseOriginalPolygons);
        // Each donor owns two original polygons with three unit-area remnants
        // apiece. Original order deliberately differs from longitude order.
        std::array<std::array<int,2>,2> regions{{{{3,0}},{{2,1}}}};
        if(reverseOriginalPolygons)for(auto& pair:regions)std::reverse(pair.begin(),pair.end());
        std::array<Geometry,2> donors;Geometry allSource;std::vector<Geometry> pieces;
        for(std::size_t d=0;d<donors.size();++d)for(const auto region:regions[d]) {
            const auto polygon=rectangle(std::ldexp(double(region),-5),-.001,.01,.002).polygons.front();
            donors[d].polygons.push_back(polygon);allSource.polygons.push_back(polygon);
            for(int i=1;i<=3;++i)pieces.push_back(exactSquareMetrePiece(region*32+i));
        }
        // Construct named donors directly so every timeline binding remains
        // canonical, independently of the requested traversal order.
        auto document=ProjectDocument({{"A","A",donors[0].polygons,0x123456},{"B","B",donors[1].polygons,0x654321},
            {"T","T",rectangle(-.02,0,.01,.01).polygons,0xabcdef}},{{"countries","Countries"}});
        Project project;project.replace(document);const auto before=projectcodec::encode(project);
        std::vector<ObjectRef> order{territorialRef("A"),territorialRef("B")};if(reverseDonors)std::reverse(order.begin(),order.end());
        // Context order is also unrelated to donor or polygon traversal.
        const auto result=preview(project,{territorialRef("T"),order,selectionWithoutPieces(allSource,pieces),
            {{"B",1,{}},{"A",1,{}},{"B",0,{}},{"A",0,{}}}});
        QVERIFY2(result.ok(),result.detail.c_str());QCOMPARE(result.autoIncludedSliverCount,std::size_t(10));
        QVERIFY(result.autoIncludedSliverAreaM2==10.);QCOMPARE(result.affectedDonors,order);
        QCOMPARE(result.removedRoots,std::vector<ObjectRef>{order.front()});QCOMPARE(result.rows.size(),std::size_t(3));
        QVERIFY(!result.rows[1].after);QCOMPARE(result.rows[2].owner,order.back());QVERIFY(result.rows[2].after);
        QCOMPARE(result.rows[2].after->polygons.size(),std::size_t(2));
        const auto lastDonor=reverseDonors?0:1,lastRegion=regions[lastDonor][1];
        Geometry expectedKept;for(const auto slot:{lastRegion*32+2,lastRegion*32+3})expectedKept.polygons.push_back(exactSquareMetrePiece(slot).polygons.front());
        QVERIFY(samePolygonCoverage(*result.rows[2].after,expectedKept));
        const auto remaining=calculateGeometry({GeometryOperation::Difference,allSource,result.transferredGeometry});
        QVERIFY(remaining.succeeded());QVERIFY(samePolygonCoverage(remaining.geometry,expectedKept));
        QVERIFY2(prepare(project,result).ok(),"The cross-boundary receipt must remain strict-committable");QCOMPARE(projectcodec::encode(project),before);
    }
    void unrepresentableProtectedMicroscopicRemnantFailsWithoutDroppingIt() {
        const auto donor=rectangle(20,45,.01,.01);auto selected=donor;
        selected.polygons[0][0]={{20+1e-7,45},{20.01,45},{20.01,45.01},{20,45.01},{20,45+1e-7},{20+1e-7,45}};
        const auto remainder=calculateRiverGeometryIntermediate({GeometryOperation::Difference,donor,selected});QVERIFY(remainder.succeeded());
        Project project;project.replace(sliverFixture(donor));const auto before=projectcodec::encode(project);
        AnnexGeometryPreviewRequest request{territorialRef("T"),{territorialRef("D")},selected,{{"D",0,{remainder.geometry}}}};
        const auto blocked=preview(project,request);QVERIFY(!blocked.ok());QVERIFY(blocked.blocking());
        QCOMPARE(blocked.detail,std::string("RIVER_REMAINDER_NOT_REPRESENTABLE"));QVERIFY(!prepare(project,blocked).ok());QCOMPARE(projectcodec::encode(project),before);
        request.riverSliverContext[0].unselectedGeometries.clear();const auto admitted=preview(project,request);
        QVERIFY2(admitted.ok(),admitted.detail.c_str());QCOMPARE(admitted.autoIncludedSliverCount,std::size_t(1));QVERIFY(prepare(project,admitted).ok());
    }
    void polarAndWideRemnantsStayWithDonor() {
        for(const auto& pair:std::vector<std::pair<Geometry,Geometry>>{
                {rectangle(0,81,.01,.01),rectangle(.002,81.002,.000005,.000005)},
                {rectangle(0,59,3,3),rectangle(.1,60,1.01,1.2e-10)}}) {
            Project project;project.replace(sliverFixture(pair.first));
            const auto selection=calculateRiverGeometryIntermediate({GeometryOperation::Difference,pair.first,pair.second});QVERIFY(selection.succeeded());
            const auto result=preview(project,{territorialRef("T"),{territorialRef("D")},selection.geometry,{{"D",0,{}}}});
            QVERIFY2(result.ok(),result.detail.c_str());QCOMPARE(result.autoIncludedSliverCount,std::size_t(0));QVERIFY(result.rows[1].after);
        }
    }
    void tinyExplicitWholeAndPartialTransfersAreNotDiscarded() {
        const auto donor=rectangle(2,0,.000003,.000003);Project project;project.replace(sliverFixture(donor));
        const auto whole=preview(project,{territorialRef("T"),{territorialRef("D")},donor});
        QVERIFY2(whole.ok(),whole.detail.c_str());QCOMPARE(whole.removedRoots,std::vector<ObjectRef>{territorialRef("D")});
        const auto part=preview(project,{territorialRef("T"),{territorialRef("D")},rectangle(2,0,.0000015,.0000015)});
        QVERIFY2(part.ok(),part.detail.c_str());QVERIFY(part.rows[1].after);QVERIFY(planarArea(part.transferredGeometry)>0);
    }
    // A regression that eagerly creates a CommandPreview would reject each of
    // these before there is real geometry available to render or archive.
    void danglingReferenceHasRealPreviewButStrictCommitRejects_data() {
        QTest::addColumn<QString>("kind");
        for(const auto kind:{"distribution","child-distribution","label","label-settings"})QTest::newRow(kind)<<QString(kind);
    }
    void danglingReferenceHasRealPreviewButStrictCommitRejects() {
        QFETCH(QString,kind);auto document=fixture();
        if(kind=="child-distribution")addUnit(document,"child",rectangle(11,1,2,2),"B");
        if(kind=="distribution"||kind=="child-distribution") {
            DistributionLayer layer;layer.id="statistics";layer.name="Statistics";document.distributionLayers.push_back(layer);
            DistributionEntry entry;entry.id="entry";entry.layerId=layer.id;entry.territory=territorialRef(kind=="child-distribution"?"child":"B");entry.value=1;document.distributionEntries.push_back(entry);
        } else if(kind=="label") {
            Geometry point;point.type="Point";point.points={{15,5}};document.geometries.insert({"label",1},point);
            PlaceLabel label;label.id="label";label.name="B label";label.geometry={"label",1};label.territory=territorialRef("B");document.labels.push_back(label);
        } else document.presentation.webPresentation.labelSettings[territorialRef("B")]={};
        Project project;project.replace(document);const auto before=projectcodec::encode(project);const auto revision=project.revision();
        const auto result=preview(project,{territorialRef("A"),{territorialRef("B")},rectangle(10,0,10,10)});
        QVERIFY2(result.ok(),result.detail.c_str());QVERIFY(!result.blocking());
        QCOMPARE(planarArea(result.transferredGeometry),100.);QCOMPARE(result.affectedDonors,std::vector<ObjectRef>{territorialRef("B")});
        QCOMPARE(result.removedRoots,std::vector<ObjectRef>{territorialRef("B")});QVERIFY(result.rows.size()>=2);
        QCOMPARE(result.rows[0].owner,territorialRef("A"));QVERIFY(result.rows[0].after);QCOMPARE(planarArea(*result.rows[0].after),200.);
        QCOMPARE(result.rows[1].owner,territorialRef("B"));QVERIFY(!result.rows[1].after);
        QCOMPARE(projectcodec::encode(project),before);QCOMPARE(project.revision(),revision);
        const auto committed=prepare(project,result);QVERIFY(!committed.ok());QVERIFY(!committed.preview);
        QVERIFY(!committed.detail.empty());QCOMPARE(projectcodec::encode(project),before);QCOMPARE(project.revision(),revision);QVERIFY(!project.undo());
    }
    void validReceiptCommitsOnceAndUndoRestores() {
        Project project;project.replace(fixture());const auto before=projectcodec::encode(project);
        const auto result=preview(project,{territorialRef("A"),{territorialRef("B")},rectangle(10,0,5,10)});
        QVERIFY2(result.ok(),result.detail.c_str());QCOMPARE(planarArea(result.transferredGeometry),50.);
        auto prepared=prepare(project,result);QVERIFY2(prepared.ok(),prepared.detail.c_str());QCOMPARE(projectcodec::encode(project),before);
        QVERIFY(CommandProcessor::confirm(project,*prepared.preview).ok());QVERIFY(!CommandProcessor::confirm(project,*prepared.preview).ok());
        const auto after=projectcodec::encode(project);QVERIFY(after!=before);QVERIFY(project.undo());QCOMPARE(projectcodec::encode(project),before);
        QVERIFY(project.redo());QCOMPARE(projectcodec::encode(project),after);
    }
    void shapeFailureBlocksWithoutCandidateDocument() {
        Project project;project.replace(fixture());const auto before=projectcodec::encode(project);auto shape=rectangle(10,0,5,10);shape.polygons[0][0].pop_back();
        const auto result=preview(project,{territorialRef("A"),{territorialRef("B")},shape});
        QVERIFY(!result.ok());QVERIFY(result.blocking());QVERIFY(!result.issues.empty());QVERIFY(!prepare(project,result).ok());QCOMPARE(projectcodec::encode(project),before);
    }
    void selectedOrderAndUnaffectedDonorsSurviveCalculation() {
        auto document=fixture();addUnit(document,"remote",rectangle(60,0,10,10));addUnit(document,"C",rectangle(20,0,10,10));
        Project project;project.replace(document);
        const auto result=preview(project,{territorialRef("A"),{territorialRef("remote"),territorialRef("C"),territorialRef("B"),territorialRef("C")},rectangle(10,0,15,10)});
        QVERIFY2(result.ok(),result.detail.c_str());
        QCOMPARE(result.selectedDonors,(std::vector<ObjectRef>{territorialRef("remote"),territorialRef("C"),territorialRef("B")}));
        QCOMPARE(result.affectedDonors,(std::vector<ObjectRef>{territorialRef("C"),territorialRef("B")}));
        QCOMPARE(result.rows.size(),std::size_t(3));QCOMPARE(result.rows[1].owner,territorialRef("C"));QCOMPARE(result.rows[2].owner,territorialRef("B"));
        QCOMPARE(result.removedRoots,std::vector<ObjectRef>{territorialRef("B")});QVERIFY(prepare(project,result).ok());
    }
    void untouchedPolygonVerticesAndBaselineIssuesArePreserved() {
        auto target=rectangle(0,0,10,10);auto targetIsland=rectangle(40,0,2,2).polygons.front();
        targetIsland.front().insert(targetIsland.front().begin()+1,targetIsland.front().front());
        target.polygons.push_back(targetIsland);
        auto donor=rectangle(10,0,10,10);const auto donorIsland=rectangle(60,0,2,2).polygons.front();donor.polygons.push_back(donorIsland);
        ProjectDocument document({{"A","A",target.polygons,0xabcdef},{"B","B",donor.polygons,0x123456}},{{"countries","Countries"}});
        Project project;project.replace(document);
        const auto result=preview(project,{territorialRef("A"),{territorialRef("B")},rectangle(10,0,5,10)});
        QVERIFY2(result.ok(),result.detail.c_str());QVERIFY(result.issues.empty());
        const auto& targetAfter=result.rows[0].after->polygons;const auto& donorAfter=result.rows[1].after->polygons;
        QCOMPARE(targetAfter.size(),std::size_t(2));QCOMPARE(donorAfter.size(),std::size_t(2));
        QCOMPARE(targetAfter.front().front().size(),std::size_t(6));
        for(std::size_t i=0;i<targetIsland.front().size();++i) {
            QCOMPARE(targetAfter.front().front()[i].x,targetIsland.front()[i].x);
            QCOMPARE(targetAfter.front().front()[i].y,targetIsland.front()[i].y);
        }
        for(std::size_t i=0;i<donorIsland.front().size();++i) {
            QCOMPARE(donorAfter.back().front()[i].x,donorIsland.front()[i].x);
            QCOMPARE(donorAfter.back().front()[i].y,donorIsland.front()[i].y);
        }
    }
    void rawSelectionClampsOnlyNumericalFringe() {
        Project project;project.replace(fixture());
        const auto fringe=preview(project,{territorialRef("A"),{territorialRef("B")},rectangle(10-1e-9,0,5+1e-9,10)});
        QVERIFY2(fringe.ok(),fringe.detail.c_str());QCOMPARE(planarArea(fringe.transferredGeometry),50.);
        const auto outside=preview(project,{territorialRef("A"),{territorialRef("B")},rectangle(9,0,6,10)});
        QVERIFY(!outside.ok());QCOMPARE(outside.detail,std::string("SELECTION_OUTSIDE_DONOR"));QVERIFY(!prepare(project,outside).ok());
    }
    void rootPreviewRejectsSiblingAndLockedSources() {
        auto document=fixture();addUnit(document,"child",rectangle(11,1,2,2),"B");Project project;project.replace(document);
        const auto sibling=preview(project,{territorialRef("A"),{territorialRef("child")},rectangle(11,1,2,2)});
        QVERIFY(!sibling.ok());QCOMPARE(sibling.detail,std::string("ANNEX_REQUIRES_ROOT_GENERAL"));
        auto locked=fixture();locked.units[1].locked=true;Project lockedProject;lockedProject.replace(locked);
        const auto forbidden=preview(lockedProject,{territorialRef("A"),{territorialRef("B")},rectangle(10,0,5,10)});
        QVERIFY(!forbidden.ok());QCOMPARE(forbidden.detail,std::string("LOCKED"));
    }
    void staleAndCancelledReceiptsCannotPrepare() {
        Project project;project.replace(fixture());const auto result=preview(project,{territorialRef("A"),{territorialRef("B")},rectangle(10,0,5,10)});QVERIFY(result.ok());
        QVERIFY(project.renameCountry("A","renamed"));const auto before=projectcodec::encode(project);const auto stale=prepare(project,result);
        QVERIFY(!stale.ok());QCOMPARE(stale.error,CommandError::StaleRevision);QCOMPARE(projectcodec::encode(project),before);
        JobScheduler jobs;const auto ticket=jobs.enqueue(project.snapshot(),"cancelled-annex");jobs.takeNext();jobs.cancel(ticket.id());
        const auto fresh=preview(project,{territorialRef("A"),{territorialRef("B")},rectangle(10,0,5,10)});QVERIFY(fresh.ok());
        const auto refused=prepareAnnexGeometryCommit(project.snapshot(),fresh,ticket.token());QVERIFY(!refused.ok());QVERIFY(!refused.preview);QCOMPARE(refused.detail,std::string("CANCELLED"));
        const auto cancelled=calculateAnnexGeometryPreview(project.snapshot(),{territorialRef("A"),{territorialRef("B")},rectangle(10,0,5,10)},territorialPreviewCalculators(),ticket.token());
        QCOMPARE(cancelled.status,GeometryOperationStatus::Cancelled);QVERIFY(!cancelled.ok());QVERIFY(!cancelled.plan);QCOMPARE(projectcodec::encode(project),before);
    }
    void baselineOverlapIsAllowedButNewOverlapBlocks() {
        auto document=fixture();addUnit(document,"C",rectangle(0,0,5,5));Project project;project.replace(document);
        const auto existing=preview(project,{territorialRef("A"),{territorialRef("B")},rectangle(10,0,5,10)});
        QVERIFY2(existing.ok(),existing.detail.c_str());QVERIFY(!existing.blocking());
        // C overlaps B initially. Transferring that area would introduce A/C
        // overlap, even though total land union is unchanged.
        auto overlapping=fixture();addUnit(overlapping,"C",rectangle(10,0,5,5));Project other;other.replace(overlapping);
        const auto introduced=preview(other,{territorialRef("A"),{territorialRef("B")},rectangle(10,0,5,10)});
        QVERIFY(!introduced.ok());QVERIFY(introduced.blocking());QVERIFY(!introduced.issues.empty());QVERIFY(!prepare(other,introduced).ok());
    }
};
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(argc==5&&QString::fromLocal8Bit(argv[1])=="--browser-annex")
        return compareBrowserAnnex(QString::fromLocal8Bit(argv[2]),QString::fromLocal8Bit(argv[3]),QString::fromLocal8Bit(argv[4]));
    TerritorialGeometryPreviewTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "territorial_geometry_preview_tests.moc"
