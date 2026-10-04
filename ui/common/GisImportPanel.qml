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
    readonly property string stepLabel:state.stage==="reading"||state.stage==="empty"||state.stage==="error"?"1 · 파일 확인":state.stage==="impact"?"3 · 결과 검토":"2 · 속성 연결"
    function resetFields() {
        const layers=state.layers || []
        const layer=layers[layerChoice.currentIndex]
        if (!layer) return
        const values=["country","subunit","region","distribution","generic"]
        let requested=values.indexOf(layer.target)
        targetChoice.currentIndex=requested<0?4:requested
        const web=state.sourceKind==="geojson-zip" || state.sourceKind==="geopackage"
        idField.text=layer.target==="country" && web?"pandolab_id":
                     layer.target==="distribution"?"entry_id":"__fid__"
        nameField.text=layer.target==="country" && web?"pandolab_name":"name"
    }
    function choices() {
        return {target:targetChoice.currentValue,idField:idField.text,nameField:nameField.text,
            parentId:parentId.currentValue,coast:coastChoice.currentValue,
            layerId:layerId.text,
            layerName:layerName.text}
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        Label {objectName:"gisImportStep";text:root.stepLabel;font.bold:true;font.pixelSize:18}
        RowLayout {
            Layout.fillWidth: true
            UiButton {symbol:"folder"; objectName:"gisChooseFile";text:"GIS 파일 선택";onClicked:root.fileRequested() }
            Label { Layout.fillWidth:true;text:root.state.fileName || "GeoJSON · ZIP · GeoPackage";elide:Text.ElideMiddle }
        }
        Label {
            Layout.fillWidth:true
            text:"GIS 데이터 가져오기는 현재 프로젝트에 추가합니다. 프로젝트 파일 열기와 별개의 작업입니다."
            wrapMode:Text.WordWrap
        }
        ProgressBar {Layout.fillWidth:true;indeterminate:true;visible:root.state.stage==="reading"||root.state.stage==="preparing"}
        Label {visible:root.state.stage==="reading"||root.state.stage==="preparing";text:root.state.stage==="reading"?"선택한 파일을 확인하고 있습니다.":"적용 결과를 계산하고 있습니다."}
        ScrollView {
            Layout.fillWidth:true;Layout.fillHeight:true
            contentWidth:availableWidth
            ColumnLayout {
                width:parent.width
                spacing:12
                enabled:root.state.stage!=="reading"&&root.state.stage!=="preparing"
                Label {text:root.state.stage==="impact"?"적용 결과":"레이어와 속성 연결";font.bold:true}
                ColumnLayout {Layout.fillWidth:true;visible:root.state.stage==="mapping"||root.state.stage==="preparing";enabled:root.state.stage==="mapping";spacing:12
                Label { text:"가져올 레이어";visible:(root.state.layers || []).length>0 }
                UiComboBox {
                    id:layerChoice;objectName:"gisLayerChoice"
                    Layout.fillWidth:true
                    model:root.state.layers || []
                    textRole:"name";valueRole:"index"
                    onCurrentIndexChanged:root.resetFields()
                }
                Label { text:"대상 종류";visible:layerChoice.count>0 }
                UiComboBox {
                    id:targetChoice;objectName:"gisTargetChoice"
                    Layout.fillWidth:true
                    model:[{text:"일반 객체 · 독립",value:"country"},{text:"일반 객체 · 부모 있음",value:"subunit"},{text:"독립 권역",value:"region"},{text:"분포",value:"distribution"},{text:"기타 객체",value:"generic"}];textRole:"text";valueRole:"value"
                }
                RowLayout {
                    Layout.fillWidth:true
                    Label { text:"객체 ID 필드" }
                    UiTextField { id:idField;objectName:"gisIdField";Layout.fillWidth:true;placeholderText:"__fid__ 또는 속성 이름" }
                }
                RowLayout {
                    Layout.fillWidth:true
                    Label { text:"이름 필드" }
                    UiTextField { id:nameField;objectName:"gisNameField";Layout.fillWidth:true }
                }
                Label {
                    Layout.fillWidth:true;wrapMode:Text.WordWrap
                    text:"__fid__는 GeoJSON feature ID를 사용합니다. ID가 속성에 있다면 해당 필드명을 입력하세요. 중복 ID는 가져오기를 중단합니다."
                    font.pixelSize:11
                }
                RowLayout {
                    Layout.fillWidth:true
                    visible:targetChoice.currentValue==="subunit"
                    Label { text:"상위 단위" }
                    UiComboBox {id:parentId;objectName:"gisParentId";Layout.fillWidth:true;textRole:"name";valueRole:"id";model:[{id:"",name:"각 객체의 상위 필드 사용"}].concat(editor.relationParentOptions)}
                }
                RowLayout {
                    Layout.fillWidth:true
                    visible:targetChoice.currentValue==="country"||targetChoice.currentValue==="subunit"
                    Label { text:"해안선·영토 충돌" }
                    UiComboBox {
                        id:coastChoice;objectName:"gisCoastChoice";Layout.fillWidth:true
                        model:[{text:"충돌 시 중단",value:"reject"},
                               {text:"가져온 경계 우선 · 영토 이전",value:"imported"},
                               {text:"기존 국가 경계 우선",value:"country"}]
                        textRole:"text";valueRole:"value"
                    }
                }
                RowLayout {
                    Layout.fillWidth:true;visible:targetChoice.currentValue==="distribution"
                    Label { text:"분포 종류" }
                }
                RowLayout {
                    Layout.fillWidth:true;visible:targetChoice.currentValue==="distribution"
                    Label { text:"레이어 ID" }
                    UiTextField { id:layerId;Layout.fillWidth:true;placeholderText:"비우면 각 객체의 layer_id 사용" }
                }
                RowLayout {
                    Layout.fillWidth:true;visible:targetChoice.currentValue==="distribution"
                    Label { text:"레이어 이름" }
                    UiTextField { id:layerName;Layout.fillWidth:true;placeholderText:"비우면 name 필드 사용" }
                }
                }
                Label {
                    Layout.fillWidth:true;wrapMode:Text.WordWrap
                    visible:root.state.stage==="impact"
                    text:"영향 미리보기: " + (root.state.summary || "")
                }
                UiNotice {Layout.fillWidth:true;visible:!!root.state.error;kind:"error";text:root.state.error||""}
            }
        }
        RowLayout {
            Layout.fillWidth:true
            UiButton {objectName:"gisReviewBack";visible:root.state.stage==="impact";text:"이전";onClicked:editor.backGisImport()}
            UiButton { text:"취소";onClicked:root.close() }
            Item { Layout.fillWidth:true }
            UiButton {
                objectName:"gisPrepare";text:"영향 확인"
                visible:root.state.stage!=="impact";enabled:root.state.stage==="mapping" && layerChoice.currentIndex>=0
                onClicked:editor.prepareGisImport(layerChoice.currentValue,root.choices())
            }
            UiButton {
                objectName:"gisConfirm";text:"가져오기 확정"
                visible:root.state.stage==="impact";enabled:root.state.stage==="impact"
                onClicked:if(editor.confirmGisImport(root.state.session))root.close()
            }
        }
    }
}
