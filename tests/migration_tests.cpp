#include "projectcodec.h"
#include "presentationmigration.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtTest>

class MigrationTests : public QObject {
    Q_OBJECT
private:
    QByteArray sample() {
        QFile file(":/assets/sample.pando.json");
        if (!file.open(QIODevice::ReadOnly)) qFatal("sample resource unavailable");
        return file.readAll();
    }
private slots:
    void historicalOriginV7RoundTrip() {
        using namespace pandoeditor;
        auto document=projectcodec::decode(sample());
        LibraryOrigin origin;
        origin.libraryId="historical-country:example";
        origin.geometryVersionId="example:1945";
        origin.referenceDate="1945-08-15";
        origin.sourceId="historical-pilot";
        origin.sourceVersion="2";
        origin.certainty="low";
        origin.datePrecision="approximate";
        origin.partial=true;
        document.units.front().libraryOrigin=origin;
        Project project;project.replace(document);
        const auto saved=projectcodec::encode(project);
        QCOMPARE(QJsonDocument::fromJson(saved).object()["version"].toInt(),7);
        const auto reopened=projectcodec::decode(saved);
        QVERIFY(reopened.units.front().libraryOrigin.has_value());
        const auto& actual=*reopened.units.front().libraryOrigin;
        QCOMPARE(actual.libraryId,origin.libraryId);
        QCOMPARE(actual.geometryVersionId,origin.geometryVersionId);
        QVERIFY(actual.referenceDate==origin.referenceDate);
        QCOMPARE(actual.certainty,origin.certainty);
        QCOMPARE(actual.datePrecision,origin.datePrecision);
        QVERIFY(actual.partial);
        QCOMPARE(projectcodec::encode(project),saved);
    }
    void labelAndDistributionSettingsPromoteAndRoundTrip() {
        using namespace pandoeditor;auto d=projectcodec::decode(sample());Geometry point;point.type="Point";point.points={{1,2}};d.geometries.insert({"label-point",1},point);
        PlaceLabel label;label.id="city";label.name="City";label.kind="city";label.geometry={"label-point",1};d.labels.push_back(label);
        PreservedExtension labels;labels.id="label-settings";labels.sourceFormat="pandolab-project";labels.sourceSchema=5;labels.jsonPointer="/labelSettings";labels.payload=R"({"label:city":{"pinned":true,"manualPosition":[10,20],"future":1e+09},"unknown:x":{"pinned":true}})";
        PreservedExtension distribution=labels;distribution.id="distribution-settings";distribution.jsonPointer="/distributionSettings";distribution.payload=R"({"renderMode":"intensity","boundaryVisible":false,"future":[3,1,2]})";
        d.extensions.push_back(labels);d.extensions.push_back(distribution);presentationmigration::promote(d);
        QCOMPARE(d.presentation.webPresentation.labelSettings.at({"label","city"}).manualPosition->x,10.);
        QCOMPARE(d.presentation.webPresentation.distributionSettings.renderMode,DistributionRenderMode::Intensity);QVERIFY(!d.presentation.webPresentation.distributionSettings.boundaryVisible);
        Project project;project.replace(d);const auto saved=projectcodec::encode(project);QVERIFY(saved.contains("1e+09"));QVERIFY(saved.contains("[3,1,2]"));
        const auto reopened=projectcodec::decode(saved);QVERIFY(reopened.presentation.webPresentation.labelSettings.at({"label","city"}).pinned);QCOMPARE(reopened.presentation.webPresentation.distributionSettings.renderMode,DistributionRenderMode::Intensity);
    }
    void contentMetadataDependencyAndPresentationUndo() {
        using namespace pandoeditor;auto d=projectcodec::decode(sample());
        Geometry point;point.type="Point";point.points={{2,3}};d.geometries.insert({"point",1},point);
        PlaceLabel label;label.id="point";label.name="old";label.geometry={"point",1};d.labels.push_back(label);
        d.presentation.webPresentation.hiddenItems["labels"].insert(label.id);
        PreservedExtension extension;extension.id="opaque";extension.payload="{}";extension.forbiddenEffects={"geometry"};extension.dependencies={{"label",label.id}};extension.dependencyKnowledge="known";d.extensions.push_back(extension);
        Project p;p.replace(d);label.name="new";
        CommandArguments args;args.action=ContentEdit{{"label",label.id},label,{}};
        auto renamed=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"content.edit",args));QVERIFY2(renamed.ok(),renamed.detail.c_str());QVERIFY(renamed.preview);QVERIFY(CommandProcessor::confirm(p,*renamed.preview).ok());
        auto roundTrip=projectcodec::decode(projectcodec::encode(p));QVERIFY(!itemVisible(roundTrip.presentation.webPresentation,"labels",label.id));
        args.action=ContentEdit{{"label",label.id},{},{}};
        auto blocked=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"content.edit",args));QVERIFY(!blocked.ok()); // cannot leave an extension dependency dangling
        auto independent=p.document();independent.extensions.clear();p.replace(independent);
        auto deleted=CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"content.edit",args));QVERIFY(deleted.preview);QVERIFY(CommandProcessor::confirm(p,*deleted.preview).ok());
        QVERIFY(p.undo());QVERIFY(!itemVisible(p.document().presentation.webPresentation,"labels",label.id));QCOMPARE(p.document().labels.front().name,std::string("new"));
    }
    void contentPromotionAtomicRoundTrip() {
        using namespace pandoeditor;
        auto d=projectcodec::decode(sample());PreservedExtension labels;
        labels.id="web-labels";labels.sourceFormat="pandolab-project";labels.sourceSchema=5;labels.jsonPointer="/labels";
        labels.payload=R"([{"id":"place","name":"City","kind":"city","coordinates":[10,20],"future":{"number":1e+09,"order":[3,1,2]}}])";
        d.extensions.push_back(labels);QVERIFY(projectcodec::promoteContent(d).empty());QCOMPARE(d.labels.size(),std::size_t(1));
        QCOMPARE(d.extensions.front().status,std::string("migrationArchive"));
        Project p;p.replace(d);auto encoded=projectcodec::encode(p);QVERIFY(encoded.contains("1e+09"));QVERIFY(encoded.contains("[3,1,2]"));
        auto reopened=projectcodec::decode(encoded);QCOMPARE(reopened.labels.size(),std::size_t(1));
        reopened.labels.front().name="Edited";QVERIFY(projectcodec::promoteContent(reopened).empty());QCOMPARE(reopened.labels.front().name,std::string("Edited"));
        auto bad=projectcodec::decode(sample());labels.payload=R"([{"id":"ok","coordinates":[1,2]},{"id":"bad","coordinates":[]}])";bad.extensions.push_back(labels);
        QVERIFY(!projectcodec::promoteContent(bad).empty());QVERIFY(bad.labels.empty());QCOMPARE(bad.extensions.back().payload,labels.payload);
    }
    void contentCommandsPreviewUndoCancelAndStale() {
        using namespace pandoeditor;
        Project p; p.replace(projectcodec::decode(sample()));
        auto prepare=[&](ContentEdit edit) {
            CommandArguments args; args.action=std::move(edit);
            return CommandProcessor::prepare(p,CommandProcessor::makeRequest(p,"content.edit",args));
        };
        PlaceLabel label; label.id="new-label"; label.name="New"; label.geometry={"new-point",1};
        Geometry point; point.type="Point"; point.points={{2,3}};
        auto preview=prepare({{"label",label.id},label,std::make_pair(label.geometry,point),true});
        QVERIFY2(preview.ok(),preview.detail.c_str()); QVERIFY(preview.preview); QVERIFY(p.document().labels.empty());
        QVERIFY(CommandProcessor::confirm(p,*preview.preview).changed()); QCOMPARE(p.document().labels.size(),std::size_t(1));
        QVERIFY(!prepare({{"label",label.id},label,{},true}).ok());
        QVERIFY(p.undo()); QVERIFY(p.document().labels.empty()); QVERIFY(p.redo());
        label.name="Changed"; auto cancelled=prepare({{"label",label.id},label,{}}); QVERIFY(cancelled.preview);
        CommandProcessor::cancel(*cancelled.preview); QCOMPARE(p.document().labels.front().name,std::string("New"));
        auto stale=prepare({{"label",label.id},label,{}}); QVERIFY(stale.preview);
        QVERIFY(p.renameCountry(p.document().units.front().id,"Unrelated edit"));
        QCOMPARE(CommandProcessor::confirm(p,*stale.preview).error,CommandError::StaleRevision);
        auto deletion=prepare({{"label",label.id},{},{}}); QVERIFY(deletion.preview);
        QVERIFY(CommandProcessor::confirm(p,*deletion.preview).changed()); QVERIFY(p.document().labels.empty());
        QVERIFY(p.undo()); QCOMPARE(p.document().labels.front().name,std::string("New"));
        Project reopened; reopened.replace(projectcodec::decode(projectcodec::encode(p)));
        QVERIFY(sameContent(p.document(),reopened.document()));
        DistributionLayer layer; layer.id="locked-layer"; layer.locked=true;
        auto createLayer=prepare({{"distributionLayer",layer.id},layer,{},true}); QVERIFY(createLayer.preview);
        QVERIFY(CommandProcessor::confirm(p,*createLayer.preview).changed());
        auto bypass=layer; bypass.name="Should not change"; bypass.locked=false;
        QCOMPARE(prepare({{"distributionLayer",layer.id},bypass,{}}).error,CommandError::Locked);
        layer.locked=false; auto unlock=prepare({{"distributionLayer",layer.id},layer,{}}); QVERIFY(unlock.preview);
        QVERIFY(CommandProcessor::confirm(p,*unlock.preview).changed());
        auto child=layer; child.id="child-layer"; child.parentId=layer.id;
        auto createChild=prepare({{"distributionLayer",child.id},child,{},true}); QVERIFY(createChild.preview);
        QVERIFY(CommandProcessor::confirm(p,*createChild.preview).changed());
        DistributionEntry entry; entry.id="attached-entry"; entry.layerId=layer.id; entry.territory=territorialRef(p.document().units.front().id);
        auto createEntry=prepare({{"distributionEntry",entry.id},entry,{},true}); QVERIFY(createEntry.preview);
        QVERIFY(CommandProcessor::confirm(p,*createEntry.preview).changed());
        auto removeLayer=prepare({{"distributionLayer",layer.id},{},{}}); QVERIFY(removeLayer.preview);
        QVERIFY(CommandProcessor::confirm(p,*removeLayer.preview).changed());
        QVERIFY(p.document().distributionEntries.empty()); QVERIFY(!p.document().distributionLayers.front().parentId);
        QVERIFY(p.undo()); QCOMPARE(p.document().distributionEntries.size(),std::size_t(1));
        QCOMPARE(*p.document().distributionLayers.back().parentId,layer.id);
    }
    void v6ContentRoundTripAndReferenceValidation() {
        using namespace pandoeditor;
        auto d=projectcodec::decode(sample());
        const auto country=territorialRef(d.units.front().id);
        Geometry point; point.type="Point"; point.points={{1,2}};
        Geometry line; line.type="LineString"; line.lines={{{1,2},{3,4}}};
        d.geometries.insert({"label-geometry",1},point);
        d.geometries.insert({"river-geometry",1},line);
        d.countryDetails[country].capital="Capital text only";
        d.symbols[country]={FlagPolicy::None,{}};
        PlaceLabel label; label.id="place"; label.name="Place"; label.kind="capital";
        label.geometry={"label-geometry",1}; label.territory=country;
        label.source.details=R"({"big":900719925474099312345,"number":1.2300e+02,"ordered":[3,1,2]})";
        d.labels.push_back(label);
        HydroFeature river; river.id="river"; river.geometry={"river-geometry",1}; d.hydro.push_back(river);
        HydroFeature lake; lake.id="lake"; lake.kind="lake"; lake.geometry=d.units.front().geometry; d.hydro.push_back(lake);
        DistributionLayer layer; layer.id="language"; layer.name="Language"; d.distributionLayers.push_back(layer);
        auto child=layer; child.id="child"; child.parentId="language"; d.distributionLayers.push_back(child);
        DistributionEntry entry; entry.id="entry-60"; entry.layerId="language"; entry.territory=country; entry.share=60;
        d.distributionEntries.push_back(entry); entry.id="entry-70"; entry.share=70; d.distributionEntries.push_back(entry);
        GenericFeature generic; generic.id="place"; generic.geometry={"label-geometry",1}; generic.source=label.source;
        d.genericFeatures.push_back(generic);
        for(const auto& type:{"MultiPoint","LineString","MultiLineString","Polygon","MultiPolygon"}) {
            Geometry g; g.type=type;
            if(g.type=="MultiPoint") g.points={{1,2},{3,4}};
            else if(g.type=="LineString" || g.type=="MultiLineString") g.lines={{{1,2},{3,4}}};
            else g.polygons={{{{0,0},{2,0},{2,2},{0,0}}}};
            GenericFeature f; f.id=type; f.geometry={std::string("generic-")+type,1}; f.source=label.source;
            d.geometries.insert(f.geometry,g); d.genericFeatures.push_back(f);
        }
        const auto index=validateDocument(d);
        QVERIFY(index.objects.count({"label","place"})); QVERIFY(index.objects.count({"generic","place"}));
        Project p; p.replace(d);
        const auto bytes=projectcodec::encode(p); QVERIFY(bytes.contains("1.2300e+02"));
        auto reopened=projectcodec::decode(bytes); QVERIFY(sameContent(d,reopened));
        auto futureBytes=bytes;
        futureBytes.replace("\"labels\":[{","\"labels\":[{\"futureField\":{\"n\":9.9900e+03,\"items\":[2,0,1]},");
        Project future; future.replace(projectcodec::decode(futureBytes));
        QVERIFY(future.renameCountry(country.id,"Safe property edit"));
        const auto futureSaved=projectcodec::encode(future);
        QVERIFY(futureSaved.contains("9.9900e+03")); QVERIFY(futureSaved.contains("[2,0,1]"));
        Project futureReopened; futureReopened.replace(projectcodec::decode(futureSaved));
        QCOMPARE(projectcodec::encode(futureReopened),futureSaved);
        QVERIFY(p.renameCountry(country.id,"Changed")); QVERIFY(p.undo()); QVERIFY(sameContent(d,p.document()));
        QVERIFY(p.redo()); QVERIFY(sameContent(d,p.document()));
        auto invalid=d; invalid.distributionLayers.front().parentId="child";
        QVERIFY_EXCEPTION_THROWN(validateDocument(invalid),std::invalid_argument);
        invalid=d; invalid.labels.front().territory=territorialRef("missing");
        QVERIFY_EXCEPTION_THROWN(validateDocument(invalid),std::invalid_argument);
        invalid=d; invalid.distributionEntries.front().share=101;
        QVERIFY_EXCEPTION_THROWN(validateDocument(invalid),std::invalid_argument);
        invalid=d; invalid.hydro.front().kind="lake";
        QVERIFY_EXCEPTION_THROWN(validateDocument(invalid),std::invalid_argument);
        invalid=d; invalid.distributionEntries.front().geometry=d.units.front().geometry;
        QVERIFY_EXCEPTION_THROWN(validateDocument(invalid),std::invalid_argument);
    }
    void v5UnknownPresentationTokensRemainLossless() {
        pandoeditor::Project p;p.replace(projectcodec::decode(sample()));
        auto bytes=projectcodec::encode(p);
        bytes.replace("\"webPresentation\":{","\"webPresentation\":{\"future\":{\"big\":900719925474099312345,\"ordered\":[3,1,2],\"decimal\":1.2300e+02},");
        p.replace(projectcodec::decode(bytes));QVERIFY(p.renameCountry(p.document().units.front().id,"safe edit"));
        auto saved=projectcodec::encode(p);QVERIFY(saved.contains("900719925474099312345"));QVERIFY(saved.contains("1.2300e+02"));QVERIFY(saved.contains("[3,1,2]"));
        pandoeditor::Project reopened;reopened.replace(projectcodec::decode(saved));QCOMPARE(projectcodec::encode(reopened),saved);
    }
    void v5ForeignObjectOrderIsRetainedOutsideCanonicalOrder() {
        try {
            pandoeditor::Project p;p.replace(projectcodec::decode(sample()));
            auto bytes=projectcodec::encode(p);
            bytes.replace("\"objectOrder\":[]","\"objectOrder\":[\"foreign:overlay\"]");
            p.replace(projectcodec::decode(bytes));
            QVERIFY(p.renameCountry(p.document().units.front().id,"safe edit"));
            const auto saved=projectcodec::encode(p);
            QVERIFY(saved.contains("foreign:overlay"));
            QVERIFY(p.document().presentation.webPresentation.objectOrder.empty());
        } catch(const std::exception& error) { QFAIL(error.what()); }
    }
    void v5PresentationPresenceRoundTrip() {
        auto d=projectcodec::decode(sample());
        auto u=d.units.front();u.id="presentation-S";u.kind=pandoeditor::UnitKind::Subunit;
        u.baseName.clear();u.nameExplicit=true;
        const auto ref=pandoeditor::territorialRef(u.id),parent=pandoeditor::territorialRef(d.units.front().id);
        d.units.push_back(u);d.relations.push_back({"presentation-rel",ref,parent,parent});
        d.presentation.membership[ref]=d.presentation.userLayers.front().id;d.presentation.objectStyles[ref]={};
        auto& p=d.presentation.webPresentation;
        p.visibility["subunits"]=false;p.visibility["subunitFlags"]=false;
        p.hiddenItems["subunits"].insert(u.id);p.styles["countries"].opacity=.6;
        p.objectStyles["territorial:subunit:presentation-S"].blendMode="normal";
        p.objectOrder={"territorial:subunit:presentation-S"};
        pandoeditor::Project project;project.replace(d);
        const auto bytes=projectcodec::encode(project);QCOMPARE(QJsonDocument::fromJson(bytes).object()["version"].toInt(),7);
        auto legacy=QJsonDocument::fromJson(bytes).object(); legacy["version"]=5; legacy.remove("content");
        auto reopened=projectcodec::decode(QJsonDocument(legacy).toJson(QJsonDocument::Compact));
        QCOMPARE(reopened.nativeSourceVersion,5); QVERIFY(reopened.presentation.webPresentation==p);
        QVERIFY(!reopened.presentation.webPresentation.objectStyles.at("territorial:subunit:presentation-S").opacity);
        p.objectStyles["territorial:subunit:presentation-S"].opacity=1;
        project.replace(d);reopened=projectcodec::decode(projectcodec::encode(project));
        QCOMPARE(*reopened.presentation.webPresentation.objectStyles.at("territorial:subunit:presentation-S").opacity,1.);
    }
    void v1MinimumDefaultsRemainExactAfterSaving() {
        const auto d=projectcodec::decode("{\"format\":\"pandoeditor-project\",\"version\":1,\"countries\":["
          "{\"id\":\"original-id\",\"name\":\" original name \",\"color\":\"#A1b2C3\","
          "\"geometry\":{\"type\":\"MultiPolygon\",\"coordinates\":[[[[1.125,2.25],[3.5,2.25],[3.5,4.75],[1.125,2.25]]]]}}]}");
        pandoeditor::Project project; project.replace(d);
        const auto reopened=projectcodec::decode(projectcodec::encode(project));
        for (const auto* document:{&d,&reopened}) {
            QCOMPARE(document->units.size(),std::size_t(1));
            const auto& unit=document->units[0];
            QCOMPARE(unit.id,std::string("original-id")); QCOMPARE(unit.name,std::string(" original name "));
            QVERIFY(unit.notes.empty()); QCOMPARE(unit.kind,pandoeditor::UnitKind::Country);
            QVERIFY(!unit.locked); QVERIFY(!unit.validity.from); QVERIFY(!unit.validity.to);
            const auto r=pandoeditor::territorialRef(unit.id);
            QCOMPARE(document->presentation.membership.at(r),std::string("countries"));
            QCOMPARE(document->presentation.objectStyles.at(r).color,std::uint32_t(0xa1b2c3));
            QCOMPARE(document->presentation.objectStyles.at(r).opacity,1.0);
            QCOMPARE(document->presentation.userLayers.size(),std::size_t(1));
            const auto& layer=document->presentation.userLayers[0];
            QCOMPARE(layer.id,std::string("countries")); QCOMPARE(layer.name,std::string("국가"));
            QVERIFY(layer.visible); QVERIFY(!layer.locked); QCOMPARE(layer.opacity,1.0);
            const auto g=document->geometries.get(unit.geometry);
            QCOMPARE(g->polygons.size(),std::size_t(1)); QCOMPARE(g->polygons[0].size(),std::size_t(1));
            const pandoeditor::Ring expected{{1.125,2.25},{3.5,2.25},{3.5,4.75},{1.125,2.25}};
            QCOMPARE(g->polygons[0][0].size(),expected.size());
            for (std::size_t i=0;i<expected.size();++i) {
                QCOMPARE(g->polygons[0][0][i].x,expected[i].x); QCOMPARE(g->polygons[0][0][i].y,expected[i].y);
            }
        }
        QCOMPARE(reopened.documentId,d.documentId);
    }
    void v2PreservesAllPropertiesAndCoordinates() {
        const QByteArray fixture="{\"format\":\"pandoeditor-project\",\"version\":2,"
          "\"layers\":[{\"id\":\"bottom\",\"name\":\" bottom \",\"visible\":false,\"locked\":true,\"opacity\":0.375},"
          "{\"id\":\"middle\",\"name\":\"middle\",\"visible\":false,\"locked\":false,\"opacity\":0.5},"
          "{\"id\":\"top\",\"name\":\"top\",\"visible\":true,\"locked\":false,\"opacity\":0.875}],"
          "\"countries\":[{\"id\":\"stable-id\",\"name\":\" exact name \",\"memo\":\"memo\\nsecond line\",\"color\":\"#12aBcD\",\"opacity\":0.625,\"layerId\":\"top\","
          "\"geometry\":{\"type\":\"MultiPolygon\",\"coordinates\":["
          "[[[0.12345678901234566,0],[4,0],[4,4],[0.12345678901234566,0]],[[1,1],[2,1],[2,2],[1,1]]],"
          "[[[10,10],[14,10],[14,14],[10,10]]]]}},"
          "{\"id\":\"second-id\",\"name\":\"Second\",\"memo\":\"different memo\",\"color\":\"#FEDCBA\",\"opacity\":0.25,\"layerId\":\"bottom\","
          "\"geometry\":{\"type\":\"MultiPolygon\",\"coordinates\":[[[[-10,-10],[-8,-10],[-8,-8],[-10,-10]]]]}}]}";
        const auto document=projectcodec::decode(fixture);
        QCOMPARE(document.units.size(),std::size_t(2));
        QCOMPARE(document.units[0].id,std::string("stable-id"));
        QCOMPARE(document.units[0].name,std::string(" exact name "));
        QCOMPARE(document.units[0].notes,std::string("memo\nsecond line"));
        const auto r=pandoeditor::territorialRef("stable-id");
        QCOMPARE(document.presentation.membership.at(r),std::string("top"));
        QCOMPARE(document.presentation.objectStyles.at(r).color,std::uint32_t(0x12abcd));
        QCOMPARE(document.presentation.objectStyles.at(r).opacity,0.625);
        QCOMPARE(document.presentation.userLayers[0].name,std::string(" bottom "));
        QVERIFY(!document.presentation.userLayers[0].visible);
        QVERIFY(document.presentation.userLayers[0].locked);
        QCOMPARE(document.presentation.userLayers[0].opacity,0.375);
        QCOMPARE(document.presentation.userLayers[1].id,std::string("middle"));
        QCOMPARE(document.presentation.userLayers[2].id,std::string("top"));
        QCOMPARE(document.presentation.userLayers[2].opacity,0.875);
        const auto g=document.geometries.get(document.units[0].geometry);
        QCOMPARE(g->polygons.size(),std::size_t(2)); QCOMPARE(g->polygons[0].size(),std::size_t(2));
        pandoeditor::Project project; project.replace(document);
        const auto saved=projectcodec::encode(project);
        const auto reopened=projectcodec::decode(saved);
        QCOMPARE(reopened.documentId,document.documentId);
        QCOMPARE(reopened.units.size(),document.units.size());
        for (std::size_t i=0;i<document.units.size();++i) {
            const auto& original=document.units[i]; const auto& actual=reopened.units[i];
            QCOMPARE(actual.id,original.id); QCOMPARE(actual.name,original.name); QCOMPARE(actual.notes,original.notes);
            QCOMPARE(actual.kind,original.kind); QCOMPARE(actual.locked,original.locked); QCOMPARE(actual.coverageMode,original.coverageMode);
            QVERIFY(actual.geometry==original.geometry); QVERIFY(actual.validity.from==original.validity.from); QVERIFY(actual.validity.to==original.validity.to);
            const auto ref=pandoeditor::territorialRef(original.id);
            QCOMPARE(reopened.presentation.membership.at(ref),document.presentation.membership.at(ref));
            QCOMPARE(reopened.presentation.objectStyles.at(ref).color,document.presentation.objectStyles.at(ref).color);
            QCOMPARE(reopened.presentation.objectStyles.at(ref).opacity,document.presentation.objectStyles.at(ref).opacity);
        }
        QCOMPARE(reopened.presentation.userLayers.size(),document.presentation.userLayers.size());
        for (std::size_t i=0;i<document.presentation.userLayers.size();++i) {
            const auto& original=document.presentation.userLayers[i]; const auto& actual=reopened.presentation.userLayers[i];
            QCOMPARE(actual.id,original.id); QCOMPARE(actual.name,original.name); QCOMPARE(actual.visible,original.visible);
            QCOMPARE(actual.locked,original.locked); QCOMPARE(actual.opacity,original.opacity);
        }
        for (std::size_t unit=0;unit<document.units.size();++unit) {
            const auto original=document.geometries.get(document.units[unit].geometry);
            const auto back=reopened.geometries.get(reopened.units[unit].geometry);
            QCOMPARE(back->polygons.size(),original->polygons.size());
            for (std::size_t p=0;p<original->polygons.size();++p) {
                QCOMPARE(back->polygons[p].size(),original->polygons[p].size());
                for (std::size_t ring=0;ring<original->polygons[p].size();++ring) {
                    QCOMPARE(back->polygons[p][ring].size(),original->polygons[p][ring].size());
                    for (std::size_t i=0;i<original->polygons[p][ring].size();++i) {
                        QCOMPARE(back->polygons[p][ring][i].x,original->polygons[p][ring][i].x);
                        QCOMPARE(back->polygons[p][ring][i].y,original->polygons[p][ring][i].y);
                    }
                }
            }
        }
        pandoeditor::Project second; second.replace(reopened); QCOMPARE(projectcodec::encode(second),saved);
    }
    void v3HierarchyRoundTripsTypedKindsAndRelations() {
        auto d=projectcodec::decode(sample());
        const auto countryId=d.units[0].id;
        const auto geometry=d.units[0].geometry;
        const auto countryRef=pandoeditor::territorialRef(countryId);
        const auto first=pandoeditor::territorialRef("first-subunit");
        const auto nested=pandoeditor::territorialRef("nested-subunit");
        const auto region=pandoeditor::territorialRef("region");
        d.units.push_back({first.id,"First subunit","first notes",pandoeditor::UnitKind::Subunit,geometry,true,{},"partition"});
        d.units.push_back({nested.id,"Nested subunit","nested notes",pandoeditor::UnitKind::Subunit,geometry,false,{},"explicit"});
        d.units.push_back({region.id,"Region","region notes",pandoeditor::UnitKind::Region,geometry,false,{},"partition"});
        for (const auto& r:{first,nested,region}) {
            d.presentation.membership[r]="countries";
            d.presentation.objectStyles[r]={0xabcdef,0.75};
        }
        d.relations.push_back({"first-base",first,countryRef,countryRef,false,{}});
        d.relations.push_back({"nested-base",nested,first,countryRef,false,{}});
        d.relations.push_back({"region-base",region,nested,countryRef,false,{}});
        pandoeditor::Project project; project.replace(d);
        const auto saved=projectcodec::encode(project);
        const auto reopened=projectcodec::decode(saved);
        QCOMPARE(reopened.units.size(),d.units.size()); QCOMPARE(reopened.relations.size(),d.relations.size());
        for (std::size_t i=0;i<d.units.size();++i) {
            const auto& original=d.units[i]; const auto& actual=reopened.units[i];
            QCOMPARE(actual.id,original.id); QCOMPARE(actual.kind,original.kind); QCOMPARE(actual.name,original.name);
            QCOMPARE(actual.notes,original.notes); QCOMPARE(actual.locked,original.locked); QCOMPARE(actual.coverageMode,original.coverageMode);
            QVERIFY(actual.geometry==original.geometry);
        }
        for (std::size_t i=0;i<d.relations.size();++i) {
            const auto& original=d.relations[i]; const auto& actual=reopened.relations[i];
            QCOMPARE(actual.id,original.id); QVERIFY(actual.unit==original.unit); QVERIFY(actual.parent==original.parent);
            QVERIFY(actual.sovereign==original.sovereign); QCOMPARE(actual.dated,original.dated);
            QVERIFY(actual.validity.from==original.validity.from); QVERIFY(actual.validity.to==original.validity.to);
        }
        const auto index=pandoeditor::validateDocument(reopened);
        QCOMPARE(index.children.at(countryRef).size(),std::size_t(1));
        QVERIFY(index.children.at(first)[0]==nested); QVERIFY(index.children.at(nested)[0]==region);
        pandoeditor::Project second; second.replace(reopened); QCOMPARE(projectcodec::encode(second),saved);
    }
    void temporalWireAndAllGeometryKinds() {
        auto d=projectcodec::decode(sample());
        const auto id=d.units[0].id;
        d.relations.push_back({"dated-original-id",pandoeditor::territorialRef(id),{},pandoeditor::territorialRef(id),true,{std::string("-0044"),std::string("+02026-09-18")}});
        d.geometries.insert({"point",1},{"Point",{{1.25,2.5}},{},{}});
        d.geometries.insert({"multipoint",1},{"MultiPoint",{{1,2},{3,4}},{},{}});
        d.geometries.insert({"line",1},{"LineString",{},{{{1,2},{3,4}}},{}});
        d.geometries.insert({"multiline",1},{"MultiLineString",{},{{{1,2},{3,4}},{{5,6},{7,8}}},{}});
        d.geometries.insert({"polygon",1},{"Polygon",{},{},{{{{0,0},{1,0},{1,1},{0,0}}}}});
        pandoeditor::Project project; project.replace(d);
        auto saved=projectcodec::encode(project);
        const auto root=QJsonDocument::fromJson(saved).object();
        const auto period=root["relations"].toArray()[0].toObject()["validity"].toObject();
        QCOMPARE(period["from"].toObject()["text"].toString(),QString("-0044"));
        QCOMPARE(period["from"].toObject()["precision"].toString(),QString("year"));
        QCOMPARE(period["to"].toObject()["precision"].toString(),QString("date"));
        pandoeditor::Project reopened; reopened.replace(projectcodec::decode(saved));
        QCOMPARE(projectcodec::encode(reopened),saved);
        auto bad=root; auto relations=bad["relations"].toArray(); auto relation=relations[0].toObject();
        auto invalidPeriod=period; auto from=period["from"].toObject(); from["precision"]="date";
        invalidPeriod["from"]=from; relation["validity"]=invalidPeriod; relations[0]=relation; bad["relations"]=relations;
        QVERIFY_EXCEPTION_THROWN(projectcodec::decode(QJsonDocument(bad).toJson()),std::invalid_argument);
    }
    void archiveCannotOverwriteEditedCanonicalFields() {
        auto bytes=sample();
        bytes.insert(bytes.indexOf('{')+1,R"("future":{"name":"original","id":"opaque"},)");
        pandoeditor::Project project; project.replace(projectcodec::decode(bytes));
        const auto id=project.document().units[0].id;
        QVERIFY(project.renameCountry(id,"Edited canonical name"));
        QVERIFY(project.setMemo(id,"Edited canonical notes"));
        QVERIFY(!project.setColor(id,0x123456));
        const auto saved=projectcodec::encode(project);
        const auto d=projectcodec::decode(saved);
        QCOMPARE(d.units[0].name,std::string("Edited canonical name"));
        QCOMPARE(d.units[0].notes,std::string("Edited canonical notes"));
        QCOMPARE(d.extensions[0].sourceSchema,1);
        QCOMPARE(d.extensions[0].jsonPointer,std::string("/future"));
        QVERIFY(QByteArray::fromStdString(d.extensions[0].payload).contains("original"));
    }
    void unknownNestedFieldsAndEnvelopeExtras() {
        pandoeditor::Project project; project.replace(projectcodec::decode(sample()));
        auto root=QJsonDocument::fromJson(projectcodec::encode(project)).object();
        auto units=root["units"].toArray(); auto first=units[0].toObject();
        first["future/key~"]=QJsonArray{QJsonValue::Null,true}; units[0]=first; root["units"]=units;
        root["genericFeatures"]=QJsonArray{QJsonObject{{"id","retained"},{"unrendered",true}}};
        auto d=projectcodec::decode(QJsonDocument(root).toJson());
        QCOMPARE(d.extensions.size(),std::size_t(2));
        QCOMPARE(d.extensions[0].jsonPointer,std::string("/units/0/future~1key~0"));
        d.extensions[0].envelopeExtras=R"({"newEnvelopeNumber":123456789012345678901})";
        project.replace(d); const auto bytes=projectcodec::encode(project);
        QVERIFY(bytes.contains("123456789012345678901"));
        pandoeditor::Project reopened; reopened.replace(projectcodec::decode(bytes));
        QCOMPARE(projectcodec::encode(reopened),bytes);
    }
    void unknownEnvelopeDependenciesCannotDeclareThemselvesSafe() {
        auto d=projectcodec::decode(sample());
        const auto r=pandoeditor::territorialRef(d.units[0].id);
        pandoeditor::PreservedExtension e;
        e.id="opaque-envelope"; e.payload="null"; e.dependencyKnowledge="known";
        e.envelopeExtras=R"({"futureDependencies":["opaque-id"]})";
        for (const auto& status:{std::string("unsupported"),std::string("migrationArchive")}) {
            e.status=status; d.extensions={e};
            pandoeditor::Project project; project.replace(d);
            const auto reopened=projectcodec::decode(projectcodec::encode(project));
            QVERIFY(!pandoeditor::effectAllowed(reopened,r,"geometry"));
            QVERIFY(!pandoeditor::effectAllowed(reopened,r,"color"));
            QVERIFY(pandoeditor::effectAllowed(reopened,r,"name"));
            QCOMPARE(reopened.extensions[0].dependencyKnowledge,std::string("known"));
        }
    }
    void malformedAndExcessiveNestingRejected() {
        for (const auto& invalid: {QByteArray("{} trailing"),QByteArray("{\"a\":01}"),QByteArray("{\"a\":1.}"),QByteArray("{\"a\":true,}"),QByteArray("{\"a\":\"\\x\"}")})
            QVERIFY_EXCEPTION_THROWN(projectcodec::decode(invalid),std::invalid_argument);
        QByteArray deep(130,'['); deep+="null"; deep+=QByteArray(130,']');
        QVERIFY_EXCEPTION_THROWN(projectcodec::decode(deep),std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(projectcodec::decode(R"({"format":"pandoeditor-project","version":3.00000000000000000000001})"),std::invalid_argument);
    }
    void legacySavesV5() {
        pandoeditor::Project project;
        project.replace(projectcodec::decode(sample()));
        const auto saved=projectcodec::encode(project);
        QCOMPARE(QJsonDocument::fromJson(saved).object()["version"].toInt(),7);
        pandoeditor::Project reopened;
        reopened.replace(projectcodec::decode(saved));
        QCOMPARE(projectcodec::encode(reopened),saved);
    }
    void duplicateKeysRejected() {
        for (const auto& bytes:{QByteArray(R"({"version":1,"version":2})"),
                               QByteArray(R"({"format":"pandoeditor-project","version":1,"countries":[],"unknown":{"x":1,"\u0078":2}})"),
                               QByteArray(R"({"nested":[{"payload":{"array":[{"x":null,"x":false}]}}]})")}) {
            try { projectcodec::decode(bytes); QFAIL("duplicate key accepted"); }
            catch (const std::invalid_argument& error) { QVERIFY(QByteArray(error.what()).contains("DUPLICATE_KEY")); }
        }
    }
    void unknownScalarsSurvive() {
        auto bytes=sample();
        const auto brace=bytes.indexOf('{');
        bytes.insert(brace+1,R"("future":{"integer":9007199254740993123456789,"decimal":0.1234567890123456789012345,"array":[null,false,1e999]},)");
        pandoeditor::Project project;
        project.replace(projectcodec::decode(bytes));
        auto saved=projectcodec::encode(project);
        QVERIFY(saved.contains("9007199254740993123456789"));
        QVERIFY(saved.contains("0.1234567890123456789012345"));
        QVERIFY(saved.contains("1e999"));
        pandoeditor::Project reopened;
        reopened.replace(projectcodec::decode(saved));
        QCOMPARE(projectcodec::encode(reopened),saved);
    }
};
QTEST_GUILESS_MAIN(MigrationTests)
#include "migration_tests.moc"
