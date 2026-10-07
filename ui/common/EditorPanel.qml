import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import "UiTokens.js" as Tokens

Rectangle {
    id: panel
    property bool compact: false
    property string mode: "all"
    property bool ownsStructureDialog:true
    function showCountryControls(){tabs.currentIndex=0}
    function showLayers(){tabs.currentIndex=1}
    property bool holdFieldCommits: false
    property bool selectionNavigation: false
    readonly property string selectedColor: editor.objectProperties.color || ""
    readonly property bool fieldCommitsHeld: holdFieldCommits || selectionNavigation || editor.objectChooserOpen
    function beginSelectionNavigation() {
        selectionNavigation = true
        Qt.callLater(function() { panel.selectionNavigation = false })
    }
    function dismissPopup() {
        if (countryPicker.popup.visible) { countryPicker.popup.close(); return true }
        if (countryLayer.popup.visible) { countryLayer.popup.close(); return true }
        return false
    }
    color:Tokens.colors(editor.appearancePreferences).panel
    border.color:Tokens.colors(editor.appearancePreferences).border
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        TabBar {
            id: tabs
            visible: panel.mode !== "country"
            Layout.fillWidth: true
            UiTabButton {text:"영토 속성";visible:panel.mode==="all";objectName:panel.mode==="all"?"countryTab":""}
            UiTabButton {text:"레이어";objectName:"layersTab"}

        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            Label { text: editor.selectionItems.length>1 ? editor.selectionItems.length+"개 선택" : editor.hasPendingEdits ? "미적용 편집" : "편집 내용"; Layout.fillWidth: true; elide: Text.ElideRight }
            UiButton {outlined:true; objectName: "cancelEdits"; text: "취소"; enabled: editor.hasPendingEdits; onClicked: editor.discardPendingEdits() }
            UiButton {outlined:true; objectName: "applyEdits"; text: "적용"; enabled: editor.hasPendingEdits; onClicked: editor.applyPendingEditsAsync() }
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
                UiButton {outlined:true;
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
            currentIndex: panel.mode==="country"?0:panel.mode==="layers"?1:tabs.currentIndex
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
                    UiComboBox {
                        id: countryPicker
                        objectName: "countryPicker"
                        Layout.fillWidth: true
                        Layout.margins: 12
                        model: editor.countryRows
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: indexOfValue(editor.selectedId)
                        displayText: editor.selectedName || "국가 선택"
                        focusPolicy: Qt.NoFocus
                        onPressedChanged: if (pressed) panel.beginSelectionNavigation()
                        onActivated: editor.selectCountry(currentValue)
                        delegate: ItemDelegate {
                            required property var modelData
                            width: countryPicker.width
                            text: modelData.name + (modelData.limited ? " · 제한" : "")
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12; Layout.rightMargin: 12
                        UiButton {outlined:true;
                            objectName: "legacyFocusSelection"; text: "선택 객체로 이동"
                            focusPolicy: Qt.NoFocus
                            enabled: editor.selectionItems.length === 1
                            onClicked: { panel.beginSelectionNavigation(); editor.focusObject() }
                        }
                        UiButton {outlined:true;
                            objectName: "clearObjectSelection"; text: "선택 해제"
                            focusPolicy: Qt.NoFocus
                            enabled: editor.selectionItems.length > 0
                            onClicked: { panel.beginSelectionNavigation(); editor.clearSelection() }
                        }
                    }
                    Label {
                        objectName: "legacyPreservedDataNotice"
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
                            delegate: UiButton {outlined:true;
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
                                    border.width: panel.selectedColor === parent.modelData ? 3 : 1
                                    border.color: panel.selectedColor === parent.modelData ? "#172d42" : "#aab5be"
                                }
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                        visible: editor.selectedId !== "" && !editor.selectedEditable
                        text: editor.selectionItems.length>1 ? "여러 객체가 선택되었습니다."
                            : editor.primaryObject.domain === "placeBuiltin" ? "내장 지명은 읽기 전용입니다. 복사본을 만들어 편집할 수 있습니다."
                            : editor.primaryObject.type !== "general" ? "이 객체의 속성 편집은 후속 단계에서 지원합니다."
                            : "객체 또는 소속 레이어가 잠겨 있습니다."
                        wrapMode: Text.WordWrap
                    }
                    UiButton {
                        objectName: "copyBuiltinPlaceForEditing"
                        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                        visible: editor.primaryObject.domain === "placeBuiltin"
                        text: "복사하여 편집"
                        onClicked: editor.copySelectedPlaceForEditing()
                    }
                    GroupBox {
                        objectName: "territorialStructurePanel"
                        title: "영토 구조"
                        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                        visible: editor.primaryObject.domain === "territorial"
                        ColumnLayout {
                            anchors.fill: parent
                            UiComboBox {
                                id: parentChoice
                                Layout.fillWidth: true
                                visible: editor.primaryObject.type === "general"
                                model: editor.relationParentOptions
                                textRole: "name"; valueRole: "id"
                                displayText: editor.objectProperties.parentId || "상위 영역 선택"
                            }
                            UiButton {outlined:true;
                                objectName: "changeTerritorialParent"
                                visible: editor.primaryObject.type === "general"
                                text: "상위 영역 변경"
                                enabled: parentChoice.currentValue !== ""
                                onClicked: editor.changeSelectedParent(parentChoice.currentValue)
                            }
                            UiComboBox {
                                id: transferCountry
                                objectName: "transferCountry"
                                Layout.fillWidth: true
                                visible: editor.primaryObject.type === "general"
                                model: editor.relationCountryOptions; textRole: "name"; valueRole: "id"
                            }
                            UiButton {outlined:true;
                                objectName: "transferTerritorial"
                                visible: editor.primaryObject.type === "general"
                                text: "선택 국가로 영토 이전"
                                enabled: transferCountry.currentValue !== ""
                                onClicked: editor.transferSelectedSubunit(transferCountry.currentValue)
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                UiButton {outlined:true; objectName: "deleteTerritorial"; text: "삭제"; enabled: editor.selectionItems.length > 0; onClicked: editor.beginDeleteSelection() }
                                UiButton {outlined:true; objectName: "convertTerritorial"; text: "상위 관계 전환"; visible: editor.primaryObject.type === "general"; onClicked: editor.beginTypeConversion() }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                UiButton {outlined:true; objectName: "mergeTerritorial"; text: "합병"; enabled: editor.selectionItems.length === 1; onClicked: editor.beginMergeSelection() }
                                UiButton {outlined:true; objectName: "annexTerritorial"; text: "영역 편입"; enabled: editor.selectionItems.length === 1; onClicked: editor.beginAnnexGeometry() }
                                UiButton {outlined:true; objectName: "splitTerritorial"; text: "절단선 분할"; enabled: editor.selectionItems.length === 1; onClicked: editor.beginSplitGeometry() }
                                UiButton {outlined:true; objectName: "sharedBoundaryTerritorial"; text: "공유 국경"; enabled: editor.selectionItems.length >= 1 && editor.canBeginSharedBoundaryGeometry(); onClicked: editor.beginSharedBoundaryGeometry() }
                                UiButton {outlined:true; objectName: "coastTerritorial"; text: "해안(국가)"; enabled: editor.selectionItems.length === 1; onClicked: editor.beginCoastlineGeometry("country") }
                                UiButton {outlined:true; objectName: "coastSubunitTerritorial"; text: "해안(하위)"; enabled: editor.selectionItems.length === 1; onClicked: editor.beginCoastlineGeometry("subunit") }
                                UiButton {outlined:true; objectName: "coastIndependentTerritorial"; text: "해안(독립)"; enabled: editor.selectionItems.length === 1; onClicked: editor.beginCoastlineGeometry("independent") }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                UiButton {outlined:true; objectName: "createGeneral"; text: "일반객체 추가"; onClicked: editor.beginTerritorialCreate("general") }
                                UiButton {outlined:true; objectName: "createRegional"; text: "독립 권역 추가"; onClicked: editor.beginTerritorialCreate("regional") }
                            }
                        }
                    }
                    Label { text: "이름"; Layout.leftMargin: 12 }
                    UiTextField {
                        objectName: "legacyCountryName"
                        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                        enabled: editor.selectedEditable
                        text: editor.nameDraft
                        Accessible.name: "국가 이름"
                        onTextEdited: editor.nameDraft=text
                        onEditingFinished: if (!panel.fieldCommitsHeld) editor.commitCountryField("name")
                    }
                    Label { text: "메모"; Layout.leftMargin: 12 }
                    UiTextArea {
                        objectName: "legacyCountryMemo"
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
                                if (!panel.fieldCommitsHeld) editor.commitCountryField("notes")
                            }
                        }
                        background: Rectangle { color: "#f5f7f9"; border.color: "#c4cdd5"; radius: 4 }
                    }
                    Label { text: "RGB 색상"; Layout.leftMargin: 12 }
                    UiTextField {
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
                    UiComboBox {
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
                        UiButton {outlined:true; objectName: "addLayer"; text: "추가"; onClicked: editor.addLayer() }
                        UiButton {outlined:true; objectName: "removeLayer"; text: "삭제"; enabled: editor.canDeleteLayer; onClicked: editor.removeLayer() }
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
                        UiButton {outlined:true; objectName: "layerUp"; text: "위로"; onClicked: editor.moveLayer(1) }
                        UiButton {outlined:true; objectName: "layerDown"; text: "아래로"; onClicked: editor.moveLayer(-1) }
                    }
                    UiTextField {
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
    Loader {
        active:panel.ownsStructureDialog
        sourceComponent:Dialog {
        id: structureDialog
        objectName: panel.ownsStructureDialog?"territorialStructureDialog":""
        parent: Overlay.overlay
        modal: true; anchors.centerIn: parent; width: Math.min(parent.width - 32, 380)
        visible: panel.ownsStructureDialog&&editor.structureDialogOpen
        title: "영토 구조 변경"
        closePolicy: Popup.NoAutoClose
        contentItem: ScrollView {
          id: structureScroll
          clip: true
          implicitHeight: Math.min(structureColumn.implicitHeight, Math.max(120, structureDialog.parent.height - 120))
          ColumnLayout {
            id: structureColumn
            width: structureScroll.availableWidth
            spacing: 10
            Label { Layout.fillWidth: true; text: editor.structureState.detail; wrapMode: Text.WordWrap }
            Item {
                objectName: "territorialGeometryPreview"
                Layout.fillWidth: true; Layout.preferredHeight: visible ? 150 : 0
                visible: (editor.structureState.previewPaths || []).length > 0
                clip: true
                Item {
                    scale: Math.min(parent.width / (editor.structureState.previewWidth || 1), parent.height / (editor.structureState.previewHeight || 1))
                    transformOrigin: Item.TopLeft
                    Repeater {
                        model: editor.structureState.previewPaths || []
                        delegate: Shape {
                            required property var modelData
                            ShapePath {
                                strokeColor: "#c66c12"; strokeWidth: 0.06
                                fillColor: "#5582b7d7"; fillRule: ShapePath.OddEvenFill
                                PathSvg { path: modelData.path }
                            }
                        }
                    }
                }
            }
            UiTextField { id: createName; objectName: "territorialCreateName"; visible: editor.structureState.createSetup === true; Layout.fillWidth: true; placeholderText: "이름"; text: editor.structureState.name || "" }
            UiTextField { id: createId; objectName: "territorialCreateId"; visible: editor.structureState.createSetup === true; Layout.fillWidth: true; placeholderText: "객체 ID"; text: editor.structureState.generatedId || "" }
            UiComboBox {
                id: createParent; objectName: "territorialCreateParent"; visible: editor.structureState.createSetup === true && editor.structureState.kind === 0
                Layout.fillWidth: true; model: editor.relationParentOptions; textRole: "name"; valueRole: "id"; displayText: "상위 영역 선택"
            }
            UiButton {outlined:true;
                objectName: "saveTerritorialCreateSetup"; visible: editor.structureState.createSetup === true; text: "설정 저장 후 도형 그리기"
                onClicked: {
                    if (editor.updateTerritorialCreateSetup(createName.text, createParent.visible ? createParent.currentValue || "" : "", createId.text))
                        editor.beginGeometryDraw()
                }
            }
            Label { visible: editor.structureState.conversionSetup === true || (editor.structureState.generatedId !== undefined && editor.structureState.generatedId !== "" && !editor.structureState.createSetup); Layout.fillWidth: true; text: "객체 ID: " + (editor.structureState.generatedId || "") ; wrapMode: Text.WrapAnywhere }
            UiComboBox {
                id: conversionParent
                objectName: "conversionParent"
                visible: editor.structureState.conversionSetup === true
                Layout.fillWidth: true; model: editor.relationParentOptions; textRole: "name"; valueRole: "id"
                displayText: "상위 영역 선택"
            }
            UiButton {outlined:true;
                objectName: "applyConversionTarget"
                visible: editor.structureState.conversionSetup === true
                text: "전환 대상 설정"
                enabled: conversionParent.currentValue !== ""
                onClicked: editor.updateTypeConversionTarget(conversionParent.currentValue)
            }
            Repeater { model: editor.structureState.impacts || []; delegate: Label { required property var modelData; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; text: (modelData.kind === "transfer" ? "영토 이전·도형 갱신" : modelData.kind === "convert" ? "종류 전환" : modelData.messageKey) + " · " + modelData.id } }
            RowLayout {
                Layout.fillWidth: true
                UiButton {outlined:true; text: "취소"; onClicked: editor.cancelStructureMutation() }
                Item { Layout.fillWidth: true }
                UiButton {outlined:true; objectName: "confirmTerritorialStructure"; text: "확인"; enabled: editor.structureState.geometryRequired !== true && editor.structureState.conversionSetup !== true && editor.structureState.createSetup !== true; onClicked: editor.confirmStructureMutation() }
            }
          }
        }
    }
    }
}
