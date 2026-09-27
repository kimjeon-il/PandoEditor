#include "world_diagnostic_helper.h"
#include <QtTest>
#include <algorithm>

namespace {
const QString root=QStringLiteral(M71_WORLD_FIXTURE);
const m71fixture::ProjectionDiagnostic* find(const std::vector<m71fixture::ProjectionDiagnostic>& rows,const std::string& id) {
    const auto it=std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.id==id;});
    return it==rows.end()?nullptr:&*it;
}
bool hasClass(const m71fixture::ProjectionDiagnostic& row,const std::string& name) {
    return std::find(row.classes.begin(),row.classes.end(),name)!=row.classes.end();
}
std::vector<double> sourceCoordinates(const pandoeditor::ProjectDocument& d) {
    std::vector<double> values;
    for(const auto& [ref,g]:d.geometries.versions()) {
        values.push_back(double(ref.version));
        auto append=[&](const pandoeditor::Point& p){values.push_back(p.x);values.push_back(p.y);};
        for(const auto& p:g->points) append(p);
        for(const auto& line:g->lines) for(const auto& p:line) append(p);
        for(const auto& poly:g->polygons) for(const auto& ring:poly) for(const auto& p:ring) append(p);
    }
    return values;
}
}

class WorldProjectionDiagnosticsTests : public QObject {
    Q_OBJECT
private slots:
    void currentProjectionProducesFiniteOutputForCorpus() {
        const auto d=m71fixture::loadWorldCorpusProject(root);
        MapProjection projection;projection.rebuild(d);
        QVERIFY(m71fixture::projectionPathsFinite(projection));
        const auto rows=m71fixture::diagnoseCurrentProjection(d);
        QVERIFY(rows.size()>=d.units.size()+2);
        for(const auto& row:rows) {
            QVERIFY2(!row.hasNonFinite,row.id.c_str());
            QVERIFY(std::isfinite(row.sourceLongitudeSpan));
            QVERIFY(std::isfinite(row.projectedWidth));
            QVERIFY(std::isfinite(row.maxProjectedSegmentJump));
        }
    }
    void diagnosticsIdentifyDatelineRiskCases() {
        const auto rows=m71fixture::diagnoseCurrentProjection(m71fixture::loadWorldCorpusProject(root));
        for(const auto* id:{"RUS","FJI","KIR","DATELINE"}) {
            const auto* row=find(rows,id);QVERIFY(row);QVERIFY(hasClass(*row,"dateline-risk"));
            QVERIFY(row->sourceLongitudeSpan>300 || row->maxProjectedSegmentJump>180);
        }
        const auto* control=find(rows,"DEU");QVERIFY(control);QVERIFY(hasClass(*control,"control"));
        QVERIFY(!hasClass(*control,"dateline-risk"));
    }
    void diagnosticsIdentifyPolarRiskCase() {
        const auto rows=m71fixture::diagnoseCurrentProjection(m71fixture::loadWorldCorpusProject(root));
        for(const auto* id:{"ATA","POLAR"}) {
            const auto* row=find(rows,id);QVERIFY(row);QVERIFY(hasClass(*row,"polar-risk"));
        }
        const auto* zaf=find(rows,"ZAF");QVERIFY(zaf);QVERIFY(hasClass(*zaf,"hole-risk"));
        const auto* chl=find(rows,"CHL");QVERIFY(chl);QVERIFY(hasClass(*chl,"long-narrow-risk"));
    }
    void currentProjectionDoesNotMutateDocumentGeometry() {
        const auto d=m71fixture::loadWorldCorpusProject(root);
        const auto before=sourceCoordinates(d);
        const auto rows=m71fixture::diagnoseCurrentProjection(d);
        QVERIFY(!rows.empty());
        QCOMPARE(sourceCoordinates(d),before);
    }
};
QTEST_MAIN(WorldProjectionDiagnosticsTests)
#include "world_projection_diagnostics.moc"
