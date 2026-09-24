import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: popup
    objectName: "mapDisplayPopup"
    width: Math.min(340, parent ? parent.width-16 : 340)
    height: Math.min(implicitHeight, parent ? parent.height-16 : 600)
    x: 8
    y: editor.mobileMode && parent ? Math.max(8,parent.height-height-8) : 8
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    contentItem: ScrollView {
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            Label { text: "지도 표시"; font.bold: true }
            Repeater {
                model: editor.presentationGroups.length
                delegate: GroupBox {
                    required property int index
                    readonly property var modelData: editor.presentationGroups[index]
                    Layout.fillWidth: true
                    title: modelData.title
                    ColumnLayout {
                        anchors.fill: parent
                        RowLayout {
                            CheckBox { text: "표시"; checked: modelData.visible; onClicked: editor.setPresentationVisibility(modelData.key,checked) }
                            CheckBox { visible: modelData.content !== true; text: "경계"; checked: modelData.boundary === true; onClicked: editor.setPresentationBoundary(modelData.key,checked) }
                        }
                        RowLayout {
                            visible: modelData.content !== true
                            CheckBox { text: "이름"; checked: modelData.names === true; onClicked: editor.setPresentationVisibility(modelData.nameKey,checked) }
                            CheckBox { text: "깃발"; checked: modelData.flags === true; onClicked: editor.setPresentationVisibility(modelData.flagKey,checked) }
                        }
                        Label { text: "불투명도 " + Math.round(modelData.opacity*100) + "%" }
                        Slider { Layout.fillWidth: true; from: 0; to: 1; value: modelData.opacity; onMoved: editor.setPresentationOpacity(modelData.key,value) }
                    }
                }
            }
            GroupBox {
                Layout.fillWidth: true
                title: "분포 표시"
                ColumnLayout {
                    anchors.fill: parent
                    RadioButton { text: "지배 분포"; checked: editor.distributionDisplay.mode === "dominant"; onClicked: editor.setDistributionDisplay("dominant",editor.distributionDisplay.boundaryVisible) }
                    RadioButton { text: "선택 레이어 강도"; checked: editor.distributionDisplay.mode === "intensity"; enabled: editor.distributionDisplay.intensityAvailable; onClicked: editor.setDistributionDisplay("intensity",editor.distributionDisplay.boundaryVisible) }
                    Label { visible: editor.distributionDisplay.mode === "intensity" && !editor.distributionDisplay.intensityAvailable; text: "분포 레이어를 먼저 선택하세요"; wrapMode: Text.Wrap }
                    CheckBox { text: "분포 경계"; checked: editor.distributionDisplay.boundaryVisible; onClicked: editor.setDistributionDisplay(editor.distributionDisplay.mode,checked) }
                }
            }
            RowLayout {
                visible: editor.primaryObject.domain === "label" || editor.primaryObject.domain === "territorial"
                Button { text: "라벨 고정"; onClicked: editor.setLabelPinned(editor.primaryObject,true) }
                Button { text: "자동 위치"; onClicked: editor.resetLabelPosition(editor.primaryObject) }
            }
            Button { text: "선택 객체 표시/숨김"; enabled: editor.selectionItems.length>0; onClicked: editor.toggleSelectionVisibility() }
        }
    }
}
