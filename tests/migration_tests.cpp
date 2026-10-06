#include "territorial_fixture.h"
#include "projectcodec.h"
#include "losslessjson.h"
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
    void historicalOriginV9RoundTrip() {
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
        origin.missingLibraryRefs={"current-country:missing"};
        document.units.front().libraryOrigin=origin;
        Project project;project.replace(document);
        const auto saved=projectcodec::encode(project);
        QCOMPARE(QJsonDocument::fromJson(saved).object()["version"].toInt(),10);
        const auto reopened=projectcodec::decode(saved);
        QVERIFY(reopened.units.front().libraryOrigin.has_value());
        const auto& actual=*reopened.units.front().libraryOrigin;
        QCOMPARE(actual.libraryId,origin.libraryId);
        QCOMPARE(actual.geometryVersionId,origin.geometryVersionId);
        QVERIFY(actual.referenceDate==origin.referenceDate);
        QCOMPARE(actual.certainty,origin.certainty);
        QCOMPARE(actual.datePrecision,origin.datePrecision);
        QVERIFY(actual.partial);
        QCOMPARE(actual.missingLibraryRefs,origin.missingLibraryRefs);
        QCOMPARE(projectcodec::encode(project),saved);
    }
    void labelAndDistributionSettingsPersistAndRoundTrip() {
        using namespace pandoeditor;auto d=projectcodec::decode(sample());Geometry point;point.type="Point";point.points={{1,2}};d.geometries.insert({"label-point",1},point);PlaceLabel label;label.id="city";label.name="City";label.kind="city";label.geometry={"label-point",1};d.labels.push_back(label);auto& settings=d.presentation.webPresentation.labelSettings[{"label","city"}];settings.pinned=true;settings.manualPosition=Point{10,20};d.presentation.webPresentation.distributionSettings.boundaryVisible=false;Project p;p.replace(d);const auto saved=projectcodec::encode(p);const auto reopened=projectcodec::decode(saved);QVERIFY(reopened.presentation.webPresentation.labelSettings.at({"label","city"}).pinned);QCOMPARE(reopened.presentation.webPresentation.labelSettings.at({"label","city"}).manualPosition->x,10.);QVERIFY(!reopened.presentation.webPresentation.distributionSettings.boundaryVisible);
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
    void opaqueContentRetentionAndAtomicRejection() {
        using namespace pandoeditor;auto d=projectcodec::decode(sample());PreservedExtension labels;labels.id="web-labels";labels.sourceFormat="pandolab-project";labels.sourceSchema=5;labels.jsonPointer="/labels";labels.payload=R"([{"id":"place","name":"City","coordinates":[10,20],"future":{"number":1e+09,"order":[3,1,2]}}])";d.extensions.push_back(labels);Project p;p.replace(d);const auto saved=projectcodec::encode(p);const auto reopened=projectcodec::decode(saved);QVERIFY(reopened.labels.empty());QCOMPARE(reopened.extensions.front().payload,losslessjson::parse(QByteArray::fromStdString(labels.payload)).encode().toStdString());QVERIFY(saved.contains("1e+09"));QVERIFY(saved.contains("[3,1,2]"));auto bad=QJsonDocument::fromJson(saved).object();bad["labels"]=QJsonArray{};QVERIFY_EXCEPTION_THROWN(projectcodec::decode(QJsonDocument(bad).toJson()),std::invalid_argument);QCOMPARE(projectcodec::encode(p),saved);
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
    void v9ContentRoundTripAndReferenceValidation() {
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
        HydroFeature lake; lake.id="lake"; lake.kind="lake"; lake.geometry=staticGeometryBinding(d,d.units.front().id).geometryRef; d.hydro.push_back(lake);
        DistributionLayer layer; layer.id="language"; layer.name="Language"; d.distributionLayers.push_back(layer);
        auto child=layer; child.id="child"; child.parentId="language"; d.distributionLayers.push_back(child);
        DistributionEntry entry; entry.id="entry-60"; entry.layerId="language"; entry.territory=country; entry.value=60;
        d.distributionEntries.push_back(entry); entry.id="entry-70"; entry.value=70; d.distributionEntries.push_back(entry);
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
        QVERIFY_EXCEPTION_THROWN(projectcodec::decode(futureBytes),std::invalid_argument);
        QVERIFY(p.renameCountry(country.id,"Changed")); QVERIFY(p.undo()); QVERIFY(sameContent(d,p.document()));
        QVERIFY(p.redo()); QVERIFY(sameContent(d,p.document()));
        auto invalid=d; invalid.distributionLayers.front().parentId="child";
        QVERIFY_EXCEPTION_THROWN(validateDocument(invalid),std::invalid_argument);
        invalid=d; invalid.labels.front().territory=territorialRef("missing");
        QVERIFY_EXCEPTION_THROWN(validateDocument(invalid),std::invalid_argument);
        invalid=d; invalid.distributionEntries.front().value=std::numeric_limits<double>::infinity();
        QVERIFY_EXCEPTION_THROWN(validateDocument(invalid),std::invalid_argument);
        invalid=d; invalid.hydro.front().kind="lake";
        QVERIFY_EXCEPTION_THROWN(validateDocument(invalid),std::invalid_argument);
        invalid=d; invalid.distributionEntries.front().geometry=staticGeometryBinding(d,d.units.front().id).geometryRef;
        QVERIFY_EXCEPTION_THROWN(validateDocument(invalid),std::invalid_argument);
    }
    void unknownPresentationTokensReject() {
        auto root=QJsonDocument::fromJson(sample()).object();auto presentation=root["presentation"].toObject();auto web=presentation["webPresentation"].toObject();auto styles=web["styles"].toObject();styles["future-group"]=QJsonObject{{"opacity",0.5}};web["styles"]=styles;presentation["webPresentation"]=web;root["presentation"]=presentation;QVERIFY_EXCEPTION_THROWN(projectcodec::decode(QJsonDocument(root).toJson()),std::invalid_argument);
    }
    void obsoleteObjectOrderKeysReject() {
        auto root=QJsonDocument::fromJson(sample()).object();auto presentation=root["presentation"].toObject();auto web=presentation["webPresentation"].toObject();web["objectOrder"]=QJsonArray{"territorial:country:DEU"};presentation["webPresentation"]=web;root["presentation"]=presentation;QVERIFY_EXCEPTION_THROWN(projectcodec::decode(QJsonDocument(root).toJson()),std::invalid_argument);
    }
    void v9PresentationPresenceRoundTrip() {
        using namespace pandoeditor;auto d=projectcodec::decode(sample());const auto geometry=staticGeometryBinding(d,d.units.front().id).geometryRef;appendTerritory(d,{"S","Child","",UnitKind::General},geometry,d.units.front().id);d.presentation.objectStyles[territorialRef("S")]={0,1,false};d.presentation.webPresentation.objectStyles[territorialPresentationKey("S")].opacity=1.;d.presentation.webPresentation.styles["countries"].opacity=0.7;d.presentation.webPresentation.overlayOrder={"genericFeatures","regions","subunits","distributions"};Project p;p.replace(d);const auto bytes=projectcodec::encode(p);const auto reopened=projectcodec::decode(bytes);QCOMPARE(*reopened.presentation.webPresentation.objectStyles.at(territorialPresentationKey("S")).opacity,1.);QCOMPARE(reopened.presentation.webPresentation.overlayOrder,d.presentation.webPresentation.overlayOrder);
    }
    void v9StaticDefaultsRemainExactAfterSaving() {
        using namespace pandoeditor;Project p;p.replace(projectcodec::decode(sample()));const auto saved=projectcodec::encode(p);const auto reopened=projectcodec::decode(saved);QCOMPARE(reopened.units.size(),std::size_t(5));QCOMPARE(reopened.timelineRecords.lifetimes.size(),std::size_t(5));for(const auto& u:reopened.units){QVERIFY(u.kind==UnitKind::General);QVERIFY(!staticLifetime(reopened,u.id).validity.from);QVERIFY(staticParentRelation(reopened,u.id).parentId.empty());QVERIFY(reopened.geometries.get(staticGeometryBinding(reopened,u.id).geometryRef));}QCOMPARE(QJsonDocument::fromJson(saved).object()["version"].toInt(),10);
    }
    void v9PreservesAllPropertiesAndCoordinates() {
        using namespace pandoeditor;auto d=projectcodec::decode(sample());d.units.front().name=" exact name ";d.units.front().notes="memo\nsecond line";d.units.front().nameExplicit=true;d.units.front().metadata=R"({"source":{"id":"original"},"ordered":[3,1,2]})";d.presentation.objectStyles[territorialRef(d.units.front().id)]={0x12abcd,0.625,true};d.presentation.userLayers.front().opacity=0.375;Project p;p.replace(d);const auto bytes=projectcodec::encode(p);const auto reopened=projectcodec::decode(bytes);QCOMPARE(reopened.units.front().name,d.units.front().name);QCOMPARE(reopened.units.front().notes,d.units.front().notes);QCOMPARE(reopened.units.front().metadata,losslessjson::parse(QByteArray::fromStdString(d.units.front().metadata)).encode().toStdString());QCOMPARE(reopened.geometries.get(staticGeometryBinding(reopened,reopened.units.front().id).geometryRef)->polygons.size(),d.geometries.get(staticGeometryBinding(d,d.units.front().id).geometryRef)->polygons.size());Project second;second.replace(reopened);QCOMPARE(projectcodec::encode(second),bytes);
    }
    void v9HierarchyRoundTripsCanonicalKindsAndRecords() {
        using namespace pandoeditor;auto d=projectcodec::decode(sample());const auto ref=staticGeometryBinding(d,d.units.front().id).geometryRef;appendTerritory(d,{"first","First","first notes",UnitKind::General,true},ref,d.units.front().id,"partition");appendTerritory(d,{"nested","Nested","nested notes",UnitKind::General},ref,"first");appendTerritory(d,{"region","Region","region notes",UnitKind::Regional},ref);for(const auto* id:{"first","nested","region"})d.presentation.objectStyles[territorialRef(id)]={0xabcdef,0.75,true};Project p;p.replace(d);const auto reopened=projectcodec::decode(projectcodec::encode(p));QCOMPARE(staticParentRelation(reopened,"nested").parentId,std::string("first"));QVERIFY(staticParentRelation(reopened,"region").parentId.empty());QCOMPARE(staticParentRelation(reopened,"first").coverageMode,std::string("partition"));QVERIFY(reopened.units.back().kind==UnitKind::Regional);auto json=QJsonDocument::fromJson(projectcodec::encode(p)).object();QVERIFY(!json.contains("relations"));for(const auto& value:json["units"].toArray()){const auto unit=value.toObject();QVERIFY(!unit.contains("geometryRef"));QVERIFY(!unit.contains("validity"));QVERIFY(!unit.contains("coverageMode"));}
    }
    void temporalWireAndAllGeometryKinds() {
        using namespace pandoeditor;auto d=projectcodec::decode(sample());const auto id=d.units.front().id;d.timelineRecords.lifetimes.front().validity={"-0001-12-31","1914-06"};d.timelineRecords.geometryBindings.front().validity=d.timelineRecords.lifetimes.front().validity;d.timelineRecords.parentRelations.front().validity=d.timelineRecords.lifetimes.front().validity;Project p;p.replace(d);const auto saved=projectcodec::encode(p);const auto reopened=projectcodec::decode(saved);QCOMPARE(reopened.timelineRecords.lifetimes.front().validity.from,d.timelineRecords.lifetimes.front().validity.from);QCOMPARE(reopened.timelineRecords.lifetimes.front().validity.to,d.timelineRecords.lifetimes.front().validity.to);QVERIFY_EXCEPTION_THROWN(requireStaticTimeline(reopened),TimelineError);auto bad=d;bad.timelineRecords.lifetimes.front().validity.from="0000";QVERIFY_EXCEPTION_THROWN(validateDocument(bad),std::invalid_argument);
    }
    void archiveCannotOverwriteEditedCanonicalFields() {
        using namespace pandoeditor;auto d=projectcodec::decode(sample());PreservedExtension e;e.id="future";e.jsonPointer="/future";e.payload=R"({"name":"original","id":"opaque"})";d.extensions={e};Project p;p.replace(d);const auto id=d.units.front().id;QVERIFY(p.renameCountry(id,"Edited canonical name"));QVERIFY(p.setMemo(id,"Edited canonical notes"));QVERIFY(!p.setColor(id,0x123456));const auto reopened=projectcodec::decode(projectcodec::encode(p));QCOMPARE(reopened.units.front().name,std::string("Edited canonical name"));QCOMPARE(reopened.units.front().notes,std::string("Edited canonical notes"));QCOMPARE(reopened.extensions.front().payload,losslessjson::parse(QByteArray::fromStdString(e.payload)).encode().toStdString());
    }
    void unknownNestedFieldsAndEnvelopeExtras() {
        auto root=QJsonDocument::fromJson(sample()).object();auto units=root["units"].toArray();auto first=units[0].toObject();first["future/key~"]=QJsonArray{QJsonValue::Null,true};units[0]=first;root["units"]=units;QVERIFY_EXCEPTION_THROWN(projectcodec::decode(QJsonDocument(root).toJson()),std::invalid_argument);using namespace pandoeditor;auto d=projectcodec::decode(sample());PreservedExtension e;e.id="opaque";e.payload="null";e.envelopeExtras=R"({"newEnvelopeNumber":123456789012345678901})";d.extensions={e};Project p;p.replace(d);const auto bytes=projectcodec::encode(p);QVERIFY(bytes.contains("123456789012345678901"));Project reopened;reopened.replace(projectcodec::decode(bytes));QCOMPARE(projectcodec::encode(reopened),bytes);
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
    void legacyVersionsRejectAndCurrentVersionSaves() {
        auto root=QJsonDocument::fromJson(sample()).object();for(int version=1;version<10;++version){root["version"]=version;QVERIFY_EXCEPTION_THROWN(projectcodec::decode(QJsonDocument(root).toJson()),std::invalid_argument);}root["version"]=10;pandoeditor::Project p;p.replace(projectcodec::decode(QJsonDocument(root).toJson()));QCOMPARE(QJsonDocument::fromJson(projectcodec::encode(p)).object()["version"].toInt(),10);
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
        auto d=projectcodec::decode(sample());d.units.front().metadata=R"({"integer":9007199254740993123456789,"decimal":0.1234567890123456789012345,"array":[null,false,1e999]})";pandoeditor::Project p;p.replace(d);const auto saved=projectcodec::encode(p);QVERIFY(saved.contains("9007199254740993123456789"));QVERIFY(saved.contains("0.1234567890123456789012345"));QVERIFY(saved.contains("1e999"));pandoeditor::Project reopened;reopened.replace(projectcodec::decode(saved));QCOMPARE(projectcodec::encode(reopened),saved);
    }

};
QTEST_GUILESS_MAIN(MigrationTests)
#include "migration_tests.moc"
