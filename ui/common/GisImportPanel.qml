import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "gisImportPanel"
    signal fileRequested()
    modal: true
    title: "GIS 데이터 가져오기"
    width: Math.min(parent ? parent.width-16 : 720,720)
    height: Math.min(parent ? parent.height-24 : 620,620)
    anchors.centerIn: parent
    closePolicy: Popup.CloseOnEscape
    onClosed: editor.cancelGisImport()
    property var state: editor.gisImportState
    function resetFields() {
        const layers=state.layers || []
        const layer=layers[layerChoice.currentIndex]
        if (!layer) return
        const values=["country","subunit","region","distribution","generic"]
        let requested=values.indexOf(layer.target)
        targetChoice.currentIndex=requested<0?4:requested
        distributionType.currentIndex=["language","ethnicity","religion"].indexOf(layer.distributionType)
        if(distributionType.currentIndex<0)distributionType.currentIndex=0
        const web=state.sourceKind==="geojson-zip" || state.sourceKind==="geopackage"
        idField.text=layer.target==="country" && web?"pandolab_id":
                     layer.target==="distribution"?"entry_id":"__fid__"
        nameField.text=layer.target==="country" && web?"pandolab_name":"name"
    }
    function choices() {
        return {target:targetChoice.currentText,idField:idField.text,nameField:nameField.text,
            countryId:countryId.text,parentId:parentId.text,coast:coastChoice.currentValue,
            distributionType:distributionType.currentText,layerId:layerId.text,
            layerName:layerName.text}
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Button { objectName:"gisChooseFile";text:"GIS 파일 선택";onClicked:root.fileRequested() }
            Label { Layout.fillWidth:true;text:root.state.fileName || "GeoJSON · ZIP · GeoPackage";elide:Text.ElideMiddle }
        }
        Label {
            Layout.fillWidth:true
            text:"GIS 데이터 가져오기는 현재 프로젝트에 추가합니다. 프로젝트 파일 열기와 별개의 작업입니다."
            wrapMode:Text.WordWrap
        }
        BusyIndicator { Layout.alignment:Qt.AlignHCenter;running:root.state.stage==="reading"||root.state.stage==="preparing";visible:running }
        ScrollView {
            Layout.fillWidth:true;Layout.fillHeight:true
            contentWidth:availableWidth
            ColumnLayout {
                width:parent.width
                spacing:8
                Label { text:"가져올 레이어";visible:(root.state.layers || []).length>0 }
                ComboBox {
                    id:layerChoice;objectName:"gisLayerChoice"
                    Layout.fillWidth:true
                    model:root.state.layers || []
                    textRole:"name";valueRole:"index"
                    onCurrentIndexChanged:root.resetFields()
                }
                Label { text:"대상 종류";visible:layerChoice.count>0 }
                ComboBox {
                    id:targetChoice;objectName:"gisTargetChoice"
                    Layout.fillWidth:true
                    model:["country","subunit","region","distribution","generic"]
                }
                RowLayout {
                    Layout.fillWidth:true
                    Label { text:"객체 ID 필드" }
                    TextField { id:idField;objectName:"gisIdField";Layout.fillWidth:true;placeholderText:"__fid__ 또는 속성 이름" }
                }
                RowLayout {
                    Layout.fillWidth:true
                    Label { text:"이름 필드" }
                    TextField { id:nameField;objectName:"gisNameField";Layout.fillWidth:true }
                }
                Label {
                    Layout.fillWidth:true;wrapMode:Text.WordWrap
                    text:"__fid__는 GeoJSON feature ID를 사용합니다. ID가 속성에 있다면 해당 필드명을 입력하세요. 중복 ID는 가져오기를 중단합니다."
                    font.pixelSize:11
                }
                RowLayout {
                    Layout.fillWidth:true
                    visible:targetChoice.currentText==="subunit"||targetChoice.currentText==="region"
                    Label { text:"소속 국가 ID" }
                    TextField { id:countryId;objectName:"gisCountryId";Layout.fillWidth:true;placeholderText:"비우면 각 객체의 sovereign_id 사용" }
                }
                RowLayout {
                    Layout.fillWidth:true
                    visible:targetChoice.currentText==="subunit"||targetChoice.currentText==="region"
                    Label { text:"상위 단위 ID" }
                    TextField { id:parentId;objectName:"gisParentId";Layout.fillWidth:true;placeholderText:"비우면 각 객체의 parent_id 사용" }
                }
                RowLayout {
                    Layout.fillWidth:true
                    visible:targetChoice.currentText==="country"||targetChoice.currentText==="subunit"
                    Label { text:"해안선·영토 충돌" }
                    ComboBox {
                        id:coastChoice;objectName:"gisCoastChoice";Layout.fillWidth:true
                        model:[{text:"충돌 시 중단",value:"reject"},
                               {text:"가져온 경계 우선 · 영토 이전",value:"imported"},
                               {text:"기존 국가 경계 우선",value:"country"}]
                        textRole:"text";valueRole:"value"
                    }
                }
                RowLayout {
                    Layout.fillWidth:true;visible:targetChoice.currentText==="distribution"
                    Label { text:"분포 종류" }
                    ComboBox { id:distributionType;Layout.fillWidth:true;model:["language","ethnicity","religion"] }
                }
                RowLayout {
                    Layout.fillWidth:true;visible:targetChoice.currentText==="distribution"
                    Label { text:"레이어 ID" }
                    TextField { id:layerId;Layout.fillWidth:true;placeholderText:"비우면 각 객체의 layer_id 사용" }
                }
                RowLayout {
                    Layout.fillWidth:true;visible:targetChoice.currentText==="distribution"
                    Label { text:"레이어 이름" }
                    TextField { id:layerName;Layout.fillWidth:true;placeholderText:"비우면 name 필드 사용" }
                }
                Label {
                    Layout.fillWidth:true;wrapMode:Text.WordWrap
                    visible:root.state.stage==="impact"
                    text:"영향 미리보기: " + (root.state.summary || "")
                }
                Label {
                    Layout.fillWidth:true;wrapMode:Text.WrapAnywhere
                    visible:!!root.state.error;color:"#ae2828"
                    text:root.state.error || ""
                }
            }
        }
        RowLayout {
            Layout.fillWidth:true
            Button { text:"취소";onClicked:root.close() }
            Item { Layout.fillWidth:true }
            Button {
                objectName:"gisPrepare";text:"영향 확인"
                enabled:root.state.stage==="mapping" && layerChoice.currentIndex>=0
                onClicked:editor.prepareGisImport(layerChoice.currentValue,root.choices())
            }
            Button {
                objectName:"gisConfirm";text:"가져오기 확정"
                enabled:root.state.stage==="impact"
                onClicked:if(editor.confirmGisImport(root.state.session))root.close()
            }
        }
    }
}
