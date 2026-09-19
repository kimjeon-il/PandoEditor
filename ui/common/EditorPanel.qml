import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel
    property bool compact: false
    property bool holdFieldCommits: false
    function dismissPopup() {
        if (countryPicker.popup.visible) { countryPicker.popup.close(); return true }
        if (countryLayer.popup.visible) { countryLayer.popup.close(); return true }
        return false
    }
    color: "#ffffff"
    border.color: "#dce2e8"
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: "국가"; objectName: "countryTab" }
            TabButton { text: "레이어"; objectName: "layersTab" }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            Label { text: editor.hasPendingEdits ? "미적용 편집" : "편집 내용"; Layout.fillWidth: true; elide: Text.ElideRight }
            Button { objectName: "cancelEdits"; text: "취소"; enabled: editor.hasPendingEdits; onClicked: editor.discardPendingEdits() }
            Button { objectName: "applyEdits"; text: "적용"; enabled: editor.hasPendingEdits; onClicked: editor.applyPendingEditsAsync() }
        }
        ColumnLayout {
            objectName: "backgroundWorkPanel"
            visible: editor.jobBusy
            Layout.fillWidth: true
            Layout.leftMargin: 8; Layout.rightMargin: 8
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: editor.jobProgress < 0 ? "작업 대기 중…" : "계산 중…"
                    elide: Text.ElideRight
                }
                Button {
                    objectName: "cancelBackgroundWork"
                    text: "작업 취소"
                    Accessible.name: "계산 작업 취소"
                    onClicked: editor.cancelBackgroundWork()
                }
            }
            ProgressBar {
                objectName: "backgroundProgress"
                Layout.fillWidth: true
                indeterminate: editor.jobProgress <= 0
                from: 0; to: 100; value: Math.max(0, editor.jobProgress)
                Accessible.name: "계산 진행 상태"
            }
        }
        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true
            ScrollView {
                id: countryScroll
                objectName: "countryScroll"
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: countryScroll.availableWidth
                    spacing: 10
                    ComboBox {
                        id: countryPicker
                        objectName: "countryPicker"
                        Layout.fillWidth: true
                        Layout.margins: 12
                        model: editor.countryRows
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: indexOfValue(editor.selectedId)
                        displayText: editor.selectedName || "국가 선택"
                        onActivated: editor.selectCountry(currentValue)
                        delegate: ItemDelegate {
                            required property var modelData
                            width: countryPicker.width
                            text: modelData.name + (modelData.limited ? " · 제한" : "")
                        }
                    }
                    Label {
                        objectName: "preservedDataNotice"
                        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                        visible: editor.countryRows.some(function(row) { return row.id === editor.selectedId && row.limited })
                        text: "보존 데이터가 있어 일부 편집이 제한됩니다. 이름·메모 외 변경은 거절될 수 있습니다."
                        wrapMode: Text.WordWrap
                    }
                    Flow {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12; Layout.rightMargin: 12
                        Layout.preferredHeight: childrenRect.height
                        spacing: 8
                        Repeater {
                            model: ["#a8c7db","#e6b8a2","#b8d6b0","#d5c4e8","#f0d493","#e56b6f","#499c91","#667a99"]
                            delegate: Button {
                                required property string modelData
                                objectName: "swatch"+modelData.substring(1)
                                width: 40; height: 40
                                enabled: editor.selectedEditable
                                Accessible.name: "색상 "+modelData
                                onClicked: editor.setColor(modelData)
                                background: Rectangle {
                                    radius: 8
                                    color: parent.modelData
                                    opacity: parent.enabled ? 1 : 0.45
                                    border.width: editor.colors[editor.selectedId] === parent.modelData ? 3 : 1
                                    border.color: editor.colors[editor.selectedId] === parent.modelData ? "#172d42" : "#aab5be"
                                }
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                        visible: editor.selectedId !== "" && !editor.selectedEditable
                        text: "잠긴 레이어입니다. 레이어 탭에서 잠금을 해제하세요."
                        wrapMode: Text.WordWrap
                    }
                    Label { text: "이름"; Layout.leftMargin: 12 }
                    TextField {
                        objectName: "countryName"
                        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                        enabled: editor.selectedEditable
                        text: editor.nameDraft
                        Accessible.name: "국가 이름"
                        onTextEdited: editor.nameDraft=text
                        onEditingFinished: if (!panel.holdFieldCommits) editor.commitCountryField("name")
                    }
                    Label { text: "메모"; Layout.leftMargin: 12 }
                    TextArea {
                        objectName: "countryMemo"
                        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                        Layout.preferredHeight: 90
                        enabled: editor.selectedEditable
                        text: editor.memoDraft
                        wrapMode: TextEdit.Wrap
                        placeholderText: "국가에 대한 메모"
                        Accessible.name: "국가 메모"
                        property bool wasEditing: false
                        onTextChanged: if (activeFocus) editor.memoDraft=text
                        onActiveFocusChanged: {
                            if (activeFocus) wasEditing = true
                            else if (wasEditing) {
                                wasEditing = false
                                if (!panel.holdFieldCommits) editor.commitCountryField("notes")
                            }
                        }
                        background: Rectangle { color: "#f5f7f9"; border.color: "#c4cdd5"; radius: 4 }
                    }
                    Label { text: "RGB 색상"; Layout.leftMargin: 12 }
                    TextField {
                        objectName: "countryColor"
                        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                        enabled: editor.selectedEditable
                        text: editor.colorDraft
                        placeholderText: "#123456"
                        Accessible.name: "RGB 색상"
                        onTextEdited: editor.colorDraft=text
                    }
                    Label { text: "국가 불투명도 "+Math.round(editor.countryOpacity*100)+"%"; Layout.leftMargin: 12 }
                    Slider {
                        objectName: "countryOpacity"
                        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                        enabled: editor.selectedEditable
                        from: 0; to: 1; stepSize: 0.01
                        value: editor.countryOpacity
                        Accessible.name: "국가 불투명도"
                        onMoved: editor.previewCountryOpacity(value)
                    }
                    Label { text: "소속 레이어"; Layout.leftMargin: 12 }
                    ComboBox {
                        id: countryLayer
                        objectName: "countryLayer"
                        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12; Layout.bottomMargin: 12
                        enabled: editor.selectedEditable
                        model: editor.layers
                        textRole: "name"; valueRole: "id"
                        currentIndex: indexOfValue(editor.countryLayerId)
                        onActivated: editor.moveCountry(currentValue)
                        delegate: ItemDelegate {
                            required property var modelData
                            width: parent ? parent.width : 200
                            text: modelData.name + (modelData.locked ? " · 잠김" : "")
                            enabled: !modelData.locked
                        }
                    }
                }
            }
            ScrollView {
                id: layerScroll
                objectName: "layerScroll"
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: layerScroll.availableWidth
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true; Layout.margins: 12
                        Button { objectName: "addLayer"; text: "추가"; onClicked: editor.addLayer() }
                        Button { objectName: "removeLayer"; text: "삭제"; enabled: editor.canDeleteLayer; onClicked: editor.removeLayer() }
                        Item { Layout.fillWidth: true }
                    }
                    Repeater {
                        model: editor.layers
                        delegate: ItemDelegate {
                            required property var modelData
                            Layout.fillWidth: true
                            objectName: "layerRow_"+modelData.id
                            text: modelData.name+" · "+modelData.count+"개"+(!modelData.visible ? " · 숨김" : "")+(modelData.locked ? " · 잠김" : "")
                            highlighted: editor.selectedLayerId === modelData.id
                            onClicked: editor.selectLayer(modelData.id)
                        }
                    }
                    RowLayout {
                        Layout.leftMargin: 12; Layout.rightMargin: 12
                        Button { objectName: "layerUp"; text: "위로"; onClicked: editor.moveLayer(1) }
                        Button { objectName: "layerDown"; text: "아래로"; onClicked: editor.moveLayer(-1) }
                    }
                    TextField {
                        objectName: "layerName"
                        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                        text: editor.layerNameDraft
                        Accessible.name: "레이어 이름"
                        onTextEdited: editor.layerNameDraft=text
                    }
                    CheckBox {
                        objectName: "layerVisible"
                        text: "지도에 표시"
                        Layout.leftMargin: 12
                        checked: editor.layerVisuals[editor.selectedLayerId] ? editor.layerVisuals[editor.selectedLayerId].visible : false
                        onClicked: editor.setLayerVisible(checked)
                    }
                    CheckBox {
                        objectName: "layerLocked"
                        text: "레이어 잠금"
                        Layout.leftMargin: 12
                        checked: editor.layerVisuals[editor.selectedLayerId] ? editor.layerVisuals[editor.selectedLayerId].locked : false
                        onClicked: editor.setLayerLocked(checked)
                    }
                    Label { text: "레이어 불투명도 "+Math.round(editor.layerOpacity*100)+"%"; Layout.leftMargin: 12 }
                    Slider {
                        objectName: "layerOpacity"
                        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                        from: 0; to: 1; stepSize: 0.01
                        value: editor.layerOpacity
                        Accessible.name: "레이어 불투명도"
                        onMoved: editor.previewLayerOpacity(value)
                    }
                    Label {
                        Layout.fillWidth: true; Layout.margins: 12
                        text: "위쪽 레이어가 앞에 표시됩니다.\n빈 레이어만 삭제할 수 있습니다."
                        wrapMode: Text.WordWrap; color: "#526377"
                    }
                }
            }
        }
    }
}
