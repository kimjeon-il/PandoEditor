#pragma once
#include <QJsonObject>
// Retarget only the current rendering of the same canonical project and input.
// Preparation counters can advance when immutable hydro/LOD resources arrive.
// The caller still requires the exact candidate frame at sync and swap.
inline bool nativePerfSameInputState(QJsonObject expected,QJsonObject candidate,bool navigationHover=false)
{
    if(expected.value("documentSha256").toString().size()!=64||
       expected.value("projectInstanceId").toString().isEmpty()||candidate.isEmpty())return false;
    for(const auto* key:{"sceneRevision","geometryRevision","datasetRevision"}) {
        expected.remove(key);candidate.remove(key);
    }
    if(navigationHover) {
        expected.remove("hoveredId");candidate.remove("hoveredId");
        expected.remove("selectionRevision");candidate.remove("selectionRevision");
    }
    return expected==candidate;
}
