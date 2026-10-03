import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "UiTokens.js" as Tokens

Popup {
    id: popup
    objectName: "mapDisplayPopup"
    width: Math.min(340, parent ? parent.width-16 : 340)
    height: Math.min(implicitHeight, parent ? parent.height-16 : 600)
    x: 8
    y: editor.mobileMode && parent ? Math.max(8,parent.height-height-8) : 8
    modal: true
    background:Rectangle {color:Tokens.colors(editor.appearancePreferences).panel;border.color:Tokens.colors(editor.appearancePreferences).border;radius:9}
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    contentItem: ScrollView {
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            Label { text: "지도 표시"; font.bold: true }
            GroupBox {
                Layout.fillWidth: true
                title: "투영법"
                RowLayout {
                    anchors.fill: parent
                    Button { objectName:"projectionGlobeButton"; text:"지구본"; checkable:true; checked:editor.projectionMode==="globe"; onClicked:editor.setProjectionMode("globe") }
                    Button { objectName:"projectionFlatButton"; text:"평면지도"; checkable:true; checked:editor.projectionMode==="flat"; onClicked:editor.setProjectionMode("flat") }
                }
            }
            GroupBox {
                Layout.fillWidth: true
                title: "지형"
                ColumnLayout {
                    anchors.fill: parent
                    RadioButton { objectName:"terrainNoneButton"; text:"없음"; checked:editor.terrainMode==="none"; onClicked:editor.setTerrainMode("none") }
                    RadioButton { objectName:"terrainGrayButton"; text:"흑백"; checked:editor.terrainMode==="gray"; onClicked:editor.setTerrainMode("gray") }
                    RadioButton { objectName:"terrainColorButton"; text:"색채"; checked:editor.terrainMode==="color"; onClicked:editor.setTerrainMode("color") }
                }
            }
            GroupBox {
                Layout.fillWidth: true
                title: "수계"
                RowLayout {
                    anchors.fill: parent
                    CheckBox { text:"강"; checked:editor.hydroStyle.riversVisible; onClicked:editor.setPresentationVisibility("rivers",checked) }
                    CheckBox { text:"호수"; checked:editor.hydroStyle.lakesVisible; onClicked:editor.setPresentationVisibility("lakes",checked) }
                }
            }
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
                            CheckBox { visible: modelData.content !== true; text: "색상"; checked: modelData.colorVisible === true; onClicked: editor.setPresentationColorVisible(modelData.key,checked) }
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
                    RadioButton { text: "여러 분포 겹쳐 보기"; checked: editor.distributionDisplay.mode === "overlap"; onClicked: editor.setDistributionDisplay("overlap",editor.distributionDisplay.boundaryVisible) }
                    RadioButton { text: "분포 하나만 보기"; checked: editor.distributionDisplay.mode === "single"; onClicked: editor.setDistributionDisplay("single",editor.distributionDisplay.boundaryVisible) }
                    ComboBox { visible:editor.distributionDisplay.mode === "single";Layout.fillWidth:true;textRole:"name";valueRole:"id";model:editor.distributionDisplay.layers;currentIndex:Math.max(0,model.findIndex(function(row){return row.id===editor.distributionDisplay.activeLayerId}));onActivated:editor.setDistributionDisplay("single",editor.distributionDisplay.boundaryVisible,currentValue) }
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
