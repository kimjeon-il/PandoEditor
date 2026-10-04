import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    id: root
    objectName: "contentPanel"
    property var editState: editor.contentEditState
    property bool integrated: false
    property string flagOwner: ""
    property bool holdCommits: false
    property bool autoSelection: true
    property bool showCreationTools: false
    readonly property bool selectionContent: editor.selectionItems.length === 1 && ["label","hydro","distributionLayer","distributionEntry","generic"].indexOf(editor.primaryObject.domain) >= 0
    property bool flagExternal:false
    property var flagTrigger:null
    function showFlagMenu(trigger){root.start("territorial","flag",false);flagTrigger=trigger;const point=trigger.mapToItem(Overlay.overlay,0,trigger.height);flagMenu.x=Math.max(8,Math.min(point.x,Overlay.overlay.width-flagMenu.width-8));flagMenu.y=Math.max(8,Math.min(point.y,Overlay.overlay.height-flagMenu.height-8));flagMenu.open()}
    function dismissPopup(){if(flagMenu.visible){flagMenu.close();return true}if(flagGallery.visible){flagGallery.close();return true}return false}
    function syncSelection() {
        if (!integrated || !visible || !autoSelection || showCreationTools || !selectionContent || editState.active) return
        editor.beginContentEdit(editor.primaryObject.domain, "", false)
    }
    onVisibleChanged: if (visible) { autoSelection = true; Qt.callLater(syncSelection) }
    Connections {
        target: editor
        function onSelectionChanged() {
            if (!root.integrated) return
            flagMenu.close();flagGallery.close()
            if (root.editState.active && !root.editState.create &&
                (editor.selectionItems.length !== 1 || root.editState.id !== editor.primaryObject.id || root.editState.domain !== editor.primaryObject.domain))
                editor.cancelContentEdit()
            root.showCreationTools = false
            root.autoSelection = true
            Qt.callLater(root.syncSelection)
        }
    }

    readonly property real contentHeight: form.implicitHeight
    function start(domain, type, create) {
        if (root.editState.active) editor.cancelContentEdit()
        root.autoSelection = false
        editor.beginContentEdit(domain, type, create)
    }
    function update(field, value) { if (editor.updateContentField(field, value) && !editState.create) editor.commitContentField(field) }
    ColumnLayout {
        id: form
        Layout.fillWidth: true
        spacing: 6
        Flow {
            Layout.fillWidth: true; spacing: 4
            visible: !root.editState.active && (!root.integrated || root.showCreationTools || editor.selectionItems.length === 0)
            UiButton {outlined:true; text: "지명 추가"; onClicked: root.start("label", "custom", true) }
            UiButton {outlined:true; text: "강 추가"; onClicked: root.start("hydro", "river", true) }
            UiButton {outlined:true; text: "호수 추가"; onClicked: root.start("hydro", "lake", true) }
            UiButton {outlined:true; objectName: "contentAddDistributionLayer"; text: "분포 레이어"; onClicked: root.start("distributionLayer", "", true) }
            UiButton {outlined:true; text: "분포 항목"; onClicked: root.start("distributionEntry", "", true) }
        }
        GroupBox {
            visible: !root.editState.active && (!root.integrated || root.showCreationTools || editor.selectionItems.length === 0)
            Layout.fillWidth: true
            title: "기본 수계 자료"
            ColumnLayout {
                anchors.fill: parent
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: editor.hydroDataStatus.ready ? "연결됨 · " + editor.hydroDataStatus.version : editor.hydroDataStatus.error }
                UiButton {outlined:true; text: "0.13.1 폴더 선택…"; onClicked: hydroFolder.open() }
            }
        }
        UiComboBox {
            id: objects; Layout.fillWidth: true
            visible: !root.editState.active && (!root.integrated || root.showCreationTools || editor.selectionItems.length === 0)
            model: editor.objectRows; textRole: "name"
            onActivated: editor.selectObject(model[currentIndex], "replace", "list")
        }
        Flow {
            Layout.fillWidth: true; spacing: 4; visible: !root.editState.active && editor.selectionItems.length === 1
            UiButton {outlined:true; visible:root.selectionContent; text: "선택 객체 편집"; enabled: !!editor.primaryObject.id && editor.primaryObject.domain !== "territorial" && editor.primaryObject.domain !== "hydroBuiltin"; onClicked: root.start(editor.primaryObject.domain, "", false) }
            UiButton {outlined:true; visible:editor.primaryObject.domain==="hydroBuiltin"; text: editor.hydroCopyBusy ? "수계 복사 중…" : "편집용 복사"; enabled: editor.primaryObject.domain === "hydroBuiltin" && !editor.hydroCopyBusy; onClicked: editor.copyBuiltinHydro() }
            UiButton {outlined:true; objectName: "contentCapital"; visible: editor.primaryObject.domain === "territorial" && editor.primaryObject.type === "general"; text: "수도"; onClicked: root.start("territorial", "capital", false) }
        }
        Label { visible: root.editState.active; Layout.fillWidth: true; text: ({territorial:"국가·영토",label:"지명",hydro:"수계",distributionLayer:"분포 레이어",distributionEntry:"분포 항목",generic:"지도 객체"})[root.editState.domain] || ""; wrapMode: Text.WrapAnywhere }
        ColumnLayout {
            visible: root.editState.active; enabled: !root.editState.previewReady && !root.editState.drawing
            Layout.fillWidth: true
            Repeater {
                model: ["name", "notes", "capital", "color", "unit", "value", "valueMin", "valueMax", "certainty", "validFrom", "validTo"]
                delegate: UiTextField {
                    objectName: "contentField_" + modelData
                    property string owner: ""
                    property bool edited: false
                    onActiveFocusChanged: if (activeFocus) { owner = root.editState.domain + ":" + root.editState.id; edited = false }

                    required property string modelData
                    Layout.fillWidth: true
                    visible: root.editState[modelData] !== undefined && (["valueMin","valueMax"].indexOf(modelData)<0 || root.editState.valueScaleMode === "manual")
                    placeholderText: ({name:"이름",notes:"메모",capital:"수도 문자열",color:"색상 #RRGGBB",unit:"단위",value:"값",valueMin:"색 농도 최솟값",valueMax:"색 농도 최댓값",certainty:"확실성",validFrom:"시작 연도/날짜",validTo:"종료 연도/날짜"})[modelData]
                    text: root.editState[modelData] === undefined ? "" : String(root.editState[modelData])
                    onTextEdited: { if (owner === root.editState.domain + ":" + root.editState.id) edited = editor.updateContentField(modelData, text) }
                    onEditingFinished: { if (edited && !root.holdCommits && !root.editState.create && owner === root.editState.domain + ":" + root.editState.id) editor.commitContentField(modelData); edited = false }
                }
            }
            Repeater {
                model: ["parentId", "layerId", "territoryId"]
                delegate: UiComboBox {
                    required property string modelData
                    readonly property string field: modelData
                    visible: root.editState[field] !== undefined
                    Layout.fillWidth: true
                    textRole: "name"; valueRole: "id"
                    model: visible ? [{id:"", name:field === "parentId" ? "상위 분포 없음" : field === "layerId" ? "분포 레이어 선택" : "독립 도형 (영토 참조 없음)"}].concat(editor.objectRows.filter(function(row) { return row.domain === (field === "territoryId" ? "territorial" : "distributionLayer") && row.id !== root.editState.id })) : []
                    currentIndex: Math.max(0, model.findIndex(function(row) { return row.id === root.editState[field] }))
                    onActivated: root.update(field, currentValue)
                }
            }
            UiComboBox {
                objectName: "contentKind"; visible: root.editState.domain === "label" || root.editState.domain === "hydro"; Layout.fillWidth: true
                model: root.editState.domain === "hydro" ? [{id:"river",name:"강"},{id:"lake",name:"호수"}] : [{id:"capital",name:"수도"},{id:"city",name:"도시"},{id:"town",name:"마을"},{id:"region",name:"지역"},{id:"mountain",name:"산"},{id:"water",name:"수계"},{id:"custom",name:"기타"}]
                textRole:"name";valueRole:"id"
                currentIndex: Math.max(0, model.findIndex(function(row){return row.id===root.editState.kind}))
                onActivated: root.update("kind", currentValue)
            }
            UiComboBox {
                visible: root.editState.domain === "distributionLayer"; Layout.fillWidth: true
                model: [{id:"auto",name:"색 농도 범위: 자동"},{id:"manual",name:"색 농도 범위: 직접 지정"}];textRole:"name";valueRole:"id"
                currentIndex: root.editState.valueScaleMode === "manual" ? 1 : 0
                onActivated: root.update("valueScaleMode", currentValue)
            }
            UiSwitch { visible: root.editState.locked !== undefined; text: "잠금"; checked: root.editState.locked === true; onClicked: root.update("locked", checked) }
            Label { visible: root.editState.flagPolicy !== undefined; text: "국기 설정: " + ({default:"기본",none:"없음",embedded:"이미지"}[root.editState.flagPolicy] || "") + (root.editState.flagPolicy === "default" && !root.editState.flagAvailable ? " · " + (root.editState.flagReason || "사용 가능한 기본 자료 없음") : ""); wrapMode: Text.Wrap; Layout.fillWidth: true }
            UiButton {outlined:true;
                objectName: "contentDraw"
                visible: ["label", "hydro", "distributionEntry", "generic"].indexOf(root.editState.domain) >= 0
                text: "지도에서 위치·도형 편집"; onClicked: editor.beginContentGeometry()
            }
        }
        Label { visible: root.editState.drawing === true; text: "지도에서 편집한 뒤 지도 도구의 미리보기·확정을 사용하세요."; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Label { text: root.editState.error || ""; color: "#b42318"; wrapMode: Text.Wrap; Layout.fillWidth: true; visible: text.length > 0 }
        Flow {
            visible: root.editState.active; Layout.fillWidth: true; spacing: 4
            UiButton {outlined:true; objectName: "contentCancel"; text: root.editState.create ? "취소" : "닫기"; onClicked: { root.autoSelection = false; editor.cancelContentEdit() } }
            UiButton {outlined:true; objectName: "contentPreview"; text: "미리보기"; visible: root.editState.create === true; enabled: !root.editState.previewReady && !root.editState.drawing; onClicked: editor.previewContentEdit(false) }
            UiButton {outlined:true; text: "삭제 미리보기"; visible: !root.editState.create && root.editState.domain !== "territorial"; enabled: !root.editState.previewReady && !root.editState.drawing; onClicked: editor.previewContentEdit(true) }
            UiButton {outlined:true; objectName: "contentConfirm"; text: "확정"; enabled: root.editState.previewReady === true; onClicked: editor.confirmContentEdit() }
        }
    }
    Popup {
        id:flagMenu;objectName:"flagMenu";parent:Overlay.overlay;width:Math.min(250,parent.width-16);padding:6;focus:true
        closePolicy:Popup.CloseOnEscape|Popup.CloseOnPressOutside
        onOpened:{root.flagExternal=false;flagLibraryAction.forceActiveFocus()}
        onClosed:{if(root.flagTrigger)root.flagTrigger.forceActiveFocus();if(!root.flagExternal&&root.editState.flagPolicy!==undefined)editor.cancelContentEdit()}
        contentItem:ColumnLayout {
            UiButton {id:flagLibraryAction;objectName:"flagLibraryButton";menuItem:true;symbol:"library";text:"국기 라이브러리";Layout.fillWidth:true;onClicked:{root.flagExternal=true;flagMenu.close();root.flagOwner=root.editState.domain+":"+root.editState.id;flagGallery.open()}}
            UiButton {objectName:"flagUploadButton";menuItem:true;symbol:"folder";text:"파일";Layout.fillWidth:true;onClicked:{root.flagExternal=true;flagMenu.close();root.flagOwner=root.editState.domain+":"+root.editState.id;flags.open()}}
            UiButton {objectName:"flagDefaultButton";menuItem:true;symbol:"country";text:"기본 국기";Layout.fillWidth:true;onClicked:{root.update("flagPolicy","default");flagMenu.close()}}
            UiButton {objectName:"flagRemoveButton";menuItem:true;symbol:"close";text:"제거";Layout.fillWidth:true;enabled:root.editState.flagPolicy!=="none";onClicked:{root.update("flagPolicy","none");flagMenu.close()}}
        }
    }
    HistoricalLibraryPanel {
        id:flagGallery;objectName:"flagLibraryDialog";flagMode:true;parent:Overlay.overlay
        onFlagPicked:source=>{if(root.flagOwner===root.editState.domain+":"+root.editState.id&&editor.loadContentFlag(source)&&editor.commitContentField("flagSource"))editor.cancelContentEdit()}
        onClosed:{if(root.editState.flagPolicy!==undefined&&root.flagOwner===root.editState.domain+":"+root.editState.id)editor.cancelContentEdit();if(root.flagTrigger)root.flagTrigger.forceActiveFocus()}
    }

    FileDialog { id: flags; title: "국기 이미지"; nameFilters: ["이미지 (*.png *.jpg *.jpeg *.webp *.svg)"]; onAccepted: if (root.flagOwner === root.editState.domain + ":" + root.editState.id && editor.loadContentFlag(selectedFile) && !root.editState.create){if(editor.commitContentField("flagSource"))editor.cancelContentEdit()}
        onRejected:if(root.flagOwner===root.editState.domain+":"+root.editState.id&&root.editState.flagPolicy!==undefined)editor.cancelContentEdit() }
    FolderDialog { id: hydroFolder; title: "수계 0.13.1 폴더"; onAccepted: editor.configureHydroData(selectedFolder) }
}
