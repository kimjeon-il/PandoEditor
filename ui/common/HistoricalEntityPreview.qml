import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "UiTokens.js" as Tokens

ColumnLayout {
    id: root
    property var preview: ({})
    readonly property var colors:Tokens.colors(editor.appearancePreferences)
    function labelTerm(value){return ({exact:"정확",approximate:"근사",year:"연도",month:"월",day:"일",high:"높음",medium:"보통",low:"낮음",certain:"확실",uncertain:"불확실"})[value]||value||"기재 없음"}
    spacing: 8
    Label {textFormat:Text.PlainText; Layout.fillWidth: true; text: root.preview.name || "항목을 선택하세요"; font.bold: true; font.pixelSize: 17; wrapMode: Text.Wrap }
    Label {textFormat:Text.PlainText; Layout.fillWidth: true; text: root.preview.canonicalName || ""; color:root.colors.muted; visible: !!text }
    Rectangle {
        Layout.fillWidth: true; Layout.preferredHeight: 150
        color:root.colors.subtle;border.color:root.colors.border; radius: 6
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
        Label {textFormat:Text.PlainText; anchors.centerIn: parent; text: "표시할 경계 없음"; visible: !(root.preview.polygons || []).length }
    }
    Label {textFormat:Text.PlainText; Layout.fillWidth: true; text: "기간: " + (root.preview.validFrom || "열림") + " ~ " + (root.preview.validTo || "열림"); wrapMode: Text.Wrap }
    Label {textFormat:Text.PlainText; Layout.fillWidth: true; text: "경계 자료 · 정밀도: " + root.labelTerm(root.preview.datePrecision); wrapMode: Text.Wrap }
    Label {textFormat:Text.PlainText; Layout.fillWidth: true; text: "확실성: " + root.labelTerm(root.preview.certainty); wrapMode: Text.Wrap }
    Label {textFormat:Text.PlainText; Layout.fillWidth: true; visible: !!root.preview.partial; text: "일부 원본 자료가 누락되었습니다. 추가 시 명시적 승인이 필요합니다."; color: "#a24b18"; wrapMode: Text.Wrap }
    Label {textFormat:Text.PlainText; Layout.fillWidth: true; visible: !!root.preview.approximateGeometry; text: "근사 경계 자료"; color: "#a24b18" }
    Label {textFormat:Text.PlainText; Layout.fillWidth: true; visible: !!root.preview.sourceTitle; text: "자료: " + root.preview.sourceTitle + (root.preview.sourceLicense ? " · " + root.preview.sourceLicense : ""); wrapMode: Text.Wrap }
    Label {textFormat:Text.PlainText; Layout.fillWidth: true; text: root.preview.error || ""; color: "#a73535"; visible: !!text; wrapMode: Text.Wrap }
}
