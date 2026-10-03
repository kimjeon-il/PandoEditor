import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../common" as Common
import "../common/UiTokens.js" as Tokens

Item {
    id:workspace
    objectName:"desktopWorkspace"
    property bool compact:width<800
    property bool mobileMode:false
    property bool holdFieldCommits:false
    readonly property var colors:Tokens.colors(editor.appearancePreferences)
    signal webImportRequested()
    signal historicalLibraryRequested()
    signal gisImportRequested()
    signal gisExportRequested()
    signal projectGpkgExportRequested()
    signal openRequested()
    signal saveRequested()
    signal saveAsRequested()
    signal preferencesRequested()
    property bool editorOpen:false
    property bool searchOpen:false
    property bool legacyOpen:false
    readonly property bool sideOpen:editorOpen||legacyOpen
    property bool pointerNavigation:false
    function navigationStarted(){
        if(pointerNavigation)return
        toolbar.navigationStarted();properties.navigationStarted();if(panel.item)panel.item.beginSelectionNavigation()
    }
    function navigationPointer(down){
        if(down){pointerNavigation=true;toolbar.navigating=true;properties.navigating=true;if(panel.item)panel.item.selectionNavigation=true}
        else Qt.callLater(function(){workspace.pointerNavigation=false;toolbar.navigating=false;properties.navigating=false;if(panel.item)panel.item.selectionNavigation=false})
    }
    function dismissPopup(){
        if(fileMenu.visible){fileMenu.close();return true}
        if(createMenu.visible){createMenu.close();return true}
        if(displayControls.visible){displayControls.close();return true}
        if(toolbar.dismissPopup()||mapView.dismissPopup()||properties.dismissPopup()||(panel.item&&panel.item.dismissPopup()))return true
        if(searchOpen||sideOpen){navigationStarted();searchOpen=false;editorOpen=false;legacyOpen=false;return true}
        return false
    }
    function toggleEditor(){navigationStarted();searchOpen=false;legacyOpen=false;editorOpen=!editorOpen}
    function showLegacy(content){
        navigationStarted();legacyOpen=true;editorOpen=false;searchOpen=false
        panel.active=true
        if(content)panel.item.showContent();else panel.item.showCountryControls()
    }
    Connections {
        target:editor
        function onGeometryEditChanged(){
            if(editor.geometryEditState.active===true){workspace.navigationStarted();workspace.editorOpen=false;workspace.legacyOpen=false;workspace.searchOpen=false}
        }
        function onStructureChanged(){
            // Structure actions can originate on the map toolbar before the
            // optional forms have ever been opened; their dialog lives there.
            if(editor.structureDialogOpen)panel.active=true
        }
    }
    Common.MapDisplayControls {id:displayControls;parent:workspace;y:topbar.height+4}
    Rectangle {
        id:topbar;objectName:"storageToolbar"
        anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top
        height:48;color:workspace.colors.panel
        Rectangle {anchors.bottom:parent.bottom;width:parent.width;height:1;color:workspace.colors.border}
        RowLayout {
            anchors.fill:parent;anchors.leftMargin:8;anchors.rightMargin:8;spacing:2
            Common.UiButton {objectName:"undoButton";symbol:"undo";ToolTip.text:"실행 취소";Accessible.name:"실행 취소";enabled:editor.canUndo;onClicked:editor.undo()}
            Common.UiButton {objectName:"redoButton";symbol:"redo";ToolTip.text:"다시 실행";Accessible.name:"다시 실행";enabled:editor.canRedo;onClicked:editor.redo()}
            Common.UiButton {
                id:fileTrigger;objectName:"fileMenuButton";text:"파일";selected:fileMenu.visible;focusPolicy:Qt.NoFocus
                onPressed:workspace.navigationPointer(true);onReleased:workspace.navigationPointer(false);onCanceled:workspace.navigationPointer(false)
                onClicked:fileMenu.open()
            }
            Common.UiButton {objectName:"mapDisplayButton";text:"보기";selected:displayControls.visible;focusPolicy:Qt.NoFocus;onClicked:{workspace.navigationStarted();displayControls.open()}}
            Common.UiButton {objectName:"preferencesButton";text:"설정";focusPolicy:Qt.NoFocus;onClicked:workspace.preferencesRequested()}
            Common.UiButton {text:"도움말";focusPolicy:Qt.NoFocus;onClicked:documentInfo.open()}
            Item {Layout.fillWidth:true}
            Label {visible:!workspace.compact;text:editor.fileName;elide:Text.ElideMiddle;color:workspace.colors.muted;font.pixelSize:12;Layout.maximumWidth:240}
        }
    }
    Popup {
        id:fileMenu;objectName:"fileMenu";parent:workspace
        x:Math.min(fileTrigger.x,Math.max(8,workspace.width-width-8));y:topbar.height+4
        width:Math.min(280,workspace.width-16);padding:6;focus:true
        closePolicy:Popup.CloseOnEscape|Popup.CloseOnPressOutside
        background:Rectangle {color:workspace.colors.panel;border.color:workspace.colors.border;radius:9}
        contentItem:ColumnLayout {
            spacing:2
            Common.UiButton {menuItem:true;objectName:"importButton";text:workspace.mobileMode?"가져오기…":"열기…";Layout.fillWidth:true;onClicked:{workspace.openRequested();fileMenu.close()}}
            Common.UiButton {menuItem:true;objectName:"deviceSaveButton";text:workspace.mobileMode?"기기에 저장":"저장";Layout.fillWidth:true;onClicked:{workspace.saveRequested();fileMenu.close()}}
            Common.UiButton {menuItem:true;objectName:workspace.mobileMode?"exportButton":"saveAsButton";text:workspace.mobileMode?"내보내기…":"다른 이름으로 저장…";Layout.fillWidth:true;onClicked:{workspace.saveAsRequested();fileMenu.close()}}
            Common.UiButton {menuItem:true;objectName:"webImportButton";text:"웹 프로젝트 가져오기…";Layout.fillWidth:true;onClicked:{workspace.webImportRequested();fileMenu.close()}}
            Rectangle {Layout.fillWidth:true;height:1;color:workspace.colors.border}
            Common.UiButton {menuItem:true;objectName:"gisImportButton";text:"GIS 가져오기…";Layout.fillWidth:true;onClicked:{workspace.gisImportRequested();fileMenu.close()}}
            Common.UiButton {menuItem:true;objectName:"gisExportButton";text:"GIS 내보내기…";Layout.fillWidth:true;onClicked:{workspace.gisExportRequested();fileMenu.close()}}
            Common.UiButton {menuItem:true;objectName:"projectGpkgExportButton";text:"프로젝트 GeoPackage…";Layout.fillWidth:true;onClicked:{workspace.projectGpkgExportRequested();fileMenu.close()}}
            Rectangle {Layout.fillWidth:true;height:1;color:workspace.colors.border}
            Common.UiButton {menuItem:true;objectName:"legacyPanelButton";text:"Qt 레이어·기존 속성";Layout.fillWidth:true;onClicked:{workspace.showLegacy(false);fileMenu.close()}}
        }
    }
    Popup {
        id:createMenu;objectName:"createMenu";parent:workspace
        width:Math.min(280,workspace.width-16);x:Math.max(8,(workspace.width-width)/2)
        y:Math.max(topbar.height+8,commandBar.mapToItem(workspace,0,0).y-height-8)
        padding:6;focus:true;closePolicy:Popup.CloseOnEscape|Popup.CloseOnPressOutside
        background:Rectangle {color:workspace.colors.panel;border.color:workspace.colors.border;radius:9}
        contentItem:ColumnLayout {
            spacing:2
            Common.UiButton {menuItem:true;text:"국가 추가";Layout.fillWidth:true;onClicked:{editor.beginTerritorialCreate("country");createMenu.close()}}
            Common.UiButton {menuItem:true;text:"하위단위 추가";Layout.fillWidth:true;onClicked:{editor.beginTerritorialCreate("subunit");createMenu.close()}}
            Common.UiButton {menuItem:true;text:"지방 추가";Layout.fillWidth:true;onClicked:{editor.beginTerritorialCreate("region");createMenu.close()}}
            Rectangle {Layout.fillWidth:true;height:1;color:workspace.colors.border}
            Common.UiButton {menuItem:true;objectName:"contentPanelButton";text:"지명·수계·분포";Layout.fillWidth:true;onClicked:{workspace.showLegacy(true);createMenu.close()}}
            Common.UiButton {menuItem:true;objectName:"historicalLibraryButton";text:"역사 라이브러리…";Layout.fillWidth:true;onClicked:{workspace.historicalLibraryRequested();createMenu.close()}}
        }
    }
    Popup {
        id:documentInfo;parent:workspace;x:8;y:topbar.height+4;width:Math.min(360,workspace.width-16);padding:16
        background:Rectangle {color:workspace.colors.panel;border.color:workspace.colors.border;radius:9}
        contentItem:Label {text:editor.documentNotice;wrapMode:Text.Wrap;color:workspace.colors.text}
    }
    Item {
        id:body;anchors.left:parent.left;anchors.right:parent.right;anchors.top:topbar.bottom;anchors.bottom:parent.bottom
        Common.MapView {
            id:mapView;objectName:"mapView"
            anchors.left:workspace.sideOpen&&!workspace.compact?side.right:parent.left
            anchors.top:parent.top;anchors.right:parent.right
            anchors.bottom:workspace.sideOpen&&workspace.compact?side.top:parent.bottom
            externalCommandBar:true
            onSelectionNavigationStarted:workspace.navigationStarted()
            onSelectionPointerChanged:function(down){workspace.navigationPointer(down)}
        }
        Item {
            id:side;objectName:"workspaceSidePanel"
            anchors.left:parent.left;anchors.bottom:parent.bottom
            width:workspace.compact?parent.width:320
            height:workspace.compact?Math.min(360,parent.height*.52):parent.height
            visible:workspace.sideOpen;z:30
            // Instantiate the expensive, optional forms only on first use. Keep
            // the instance afterwards so closing the panel preserves its drafts.
            Loader {
                id:panel;anchors.fill:parent;active:false;visible:workspace.legacyOpen
                sourceComponent:Common.EditorPanel {objectName:"editorPanel";compact:workspace.compact;holdFieldCommits:workspace.holdFieldCommits}
            }
            Common.ObjectPropertyPanel {
                id:properties;anchors.fill:parent;visible:workspace.editorOpen;compact:workspace.compact;holdFieldCommits:workspace.holdFieldCommits||workspace.searchOpen||editor.objectChooserOpen||fileMenu.visible||createMenu.visible
                onCloseRequested:workspace.editorOpen=false
            }
            Common.UiButton {objectName:"closeSidePanel";symbol:"close";ToolTip.text:"닫기";visible:workspace.legacyOpen;anchors.right:parent.right;anchors.top:parent.top;onClicked:{workspace.navigationStarted();workspace.legacyOpen=false}}
        }
        Common.TerritorialSelectionToolbar {
            id:toolbar
            visible:editor.selectionItems.length===1&&!editor.geometryEditState.active
            parent:workspace.editorOpen?properties.selectionHeader:mapView
            width:workspace.editorOpen?parent.width:Math.min(workspace.compact?336:378,Math.max(0,parent.width-24))
            height:implicitHeight
            x:workspace.editorOpen?0:Math.max(12,Math.min(anchorPoint.x-width/2,parent.width-width-12))
            y:workspace.editorOpen?0:Math.max(12,Math.min(anchorPoint.y-height-18,parent.height-height-76))
            readonly property point anchorPoint:{
                if(!visible)return Qt.point(mapView.width/2,mapView.height/2)
                const key=editor.primaryObject.key
                const labels=editor.placedLabels
                for(let i=0;i<labels.length;i++)if(labels[i].ref.key===key)return Qt.point(labels[i].x,labels[i].y)
                return Qt.point(mapView.width/2,mapView.height/2)
            }
            editorOpen:workspace.editorOpen
            holdFieldCommits:workspace.holdFieldCommits||workspace.searchOpen||editor.objectChooserOpen||fileMenu.visible||createMenu.visible
            onToggleEditor:workspace.toggleEditor()
            z:25
        }
        Rectangle {
            id:commandBar;objectName:"mapCommandToolbar"
            anchors.horizontalCenter:mapView.horizontalCenter;anchors.bottom:mapView.bottom;anchors.bottomMargin:12
            width:commands.implicitWidth+8;height:commands.implicitHeight+8
            radius:9;color:workspace.colors.panel;border.color:workspace.colors.border;z:24
            visible:!editor.geometryEditState.active
            RowLayout {
                id:commands;anchors.centerIn:parent;spacing:2
                Common.UiButton {objectName:"createMenuButton";symbol:"plus";ToolTip.text:"추가";selected:createMenu.visible;onClicked:{workspace.navigationStarted();createMenu.open()}}
                Common.UiButton {symbol:"image";ToolTip.text:"이미지 추가";onClicked:mapView.showReferenceImages(commandBar)}
                Common.UiButton {
                    objectName:"searchTab";symbol:"search";ToolTip.text:"검색";selected:workspace.searchOpen;focusPolicy:Qt.NoFocus
                    onPressed:workspace.navigationPointer(true);onReleased:workspace.navigationPointer(false);onCanceled:workspace.navigationPointer(false)
                    onClicked:{workspace.navigationStarted();workspace.searchOpen=!workspace.searchOpen;if(workspace.searchOpen&&workspace.compact){workspace.editorOpen=false;workspace.legacyOpen=false}}
                }
                Common.UiButton {objectName:"resetViewButton";symbol:"focus";ToolTip.text:"전체 지도 보기";onClicked:mapView.fit()}
                Common.UiButton {objectName:"openObjectEditor";symbol:"edit";ToolTip.text:"편집";enabled:editor.selectionItems.length>0;selected:workspace.editorOpen;focusPolicy:Qt.NoFocus;onClicked:workspace.toggleEditor()}
            }
        }
        Rectangle {
            objectName:"objectSearchSurface"
            width:Math.min(340,mapView.width-16);height:Math.min(420,Math.max(80,mapView.height-80))
            anchors.horizontalCenter:mapView.horizontalCenter;anchors.bottom:commandBar.top;anchors.bottomMargin:8
            visible:workspace.searchOpen;z:32;radius:9;color:workspace.colors.panel;border.color:workspace.colors.border
            Common.ObjectSearch {anchors.fill:parent;onNavigationStarted:workspace.navigationStarted();onSingleSelected:workspace.searchOpen=false}
        }
    }
}
