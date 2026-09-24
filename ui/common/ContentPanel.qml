import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ScrollView {
    id: root
    objectName: "contentPanel"
    property var editState: editor.contentEditState
    contentWidth: availableWidth
    function start(domain, type, create) { editor.beginContentEdit(domain, type, create) }
    ColumnLayout {
        width: root.availableWidth
        spacing: 6
        Flow {
            Layout.fillWidth: true; spacing: 4
            visible: !root.editState.active
            Button { text: "지명 추가"; onClicked: root.start("label", "custom", true) }
            Button { text: "강 추가"; onClicked: root.start("hydro", "river", true) }
            Button { text: "호수 추가"; onClicked: root.start("hydro", "lake", true) }
            Button { text: "분포 레이어"; onClicked: root.start("distributionLayer", "language", true) }
            Button { text: "분포 항목"; onClicked: root.start("distributionEntry", "", true) }
        }
        GroupBox {
            visible: !root.editState.active
            Layout.fillWidth: true
            title: "기본 수계 자료"
            ColumnLayout {
                anchors.fill: parent
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: editor.hydroDataStatus.ready ? "연결됨 · " + editor.hydroDataStatus.version : editor.hydroDataStatus.error }
                Button { text: "0.13.1 폴더 선택…"; onClicked: hydroFolder.open() }
            }
        }
        ComboBox {
            id: objects; Layout.fillWidth: true
            visible: !root.editState.active
            model: editor.objectRows; textRole: "name"
            onActivated: editor.selectObject(model[currentIndex], "replace", "list")
        }
        Flow {
            Layout.fillWidth: true; spacing: 4; visible: !root.editState.active
            Button { text: "선택 객체 편집"; enabled: !!editor.primaryObject.id && editor.primaryObject.domain !== "territorial" && editor.primaryObject.domain !== "hydroBuiltin"; onClicked: root.start(editor.primaryObject.domain, "", false) }
            Button { text: editor.hydroCopyBusy ? "수계 복사 중…" : "편집용 복사"; enabled: editor.primaryObject.domain === "hydroBuiltin" && !editor.hydroCopyBusy; onClicked: editor.copyBuiltinHydro() }
            Button { text: "수도 문자열"; onClicked: root.start("territorial", "capital", false) }
            Button { text: "국기"; onClicked: root.start("territorial", "flag", false) }
        }
        Label { visible: root.editState.active; Layout.fillWidth: true; text: (root.editState.domain || "") + " · " + (root.editState.id || ""); wrapMode: Text.WrapAnywhere }
        ColumnLayout {
            visible: root.editState.active; enabled: !root.editState.previewReady && !root.editState.drawing
            Layout.fillWidth: true
            Repeater {
                model: ["name", "notes", "capital", "color", "share", "certainty", "validFrom", "validTo"]
                delegate: TextField {
                    required property string modelData
                    Layout.fillWidth: true
                    visible: root.editState[modelData] !== undefined
                    placeholderText: ({name:"이름",notes:"메모",capital:"수도 문자열",color:"색상 #RRGGBB",parentId:"부모 분포 레이어 ID (없으면 공란)",layerId:"분포 레이어 ID",territoryId:"영토 ID (독립 도형이면 공란)",share:"비율 0–100",certainty:"확실성",validFrom:"시작 연도/날짜",validTo:"종료 연도/날짜"})[modelData]
                    text: root.editState[modelData] === undefined ? "" : String(root.editState[modelData])
                    onTextEdited: editor.updateContentField(modelData, text)
                }
            }
            Repeater {
                model: ["parentId", "layerId", "territoryId"]
                delegate: ComboBox {
                    required property string modelData
                    readonly property string field: modelData
                    visible: root.editState[field] !== undefined
                    Layout.fillWidth: true
                    textRole: "name"; valueRole: "id"
                    model: [{id:"", name:field === "parentId" ? "상위 분포 없음" : field === "layerId" ? "분포 레이어 선택" : "독립 도형 (영토 참조 없음)"}].concat(editor.objectRows.filter(function(row) { return row.domain === (field === "territoryId" ? "territorial" : "distributionLayer") && row.id !== root.editState.id }))
                    currentIndex: Math.max(0, model.findIndex(function(row) { return row.id === root.editState[field] }))
                    onActivated: editor.updateContentField(field, currentValue)
                }
            }
            ComboBox {
                visible: root.editState.domain === "label"; Layout.fillWidth: true
                model: ["capital", "city", "town", "region", "mountain", "water", "custom"]
                currentIndex: Math.max(0, model.indexOf(root.editState.kind))
                onActivated: editor.updateContentField("kind", currentText)
            }
            ComboBox {
                visible: root.editState.domain === "distributionLayer"; Layout.fillWidth: true
                model: ["language", "ethnicity", "religion"]
                currentIndex: Math.max(0, model.indexOf(root.editState.type))
                onActivated: editor.updateContentField("type", currentText)
            }
            CheckBox { visible: root.editState.locked !== undefined; text: "잠금"; checked: root.editState.locked === true; onClicked: editor.updateContentField("locked", checked) }
            Flow {
                Layout.fillWidth: true; spacing: 4; visible: root.editState.flagPolicy !== undefined
                Button { text: "기본"; onClicked: editor.updateContentField("flagPolicy", "default") }
                Button { text: "없음"; onClicked: editor.updateContentField("flagPolicy", "none") }
                Button { text: "이미지 업로드"; onClicked: flags.open() }
            }
            Label { visible: root.editState.flagPolicy !== undefined; text: "국기 설정: " + (root.editState.flagPolicy || "") + (root.editState.flagPolicy === "default" && !root.editState.flagAvailable ? " · " + (root.editState.flagReason || "사용 가능한 기본 자료 없음") : ""); wrapMode: Text.Wrap; Layout.fillWidth: true }
            Button {
                objectName: "contentDraw"
                visible: ["label", "hydro", "distributionEntry", "generic"].indexOf(root.editState.domain) >= 0
                text: "지도에서 위치·도형 편집"; onClicked: editor.beginContentGeometry()
            }
        }
        Label { visible: root.editState.drawing === true; text: "지도에서 편집한 뒤 지도 도구의 미리보기·확정을 사용하세요."; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Label { text: root.editState.error || ""; color: "#b42318"; wrapMode: Text.Wrap; Layout.fillWidth: true; visible: text.length > 0 }
        Flow {
            visible: root.editState.active; Layout.fillWidth: true; spacing: 4
            Button { text: "취소"; onClicked: editor.cancelContentEdit() }
            Button { objectName: "contentPreview"; text: "미리보기"; enabled: !root.editState.previewReady && !root.editState.drawing; onClicked: editor.previewContentEdit(false) }
            Button { text: "삭제 미리보기"; visible: !root.editState.create && root.editState.domain !== "territorial"; enabled: !root.editState.previewReady && !root.editState.drawing; onClicked: editor.previewContentEdit(true) }
            Button { objectName: "contentConfirm"; text: "확정"; enabled: root.editState.previewReady === true; onClicked: editor.confirmContentEdit() }
        }
    }
    FileDialog { id: flags; title: "국기 이미지"; nameFilters: ["이미지 (*.png *.jpg *.jpeg *.webp)"]; onAccepted: editor.loadContentFlag(selectedFile) }
    FolderDialog { id: hydroFolder; title: "수계 0.13.1 폴더"; onAccepted: editor.configureHydroData(selectedFolder) }
}
