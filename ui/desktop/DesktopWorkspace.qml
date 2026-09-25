import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../common" as Common

Item {
    id: workspace
    property bool compact: width < 800
    property bool mobileMode: false
    property bool holdFieldCommits: false
    signal webImportRequested()
    signal historicalLibraryRequested()
    signal gisImportRequested()
    signal gisExportRequested()
    signal openRequested()
    signal saveRequested()
    signal saveAsRequested()
    property bool editorOpen:false
    property bool searchOpen:false
    property bool legacyOpen:false
    Common.MapDisplayControls { id: displayControls; parent: workspace }
    readonly property bool sideOpen:editorOpen||searchOpen||legacyOpen
    property bool pointerNavigation:false
    function navigationStarted(){
        if(pointerNavigation)return
        toolbar.navigationStarted();properties.navigationStarted();panel.beginSelectionNavigation()
    }
    function navigationPointer(down){
        if(down){pointerNavigation=true;toolbar.navigating=true;properties.navigating=true;panel.selectionNavigation=true}
        else Qt.callLater(function(){workspace.pointerNavigation=false;toolbar.navigating=false;properties.navigating=false;panel.selectionNavigation=false})
    }
    function dismissPopup() {
        if(displayControls.visible){displayControls.close();return true}
        if(toolbar.dismissPopup()||mapView.dismissPopup()||properties.dismissPopup()||panel.dismissPopup())return true
        if(editorOpen||searchOpen){navigationStarted();editorOpen=false;searchOpen=false;return true}
        return false
    }
    function toggleEditor(){navigationStarted();searchOpen=false;legacyOpen=false;editorOpen=!editorOpen}
    Connections {target:editor;function onGeometryChanged(){workspace.editorOpen=false;workspace.searchOpen=false;workspace.legacyOpen=false}}

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ToolBar {
            objectName: "storageToolbar"
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                spacing: workspace.mobileMode ? 0 : 6
                ToolButton {
                    objectName: "importButton"
                    Layout.fillWidth: workspace.mobileMode
                    Layout.minimumWidth: workspace.mobileMode ? 0 : implicitWidth
                    text: workspace.mobileMode ? "가져오기" : "열기"
                    font.pixelSize: workspace.mobileMode ? 11 : 13
                    onClicked: workspace.openRequested()
                }
                ToolButton {
                    objectName: "deviceSaveButton"
                    Layout.fillWidth: workspace.mobileMode
                    Layout.minimumWidth: workspace.mobileMode ? 0 : implicitWidth
                    text: workspace.mobileMode ? "기기에 저장" : "저장"
                    font.pixelSize: workspace.mobileMode ? 11 : 13
                    onClicked: workspace.saveRequested()
                }
                ToolButton {
                    objectName: workspace.mobileMode ? "exportButton" : "saveAsButton"
                    Layout.fillWidth: workspace.mobileMode
                    Layout.minimumWidth: workspace.mobileMode ? 0 : implicitWidth
                    text: workspace.mobileMode ? "내보내기" : "다른 이름"
                    font.pixelSize: workspace.mobileMode ? 11 : 13
                    onClicked: workspace.saveAsRequested()
                }
                Item { visible: !workspace.mobileMode; Layout.fillWidth: visible }
                ToolButton {
                    objectName: "undoButton"
                    Layout.fillWidth: workspace.mobileMode
                    Layout.minimumWidth: workspace.mobileMode ? 0 : implicitWidth
                    text: "취소"
                    font.pixelSize: workspace.mobileMode ? 11 : 13
                    Accessible.name: "실행 취소"; enabled: editor.canUndo; onClicked: editor.undo()
                }
                ToolButton {
                    objectName: "redoButton"
                    Layout.fillWidth: workspace.mobileMode
                    Layout.minimumWidth: workspace.mobileMode ? 0 : implicitWidth
                    text: "다시"
                    font.pixelSize: workspace.mobileMode ? 11 : 13
                    Accessible.name: "다시 실행"; enabled: editor.canRedo; onClicked: editor.redo()
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                padding: 8
                text: editor.fileName + (editor.dirty ? " · 저장하지 않은 변경" : " · 저장됨")
                elide: Text.ElideMiddle
                color: "#526377"
            }
            ToolButton {
                objectName: "webImportButton"
                text: "웹 프로젝트 가져오기"
                font.pixelSize: workspace.compact ? 11 : 13
                focusPolicy: Qt.NoFocus
                onClicked: workspace.webImportRequested()
            }
        }
        Flow {
            Layout.fillWidth:true
            Layout.preferredHeight:childrenRect.height
            spacing:4
            ToolButton { objectName:"searchTab";text:"검색";focusPolicy:Qt.NoFocus;onPressed:workspace.navigationPointer(true);onReleased:workspace.navigationPointer(false);onCanceled:workspace.navigationPointer(false);onClicked:{workspace.navigationStarted();workspace.searchOpen=true;workspace.editorOpen=false;workspace.legacyOpen=false} }
            ToolButton { objectName:"mapDisplayButton";text:"지도 표시";focusPolicy:Qt.NoFocus;onClicked:{workspace.navigationStarted();displayControls.open()} }
            ToolButton { objectName:"openObjectEditor";text:"편집";enabled:editor.selectionItems.length>0;focusPolicy:Qt.NoFocus;onClicked:workspace.toggleEditor() }
            ToolButton { objectName:"legacyPanelButton";text:workspace.compact?"레이어":"Qt 레이어·기존 속성";font.pixelSize:11;focusPolicy:Qt.NoFocus;onClicked:{workspace.navigationStarted();workspace.legacyOpen=!workspace.legacyOpen;workspace.editorOpen=false;workspace.searchOpen=false;panel.showCountryControls()} }
            ToolButton { objectName:"contentPanelButton";text:"지명·수계";font.pixelSize:11;focusPolicy:Qt.NoFocus;onClicked:{workspace.navigationStarted();workspace.legacyOpen=true;workspace.editorOpen=false;workspace.searchOpen=false;panel.showContent()} }
            ToolButton { objectName:"historicalLibraryButton";text:workspace.compact?"역사":"역사 라이브러리";font.pixelSize:11;focusPolicy:Qt.NoFocus;onClicked:workspace.historicalLibraryRequested() }
            ToolButton { objectName:"gisImportButton";text:"GIS";font.pixelSize:11;focusPolicy:Qt.NoFocus;onClicked:workspace.gisImportRequested() }
            ToolButton { objectName:"gisExportButton";text:"GIS 내보내기";font.pixelSize:11;focusPolicy:Qt.NoFocus;onClicked:workspace.gisExportRequested() }
            ToolButton { objectName:"closeSidePanel";text:"닫기";visible:workspace.sideOpen;focusPolicy:Qt.NoFocus;onClicked:{workspace.navigationStarted();workspace.searchOpen=false;workspace.editorOpen=false;workspace.legacyOpen=false} }
        }
        Item {
            id:body
            Layout.fillWidth:true;Layout.fillHeight:true
            Common.MapView {
                id:mapView
                objectName:"mapView"
                anchors.left:parent.left;anchors.top:parent.top
                anchors.right:workspace.sideOpen&&!workspace.compact?side.left:parent.right
                anchors.bottom:workspace.sideOpen&&workspace.compact?side.top:parent.bottom
                onSelectionNavigationStarted:workspace.navigationStarted()
                onSelectionPointerChanged:function(down){workspace.navigationPointer(down)}
                controlsTopMargin:toolbar.visible?64:12
            }
            Common.TerritorialSelectionToolbar {
                id:toolbar
                anchors.horizontalCenter:mapView.horizontalCenter;anchors.top:mapView.top;anchors.topMargin:8
                width:Math.min(540,Math.max(0,mapView.width-16));height:implicitHeight
                editorOpen:workspace.editorOpen
                holdFieldCommits:workspace.holdFieldCommits||workspace.searchOpen||editor.objectChooserOpen
                onToggleEditor:workspace.toggleEditor()
                z:20
            }
            Item {
                id:side
                anchors.right:parent.right;anchors.bottom:parent.bottom
                width:workspace.compact?parent.width:320
                height:workspace.compact?Math.min(340,parent.height*.52):parent.height
                visible:workspace.sideOpen
                Common.EditorPanel {
                    id:panel;objectName:"editorPanel";anchors.fill:parent;compact:workspace.compact
                    visible:workspace.legacyOpen;holdFieldCommits:workspace.holdFieldCommits
                }
                Common.ObjectPropertyPanel {
                    id:properties;anchors.fill:parent;visible:workspace.editorOpen;holdFieldCommits:workspace.holdFieldCommits
                    onCloseRequested:workspace.editorOpen=false
                }
                Rectangle {
                    anchors.fill:parent;color:"white";visible:workspace.searchOpen
                    Common.ObjectSearch {
                        anchors.fill:parent
                        onNavigationStarted:workspace.navigationStarted()
                        onSingleSelected:workspace.searchOpen=false
                    }
                }
            }
        }
    }
}
