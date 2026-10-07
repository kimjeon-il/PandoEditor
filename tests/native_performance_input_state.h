#pragma once
#include <QJsonObject>
// Scene sequence is bookkeeping. The input state and content owners remain exact.
inline bool nativePerfSameInputState(QJsonObject expected,QJsonObject candidate)
{
    if(expected.isEmpty()||candidate.isEmpty())return false;
    expected.remove("sceneRevision");candidate.remove("sceneRevision");
    return expected==candidate;
}
