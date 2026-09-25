import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var preview: ({})
    spacing: 6
    Label { Layout.fillWidth: true; text: root.preview.name || "항목을 선택하세요"; font.bold: true; font.pixelSize: 17; wrapMode: Text.Wrap }
    Label { Layout.fillWidth: true; text: root.preview.canonicalName || ""; color: "#536477"; visible: !!text }
    Rectangle {
        Layout.fillWidth: true; Layout.preferredHeight: 150
        color: "#e8f0f5"; border.color: "#b6c8d5"; radius: 6
        Canvas {
            anchors.fill: parent; anchors.margins: 10
            property var shape: root.preview.polygons || []
            onShapeChanged: requestPaint()
            onPaint: {
                const ctx=getContext("2d"); ctx.clearRect(0,0,width,height)
                ctx.fillStyle="#528aaa"; ctx.strokeStyle="#215573"; ctx.lineWidth=1
                for (let polygon of shape) {
                    ctx.beginPath()
                    for (let ring of polygon) {
                        if (!ring.length) continue
                        ctx.moveTo(ring[0].x*width,ring[0].y*height)
                        for (let i=1;i<ring.length;i++) ctx.lineTo(ring[i].x*width,ring[i].y*height)
                        ctx.closePath()
                    }
                    ctx.fill("evenodd"); ctx.stroke()
                }
            }
        }
        Label { anchors.centerIn: parent; text: "표시할 경계 없음"; visible: !(root.preview.polygons || []).length }
    }
    Label { Layout.fillWidth: true; text: "기간: " + (root.preview.validFrom || "열림") + " ~ " + (root.preview.validTo || "열림"); wrapMode: Text.Wrap }
    Label { Layout.fillWidth: true; text: "경계: " + (root.preview.geometryVersionId || "-") + " · 정밀도: " + (root.preview.datePrecision || "-"); wrapMode: Text.Wrap }
    Label { Layout.fillWidth: true; text: "확실성: " + (root.preview.certainty || "-") + " · 출처: " + (root.preview.sourceId || "-"); wrapMode: Text.Wrap }
    Label { Layout.fillWidth: true; visible: !!root.preview.partial; text: "일부 원본 자료가 누락되었습니다. 추가 시 명시적 승인이 필요합니다."; color: "#a24b18"; wrapMode: Text.Wrap }
    Label { Layout.fillWidth: true; visible: !!root.preview.approximateGeometry; text: "근사 경계 자료"; color: "#a24b18" }
    Label { Layout.fillWidth: true; visible: !!root.preview.sourceTitle; text: "자료: " + root.preview.sourceTitle + (root.preview.sourceLicense ? " · " + root.preview.sourceLicense : ""); wrapMode: Text.Wrap }
    Label { Layout.fillWidth: true; text: root.preview.error || ""; color: "#a73535"; visible: !!text; wrapMode: Text.Wrap }
}
