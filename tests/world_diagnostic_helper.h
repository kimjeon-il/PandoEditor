#pragma once
#include "world_fixture_loader.h"
#include "mapprojection.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace m71fixture {
struct ProjectionDiagnostic {
    std::string id;
    double sourceLongitudeSpan=0,projectedWidth=0,maxProjectedSegmentJump=0;
    std::size_t projectedPathCharacters=0;
    bool hasNonFinite=false;
    std::vector<std::string> classes;
};

inline bool projectionPathsFinite(const MapProjection& projection) {
    if(!std::isfinite(projection.width)||!std::isfinite(projection.height)||
       projection.width<=0||projection.height<=0) return false;
    for(const auto& value:projection.paths) {
        const auto path=value.toMap();
        for(const auto* key:{"left","top","width","height"})
            if(!std::isfinite(path.value(QString::fromLatin1(key)).toDouble())) return false;
        const auto text=path.value(QStringLiteral("path")).toString();
        if(text.contains(QStringLiteral("nan"),Qt::CaseInsensitive)||
           text.contains(QStringLiteral("inf"),Qt::CaseInsensitive)) return false;
        for(const auto& point:path.value(QStringLiteral("points")).toList()) {
            const auto coords=point.toMap();
            if(!std::isfinite(coords.value(QStringLiteral("x")).toDouble())||
               !std::isfinite(coords.value(QStringLiteral("y")).toDouble())) return false;
        }
    }
    return true;
}

inline std::vector<ProjectionDiagnostic> diagnoseCurrentProjection(const pandoeditor::ProjectDocument& document) {
    MapProjection projection;projection.rebuild(document);
    std::vector<ProjectionDiagnostic> result;
    auto diagnose=[&](const std::string& id,const pandoeditor::Geometry& geometry,const QString& pathId) {
        const auto stats=geometryStats(geometry);
        ProjectionDiagnostic row;row.id=id;row.sourceLongitudeSpan=stats.east-stats.west;
        auto segment=[&](const pandoeditor::Ring& points) {
            bool previous=false;pandoeditor::Point last{};
            for(const auto& source:points) {
                const auto projected=projection.project(source);
                if(!std::isfinite(projected.x)||!std::isfinite(projected.y)) row.hasNonFinite=true;
                if(previous) row.maxProjectedSegmentJump=std::max(row.maxProjectedSegmentJump,
                    std::hypot(projected.x-last.x,projected.y-last.y));
                last=projected;previous=true;
            }
        };
        for(const auto& polygon:geometry.polygons) for(const auto& ring:polygon) segment(ring);
        for(const auto& line:geometry.lines) segment(line);
        for(const auto& point:geometry.points) segment({point});
        bool pathFound=false;
        for(const auto& value:projection.paths) {
            const auto path=value.toMap();if(path.value(QStringLiteral("countryId")).toString()!=pathId)continue;
            pathFound=true;row.projectedWidth=path.value(QStringLiteral("width")).toDouble();
            const auto text=path.value(QStringLiteral("path")).toString();
            row.projectedPathCharacters=std::size_t(text.size());
            row.hasNonFinite|=!std::isfinite(row.projectedWidth) ||
                !std::isfinite(path.value(QStringLiteral("left")).toDouble()) ||
                !std::isfinite(path.value(QStringLiteral("top")).toDouble()) ||
                text.contains(QStringLiteral("nan"),Qt::CaseInsensitive) ||
                text.contains(QStringLiteral("inf"),Qt::CaseInsensitive);
            break;
        }
        if(!pathFound) row.hasNonFinite=true;
        if(id=="DEU") row.classes.push_back("control");
        if(row.sourceLongitudeSpan>300 || stats.maxLongitudeJump>180) row.classes.push_back("dateline-risk");
        if(stats.north>=89.8 || stats.south<=-89.8) row.classes.push_back("polar-risk");
        if(stats.polygonCount>=100) row.classes.push_back("large-multipart-risk");
        if(stats.holeCount>0) row.classes.push_back("hole-risk");
        if(id=="CHL") row.classes.push_back("long-narrow-risk");
        result.push_back(std::move(row));
    };
    for(const auto& unit:document.units) {
        const auto shape=document.geometries.get(unit.geometry);
        if(shape) diagnose(unit.id,*shape,QString::fromStdString(unit.id));
    }
    for(const auto& item:document.genericFeatures) if(item.id=="DATELINE"||item.id=="POLAR") {
        const auto shape=document.geometries.get(item.geometry);
        if(shape) diagnose(item.id,*shape,QStringLiteral("content/generic/")+QString::fromStdString(item.id));
    }
    return result;
}
}
