import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../common" as Common

Item {
    id: workspace
    property bool compact: width < 800
    property bool mobileMode: false
    property bool holdFieldCommits: false
    signal webImportRequested()
    signal openRequested()
    signal saveRequested()
    signal saveAsRequested()
    function dismissPopup() { return panel.dismissPopup() }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ToolBar {
            objectName: "storageToolbar"
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                spacing: workspace.mobileMode ? 0 : 6
                ToolButton {
                    objectName: "importButton"
                    Layout.fillWidth: workspace.mobileMode
                    Layout.minimumWidth: workspace.mobileMode ? 0 : implicitWidth
                    text: workspace.mobileMode ? "가져오기" : "열기"
                    font.pixelSize: workspace.mobileMode ? 11 : 13
                    onClicked: workspace.openRequested()
                }
                ToolButton {
                    objectName: "deviceSaveButton"
                    Layout.fillWidth: workspace.mobileMode
                    Layout.minimumWidth: workspace.mobileMode ? 0 : implicitWidth
                    text: workspace.mobileMode ? "기기에 저장" : "저장"
                    font.pixelSize: workspace.mobileMode ? 11 : 13
                    onClicked: workspace.saveRequested()
                }
                ToolButton {
                    objectName: workspace.mobileMode ? "exportButton" : "saveAsButton"
                    Layout.fillWidth: workspace.mobileMode
                    Layout.minimumWidth: workspace.mobileMode ? 0 : implicitWidth
                    text: workspace.mobileMode ? "내보내기" : "다른 이름"
                    font.pixelSize: workspace.mobileMode ? 11 : 13
                    onClicked: workspace.saveAsRequested()
                }
                Item { visible: !workspace.mobileMode; Layout.fillWidth: visible }
                ToolButton {
                    objectName: "undoButton"
                    Layout.fillWidth: workspace.mobileMode
                    Layout.minimumWidth: workspace.mobileMode ? 0 : implicitWidth
                    text: "취소"
                    font.pixelSize: workspace.mobileMode ? 11 : 13
                    Accessible.name: "실행 취소"; enabled: editor.canUndo; onClicked: editor.undo()
                }
                ToolButton {
                    objectName: "redoButton"
                    Layout.fillWidth: workspace.mobileMode
                    Layout.minimumWidth: workspace.mobileMode ? 0 : implicitWidth
                    text: "다시"
                    font.pixelSize: workspace.mobileMode ? 11 : 13
                    Accessible.name: "다시 실행"; enabled: editor.canRedo; onClicked: editor.redo()
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                padding: 8
                text: editor.fileName + (editor.dirty ? " · 저장하지 않은 변경" : " · 저장됨")
                elide: Text.ElideMiddle
                color: "#526377"
            }
            ToolButton {
                objectName: "webImportButton"
                text: "웹 프로젝트 가져오기"
                font.pixelSize: workspace.compact ? 11 : 13
                focusPolicy: Qt.NoFocus
                onClicked: workspace.webImportRequested()
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Common.MapView {
                objectName: "mapView"
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.right: workspace.compact ? parent.right : panel.left
                anchors.bottom: workspace.compact ? panel.top : parent.bottom
            }
            Common.EditorPanel {
                id: panel
                objectName: "editorPanel"
                compact: workspace.compact
                holdFieldCommits: workspace.holdFieldCommits
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                width: workspace.compact ? parent.width : 320
                height: workspace.compact ? Math.min(320,parent.height*0.52) : parent.height
            }
        }
    }
}
