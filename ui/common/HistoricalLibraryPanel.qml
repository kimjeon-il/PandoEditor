import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "historicalLibraryPanel"
    signal libraryFileRequested()
    property var ownershipChoices: ({})
    property string selectedId: ""
    property string snapshotId: ""
    modal: true
    title: "역사 라이브러리"
    width: Math.min(parent ? parent.width-16 : 860,860)
    height: Math.min(parent ? parent.height-24 : 660,660)
    anchors.centerIn: parent
    closePolicy: Popup.CloseOnEscape
    onClosed: editor.cancelHistoricalAdd()
    function updateSearch() {
        editor.searchHistorical(search.text,type.currentValue,status.currentValue,
                                date.text,region.text)
    }
    function options() {
        return {libraryId:selectedId,snapshotId:snapshotId,referenceDate:date.text,
                childDepth:depth.currentValue,geometryVersionId:editor.historicalPreview.selectedVersionId || "",
                approvePartial:partial.checked,ownership:ownershipChoices}
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 7
        RowLayout {
            Layout.fillWidth: true
            Button { text: "라이브러리 파일 선택"; onClicked: root.libraryFileRequested() }
            Label { Layout.fillWidth: true; text: editor.historicalStage==="unloaded" ? "라이브러리를 선택하세요" : editor.historicalCatalogName; elide: Text.ElideRight }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField { id: search; Layout.fillWidth: true; placeholderText: "이름 검색"; onTextChanged: root.updateSearch() }
            ComboBox { id: type; model: [{text:"전체",value:""},{text:"국가",value:"country"},{text:"하위",value:"subunit"},{text:"지방",value:"region"}]; textRole:"text"; valueRole:"value"; onActivated: root.updateSearch() }
            ComboBox { id: status; model: [{text:"전체",value:"all"},{text:"현재",value:"current"},{text:"과거",value:"past"}]; textRole:"text"; valueRole:"value"; onActivated: root.updateSearch() }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField { id: date; Layout.fillWidth: true; placeholderText: "기준 날짜 (예: 1945)"; onTextChanged: {root.updateSearch();if(root.selectedId)editor.selectHistorical(root.selectedId,"",text);else editor.cancelHistoricalAdd()} }
            TextField { id: region; Layout.fillWidth: true; placeholderText: "지역"; onTextChanged: root.updateSearch() }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: "스냅샷" }
            ComboBox {
                id: snapshots; Layout.fillWidth: true
                model: [{id:"",name:"개별 항목"}].concat(editor.historicalSnapshots)
                textRole:"name"; valueRole:"id"
                onActivated: {
                    root.snapshotId=currentValue
                    editor.cancelHistoricalAdd()
                    if(currentIndex>0 && !date.text)date.text=model[currentIndex].referenceDate || ""
                }
            }
            Label { text: "하위" }
            ComboBox { id: depth; model: [{text:"없음",value:"none"},{text:"1단계",value:"level1"},{text:"전체",value:"all"}]; textRole:"text"; valueRole:"value"; onActivated: editor.cancelHistoricalAdd() }
        }
        GridLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            columns: root.width < 600 ? 1 : 2
            columnSpacing: 8
            rowSpacing: 8
            ListView {
                id: results
                objectName: "historicalResults"
                Layout.preferredWidth: root.width < 600 ? root.width-32 : root.width*0.38
                Layout.preferredHeight: root.width < 600 ? 110 : -1
                Layout.fillHeight: root.width >= 600
                clip: true
                model: editor.historicalResults
                delegate: ItemDelegate {
                    required property var modelData
                    width: results.width
                    highlighted: root.selectedId===modelData.id
                    text: modelData.name+" · "+modelData.type
                    onClicked: {
                        root.selectedId=modelData.id;root.snapshotId="";snapshots.currentIndex=0
                        root.ownershipChoices=({})
                        editor.selectHistorical(modelData.id,"",date.text)
                    }
                }
            }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.minimumWidth: root.width < 600 ? root.width-32 : 180
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    HistoricalEntityPreview {
                        Layout.fillWidth: true
                        visible: !!root.selectedId
                        preview: editor.historicalPreview
                    }
                    ComboBox {
                        id: version
                        Layout.fillWidth: true
                        visible: (editor.historicalPreview.versions || []).length>1
                        model: editor.historicalPreview.versions || []
                        textRole: "id"; valueRole: "id"
                        currentIndex: {
                            const versions=editor.historicalPreview.versions || []
                            const wanted=editor.historicalPreview.geometryVersionId || ""
                            for (let i=0;i<versions.length;i++) if(versions[i].id===wanted)return i
                            return 0
                        }
                        onActivated: editor.selectHistorical(root.selectedId,currentValue,date.text)
                    }
                    HistoricalOwnershipSetup {
                        Layout.fillWidth: true
                        visible: editor.historicalStage==="ownership"
                        needed: editor.historicalOwnershipNeeded
                        countries: editor.historicalCountries
                        onChoiceChanged: function(id,choice) {
                            const next=Object.assign({},root.ownershipChoices)
                            next[id]=choice;root.ownershipChoices=next
                        }
                    }
                    CheckBox {
                        id: partial
                        visible: !!editor.historicalPreview.partial || !!root.snapshotId
                        text: "누락 자료를 확인하고 부분 추가에 동의"
                        onToggled: editor.cancelHistoricalAdd()
                    }
                    GroupBox {
                        Layout.fillWidth: true
                        visible: editor.historicalStage==="impact"
                        title: "영향 확인"
                        ColumnLayout {
                            anchors.fill: parent
                            Label { Layout.fillWidth: true; text: editor.historicalImpact.summary || ""; wrapMode: Text.Wrap }
                            Label { Layout.fillWidth: true; text: "영토 조정: " + (editor.historicalImpact.adjusted || []).join(", "); wrapMode: Text.Wrap }
                            Label { Layout.fillWidth: true; text: "이름 갱신: " + (editor.historicalImpact.updated || []).join(", "); wrapMode: Text.Wrap }
                        }
                    }
                }
            }
        }
        Label { Layout.fillWidth: true; visible: !!editor.historicalError; text: editor.historicalError; color: "#a73535"; wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: "닫기"; onClicked: root.close() }
            Button {
                objectName: "historicalAddButton"
                text: editor.historicalStage==="impact" ? "확정" : editor.historicalStage==="preparing" ? "계산 중" : "추가"
                enabled: editor.historicalStage!=="preparing" && (root.snapshotId || root.selectedId)
                onClicked: {
                    if(editor.historicalStage==="impact") {
                        if(editor.confirmHistoricalAdd(editor.historicalSession))root.close()
                    } else editor.prepareHistoricalAdd(root.options())
                }
            }
        }
    }
}
