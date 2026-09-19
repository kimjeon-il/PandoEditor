import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: reportDialog
    objectName: "webImportDialog"
    property string extraError: ""
    readonly property string reviewedHash: editor.webImportHash
    signal cancelRequested()
    signal confirmRequested(string disposition)
    signal saveLocationRequested()
    width: Math.min(760, parent.width-24)
    height: Math.min(620, parent.height-32)
    anchors.centerIn: parent
    title: "웹 프로젝트 가져오기"
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    onRejected: cancelRequested()
    function statusText(value) {
        switch(value) {
        case "mapped": return "매핑"
        case "retained": return "보존 · 제한"
        case "archived": return "원본 보관"
        case "unavailable-reference": return "외부 자료 없음"
        case "default": return "기본값"
        case "migrated": return "형식 변환"
        default: return "안내"
        }
    }
    contentItem: ColumnLayout {
        spacing: 10
        Label {
            objectName: "webImportSummary"
            Layout.fillWidth: true
            text: editor.webImportSummary
            wrapMode: Text.WrapAnywhere
            font.bold: true
        }
        Label {
            Layout.fillWidth: true
            text: "웹 원본은 변경하지 않습니다. 보존된 데이터와 화면에 표시·편집할 수 있는 데이터는 다릅니다."
            wrapMode: Text.WordWrap
        }
        ProgressBar {
            Layout.fillWidth: true
            visible: editor.webImportBusy
            indeterminate: true
            Accessible.name: "웹 프로젝트 읽기·변환·검증"
        }
        Label {
            Layout.fillWidth: true
            visible: text !== ""
            text: editor.webImportError || reportDialog.extraError
            wrapMode: Text.WrapAnywhere
            color: "#97342d"
        }
        ListView {
            id: reportList
            objectName: "webImportReportList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: editor.webImportReport
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                required property var modelData
                width: reportList.width
                implicitHeight: row.implicitHeight+16
                color: "#f3f5f7"
                radius: 4
                Column {
                    id: row
                    x: 8; y: 8
                    width: parent.width-24
                    spacing: 4
                    Label {
                        width: parent.width
                        text: reportDialog.statusText(modelData.status) + "  " + (modelData.path || "전체 문서")
                        font.bold: true
                        wrapMode: Text.WrapAnywhere
                    }
                    Label { width: parent.width; text: modelData.message; wrapMode: Text.WordWrap }
                }
            }
        }
    }
    footer: ColumnLayout {
        spacing: 4
        Label {
            Layout.fillWidth: true
            Layout.margins: 8
            visible: editor.dirty
            text: "현재 작업에 저장하지 않은 변경이 있습니다."
            wrapMode: Text.WordWrap
        }
        Button {
            objectName: "saveAndWebImport"
            Layout.fillWidth: true
            Layout.leftMargin: 8; Layout.rightMargin: 8
            visible: editor.dirty
            enabled: editor.hasWebImportPreview && !editor.webImportBusy
            text: "기존 작업 저장 후 가져오기"
            onClicked: reportDialog.confirmRequested("save")
        }
        Button {
            objectName: "chooseWebImportSaveLocation"
            Layout.fillWidth: true
            Layout.leftMargin: 8; Layout.rightMargin: 8
            visible: editor.hasWebImportPreview && (editor.webImportError !== "" || reportDialog.extraError !== "")
            text: "기존 작업 저장 위치 선택…"
            onClicked: reportDialog.saveLocationRequested()
        }
        Button {
            objectName: "discardAndWebImport"
            Layout.fillWidth: true
            Layout.leftMargin: 8; Layout.rightMargin: 8
            enabled: editor.hasWebImportPreview && !editor.webImportBusy
            text: editor.dirty ? "기존 작업 버리고 가져오기" : "가져오기 확정"
            onClicked: reportDialog.confirmRequested("discard")
        }
        Button {
            objectName: "cancelWebImport"
            Layout.fillWidth: true
            Layout.leftMargin: 8; Layout.rightMargin: 8; Layout.bottomMargin: 8
            text: editor.webImportBusy ? "작업 취소" : "취소"
            onClicked: reportDialog.cancelRequested()
        }
    }
}
