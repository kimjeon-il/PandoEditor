import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "gisExportPanel"
    signal destinationRequested(string format,var selected)
    modal: true
    title: "GIS 데이터 내보내기"
    width: Math.min(parent ? parent.width-16 : 520,520)
    height: Math.min(parent ? parent.height-24 : 520,520)
    anchors.centerIn: parent
    closePolicy: Popup.CloseOnEscape
    onClosed: editor.cancelGisExport()
    property var state: editor.gisExportState
    function selectedLayers() {
        const selected=[]
        for(let i=0;i<layerRepeater.count;++i) {
            const item=layerRepeater.itemAt(i)
            if(item && item.checked && item.enabled)selected.push(item.category)
        }
        return selected
    }
    ColumnLayout {
        anchors.fill:parent
        spacing:12
        Label {objectName:"gisExportStep";text:root.state.stage==="working"?"2 · 파일 생성":root.state.stage==="done"?"3 · 저장 완료":"1 · 내보낼 항목 선택";font.bold:true;font.pixelSize:18}
        Label {
            Layout.fillWidth:true
            text:"프로젝트에서 선택한 레이어를 GIS 파일로 내보냅니다. 프로젝트 전체 복원용 저장과 별개입니다."
            wrapMode:Text.WordWrap
        }
        UiComboBox {
            id: formatChoice
            objectName:"gisExportFormat"
            Layout.fillWidth:true
            model:[{text:"GeoJSON ZIP",value:"geojson-zip"},
                   {text:"GIS GeoPackage",value:"geopackage"}]
            textRole:"text";valueRole:"value"
            enabled:root.state.stage!=="working"
        }
        ScrollView {
            Layout.fillWidth:true
            Layout.fillHeight:true
            ColumnLayout {
                width:parent.width
                Repeater {
                    id:layerRepeater
                    model:editor.gisExportLayers
                    UiSwitch {
                        property string category:modelData.category
                        objectName:"gisExportLayer_"+category
                        text:modelData.name+" · "+modelData.count+"개"
                        visible:modelData.count>0
                        enabled:modelData.count>0 && root.state.stage!=="working"
                        checked:modelData.count>0
                        Layout.fillWidth:true
                    }
                }
            }
        }
        BusyIndicator {
            Layout.alignment:Qt.AlignHCenter
            running:root.state.stage==="working"
            visible:running
        }
        Label {
            Layout.fillWidth:true
            visible:root.state.stage==="done"
            text:"저장했습니다: "+(root.state.fileName||"")
            wrapMode:Text.WrapAnywhere
        }
        UiNotice {Layout.fillWidth:true;visible:!!root.state.error;kind:"error";text:root.state.error||""}
        RowLayout {
            Layout.fillWidth:true
            UiButton {text:"닫기";onClicked:root.close()}
            Item {Layout.fillWidth:true}
            UiButton {
                objectName:"gisExportConfirm"
                text:"내보낼 위치 선택"
                enabled:root.state.stage!=="working" && root.selectedLayers().length>0
                onClicked:root.destinationRequested(formatChoice.currentValue,root.selectedLayers())
            }
        }
    }
}
