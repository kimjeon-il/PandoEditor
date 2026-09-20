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
                model: 3
                delegate: GroupBox {
                    required property int index
                    readonly property var modelData: editor.presentationGroups[index]
                    Layout.fillWidth: true
                    title: modelData.title
                    ColumnLayout {
                        anchors.fill: parent
                        RowLayout {
                            CheckBox { text: "표시"; checked: modelData.visible; onClicked: editor.setPresentationVisibility(modelData.key,checked) }
                            CheckBox { text: "경계"; checked: modelData.boundary; onClicked: editor.setPresentationBoundary(modelData.key,checked) }
                        }
                        RowLayout {
                            CheckBox { text: "이름"; checked: modelData.names; onClicked: editor.setPresentationVisibility(modelData.nameKey,checked) }
                            CheckBox { text: "깃발"; checked: modelData.flags; onClicked: editor.setPresentationVisibility(modelData.flagKey,checked) }
                        }
                        Label { text: "불투명도 " + Math.round(modelData.opacity*100) + "%" }
                        Slider { Layout.fillWidth: true; from: 0; to: 1; value: modelData.opacity; onMoved: editor.setPresentationOpacity(modelData.key,value) }
                    }
                }
            }
            Button { text: "선택 객체 표시/숨김"; enabled: editor.selectionItems.length>0; onClicked: editor.toggleSelectionVisibility() }
        }
    }
}
