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
    signal newProjectRequested()
    signal openRequested()
    signal saveRequested()
    signal saveAsRequested()
    signal preferencesRequested()
    property bool editorOpen:false
    property bool searchOpen:false
    property int sheetLevel:1
    property int searchSheetLevel:1
    property real searchDragHeight:-1
    property bool suppressDirectEntry:false
    property var presentedSelectionRevision:-1
    Component.onCompleted:presentedSelectionRevision=editor.selectionRevision
    property var hoverLabel:({})
    readonly property var hoverData:{const key=hoverLabel.ref?hoverLabel.ref.key:"";const rows=editor.objectRows;for(let i=0;i<rows.length;i++)if(rows[i].key===key)return rows[i];return ({})}
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
        if(documentInfo.visible){documentInfo.close();return true}
        if(fileMenu.visible){fileMenu.close();return true}
        if(createMenu.visible){createMenu.close();return true}
        if(displayControls.dismissPopup())return true
        if(toolbar.dismissPopup()||mapView.dismissPopup()||properties.dismissPopup()||(panel.item&&panel.item.dismissPopup()))return true
        if(searchOpen||sideOpen){navigationStarted();searchOpen=false;editorOpen=false;legacyOpen=false;return true}
        return false
    }
    function closePanels(){navigationStarted();editorOpen=false;legacyOpen=false;searchOpen=false}
    function openEditor(){navigationStarted();searchOpen=false;legacyOpen=false;hoverLabel=({});editorOpen=true}
    function activateHover(){if(!hoverData.key)return;suppressDirectEntry=true;editor.selectObject(hoverData);suppressDirectEntry=false}
    function toggleEditor(){navigationStarted();searchOpen=false;legacyOpen=false;editorOpen=!editorOpen}
    function createContent(domain,type){
        navigationStarted();legacyOpen=false;searchOpen=false;createMenu.close();editorOpen=true
        Qt.callLater(function(){properties.startContentCreation(domain,type)})
    }
    function showLegacy(content){
        navigationStarted();legacyOpen=true;editorOpen=false;searchOpen=false
        panel.active=true
        panel.item.showLayers()
    }
    Connections {
        target:editor
        function onSelectionChanged(){
            const changed=workspace.presentedSelectionRevision!==editor.selectionRevision;workspace.presentedSelectionRevision=editor.selectionRevision
            if(!changed||editor.geometryEditState.active)return
            if(editor.selectionItems.length===1&&!workspace.suppressDirectEntry)workspace.openEditor()
            else if(editor.selectionItems.length===0&&!editor.contentEditState.create)workspace.editorOpen=false
        }
        function onGeometryEditChanged(){
            if(editor.geometryEditState.active===true){workspace.navigationStarted();workspace.editorOpen=false;workspace.legacyOpen=false;workspace.searchOpen=false}
            else if(editor.selectionItems.length===1&&!workspace.suppressDirectEntry)workspace.openEditor()
        }
        function onStructureChanged(){
            // Structure actions can originate on the map toolbar before the
            // optional forms have ever been opened; their dialog lives there.
            if(editor.structureDialogOpen)panel.active=true
        }
    }
    Timer {id:hoverDismiss;interval:180;onTriggered:if(!toolbar.previewHover&&!toolbar.previewPopup&&!mapView.hoverLabel.ref)workspace.hoverLabel=({})}
    Common.MapDisplayControls {id:displayControls;parent:workspace;y:compactSheet?workspace.height-height:topbar.height+4;x:compactSheet?0:Math.min(viewTrigger.x,Math.max(8,workspace.width-width-8));onClosed:viewTrigger.forceActiveFocus()}
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
                onClicked:{createMenu.close();displayControls.close();fileMenu.open()}
            }
            Common.UiButton {id:viewTrigger;objectName:"mapDisplayButton";text:"보기";selected:displayControls.visible;focusPolicy:Qt.NoFocus;onClicked:{workspace.navigationStarted();fileMenu.close();createMenu.close();displayControls.open()}}
            Common.UiButton {objectName:"preferencesButton";text:"설정";focusPolicy:Qt.NoFocus;onClicked:workspace.preferencesRequested()}
            Common.UiButton {objectName:"helpButton";text:"도움말";focusPolicy:Qt.NoFocus;onClicked:documentInfo.open()}
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
        height:Math.min(fileItems.implicitHeight+12,workspace.height-topbar.height-16)
        contentItem:ScrollView {
            id:fileScroll;clip:true;contentWidth:availableWidth
            ColumnLayout {
                id:fileItems;width:fileScroll.availableWidth
            spacing:2
            Common.UiButton {menuItem:true;objectName:"newProjectButton";symbol:"plus";text:"새 프로젝트";Layout.fillWidth:true;onClicked:{workspace.navigationStarted();workspace.newProjectRequested();fileMenu.close()}}
            Common.UiButton {menuItem:true;objectName:"importButton";symbol:"folder";text:workspace.mobileMode?"가져오기…":"열기…";Layout.fillWidth:true;onClicked:{workspace.openRequested();fileMenu.close()}}
            Common.UiButton {menuItem:true;objectName:"deviceSaveButton";symbol:"save";text:workspace.mobileMode?"기기에 저장":"저장";Layout.fillWidth:true;onClicked:{workspace.saveRequested();fileMenu.close()}}
            Common.UiButton {menuItem:true;objectName:workspace.mobileMode?"exportButton":"saveAsButton";symbol:"save";text:workspace.mobileMode?"내보내기…":"다른 이름으로 저장…";Layout.fillWidth:true;onClicked:{workspace.saveAsRequested();fileMenu.close()}}
            Common.UiButton {menuItem:true;objectName:"webImportButton";symbol:"import";text:"웹 프로젝트 가져오기…";Layout.fillWidth:true;onClicked:{workspace.webImportRequested();fileMenu.close()}}
            Rectangle {Layout.fillWidth:true;height:1;color:workspace.colors.border}
            Common.UiButton {menuItem:true;objectName:"gisImportButton";symbol:"import";text:"GIS 가져오기…";Layout.fillWidth:true;onClicked:{workspace.gisImportRequested();fileMenu.close()}}
            Common.UiButton {menuItem:true;objectName:"gisExportButton";symbol:"export";text:"GIS 내보내기…";Layout.fillWidth:true;onClicked:{workspace.gisExportRequested();fileMenu.close()}}
            Common.UiButton {menuItem:true;objectName:"projectGpkgExportButton";symbol:"save";text:"프로젝트 GeoPackage…";Layout.fillWidth:true;onClicked:{workspace.projectGpkgExportRequested();fileMenu.close()}}
            Rectangle {Layout.fillWidth:true;height:1;color:workspace.colors.border}
            Common.UiButton {menuItem:true;objectName:"legacyPanelButton";symbol:"edit";text:"객체 상세 편집";Layout.fillWidth:true;onClicked:{workspace.navigationStarted();workspace.legacyOpen=false;workspace.editorOpen=true;workspace.searchOpen=false;fileMenu.close()}}
            Common.UiButton {menuItem:true;objectName:"layerManagementButton";symbol:"notes";text:"레이어 관리…";Layout.fillWidth:true;onClicked:{workspace.showLegacy(false);fileMenu.close()}}
        }
        }
    }
    Popup {
        id:createMenu;objectName:"createMenu";parent:workspace
        property int sheetLevel:1
        property real dragHeight:-1
        width:workspace.compact?workspace.width:Math.min(280,workspace.width-16);x:workspace.compact?0:Math.max(8,(workspace.width-width)/2)
        y:workspace.compact?workspace.height-height:Math.max(topbar.height+8,commandBar.mapToItem(workspace,0,0).y-height-8)
        padding:6;focus:true;closePolicy:Popup.CloseOnEscape|Popup.CloseOnPressOutside
        background:Rectangle {color:workspace.colors.panel;border.color:workspace.colors.border;radius:9}
        height:workspace.compact?(dragHeight>=0?dragHeight:[84,workspace.height*.48,workspace.height*.86][sheetLevel]):Math.min(createItems.implicitHeight+12,workspace.height-topbar.height-16)
        Behavior on height {enabled:!createHandle.dragging;NumberAnimation {duration:170;easing.type:Easing.BezierSpline;easing.bezierCurve:[.25,.1,.25,1,1,1]}}
        contentItem:ColumnLayout {spacing:4
            Common.UiSheetHandle {id:createHandle;objectName:"createSheetHandle";snapHeights:[84,workspace.height*.48,workspace.height*.86];panelHeight:createMenu.height;onPanelHeightRequested:value=>createMenu.dragHeight=value;visible:workspace.compact;Layout.fillWidth:true;level:createMenu.sheetLevel;onHeightChangedByUser:value=>createMenu.sheetLevel=value}
            Label {visible:workspace.compact;text:"추가";font.weight:Font.DemiBold;Layout.fillWidth:true}
            ScrollView {Layout.fillWidth:true;Layout.fillHeight:true;visible:!workspace.compact||createMenu.sheetLevel>0;
            id:createScroll;clip:true;contentWidth:availableWidth
            ColumnLayout {
                id:createItems;width:createScroll.availableWidth
            spacing:2
            Common.UiButton {menuItem:true;symbol:"country";text:"국가";Layout.fillWidth:true;onClicked:{editor.beginTerritorialCreate("country");createMenu.close()}}
            Common.UiButton {menuItem:true;symbol:"subunit";text:"하위단위";Layout.fillWidth:true;onClicked:{editor.beginTerritorialCreate("subunit");createMenu.close()}}
            Common.UiButton {menuItem:true;symbol:"region";text:"지방";Layout.fillWidth:true;onClicked:{editor.beginTerritorialCreate("region");createMenu.close()}}
            Rectangle {Layout.fillWidth:true;height:1;color:workspace.colors.border}
            Common.UiButton {menuItem:true;objectName:"addDistributionLayer";symbol:"distribution";text:"분포";Layout.fillWidth:true;onClicked:workspace.createContent("distributionLayer","")}
            Common.UiButton {menuItem:true;objectName:"addDistributionEntry";symbol:"distribution";text:"분포 항목";Layout.fillWidth:true;onClicked:workspace.createContent("distributionEntry","")}
            Rectangle {Layout.fillWidth:true;height:1;color:workspace.colors.border}
            Common.UiButton {menuItem:true;objectName:"addPlaceLabel";symbol:"place";text:"지명";Layout.fillWidth:true;onClicked:workspace.createContent("label","custom")}
            Common.UiButton {menuItem:true;objectName:"addRiver";symbol:"river";text:"강";Layout.fillWidth:true;onClicked:workspace.createContent("hydro","river")}
            Common.UiButton {menuItem:true;objectName:"addLake";symbol:"lake";text:"호수";Layout.fillWidth:true;onClicked:workspace.createContent("hydro","lake")}
            Common.UiButton {menuItem:true;objectName:"contentPanelButton";symbol:"edit";text:"선택 객체 상세 편집";Layout.fillWidth:true;enabled:editor.selectionItems.length===1;onClicked:{workspace.navigationStarted();workspace.legacyOpen=false;workspace.searchOpen=false;workspace.editorOpen=true;createMenu.close()}}
            Rectangle {Layout.fillWidth:true;height:1;color:workspace.colors.border}
            Common.UiButton {menuItem:true;objectName:"historicalLibraryButton";symbol:"library";text:"라이브러리…";Layout.fillWidth:true;onClicked:{workspace.historicalLibraryRequested();createMenu.close()}}
        }
        }
        }
    }
    Popup {
        id:documentInfo;objectName:"helpPopup";parent:workspace;x:8;y:topbar.height+4;width:Math.min(360,workspace.width-16);padding:16
        background:Rectangle {color:workspace.colors.panel;border.color:workspace.colors.border;radius:9}
                height:Math.min(410,workspace.height-topbar.height-16)
        focus:true;closePolicy:Popup.CloseOnEscape|Popup.CloseOnPressOutside
        contentItem:ScrollView {
            contentWidth:availableWidth;clip:true
            Label {width:parent.width;textFormat:Text.PlainText;wrapMode:Text.Wrap;color:workspace.colors.text;text:"조작 안내\n\n선택 · 지도의 객체를 클릭합니다. Ctrl/⌘를 누르면 여러 객체를 선택합니다.\n\n이동 · 빈 지도를 끌어 이동하고 휠이나 두 손가락으로 확대합니다. 전체 지도 버튼으로 화면을 맞춥니다.\n\n편집 · 선택 카드 전체를 누르면 정보·편집·관계 탭이 열립니다. 이름·메모는 입력을 마치면 반영됩니다.\n\n도형 · 편집 탭에서 작업을 고릅니다. 점을 끌거나 변을 두 번 눌러 점을 추가한 뒤 미리보기와 확정으로 완료합니다. 접기는 초안을 유지하고 이전은 이전 단계로 돌아갑니다.\n\n작은 창 · 패널 위 손잡이를 끌거나 눌러 세 단계 높이를 전환합니다.\n\n검색 · 아래 검색 버튼을 누릅니다. ↓로 결과에 이동하고 Enter로 선택합니다.\n\n저장 · 파일 메뉴에서 저장합니다. Ctrl+Z / Ctrl+Shift+Z로 실행 취소·다시 실행합니다. Esc/뒤로는 열린 화면 또는 현재 작업 단계부터 닫습니다."}
        }
    }
    Item {
        id:body;anchors.left:parent.left;anchors.right:parent.right;anchors.top:topbar.bottom;anchors.bottom:parent.bottom
        Common.MapView {
            id:mapView;objectName:"mapView"
            anchors.left:parent.left
            anchors.top:parent.top;anchors.right:parent.right
            anchors.bottom:parent.bottom
            externalCommandBar:true
            hoverPreviewVisible:toolbar.visible&&!workspace.editorOpen;hoverPreviewKey:workspace.hoverData.key||""
            onHoverLabelChanged:{if(hoverLabel.ref){hoverDismiss.stop();workspace.hoverLabel=hoverLabel}else hoverDismiss.restart()}
            onObjectActivated:if(editor.selectionItems.length===1&&!workspace.suppressDirectEntry)workspace.openEditor()
            onSelectionNavigationStarted:workspace.navigationStarted()
            onSelectionPointerChanged:function(down){workspace.navigationPointer(down)}
            onSelectionModifiersChanged:additive=>workspace.suppressDirectEntry=additive
        }
        Item {
            id:side;objectName:"workspaceSidePanel"
            anchors.left:parent.left;anchors.bottom:parent.bottom
            width:workspace.compact?parent.width:320
            property real dragHeight:-1
            readonly property var snapHeights:[84,parent.height*(workspace.legacyOpen ? .52 : .48),parent.height*(workspace.legacyOpen ? .88 : .86)]
            height:workspace.compact?(dragHeight>=0?dragHeight:snapHeights[workspace.sheetLevel]):parent.height
            clip:true;Behavior on height {enabled:!workspaceHandle.dragging;NumberAnimation {duration:170;easing.type:Easing.BezierSpline;easing.bezierCurve:[.25,.1,.25,1,1,1]}}
            visible:workspace.sideOpen;z:30
            MouseArea {anchors.fill:parent;acceptedButtons:Qt.AllButtons;onWheel:function(wheel){wheel.accepted=true}}
            // Instantiate the expensive, optional forms only on first use. Keep
            // the instance afterwards so closing the panel preserves its drafts.
            Loader {
                id:panel;anchors.fill:parent;anchors.topMargin:workspace.compact?28:0;active:false;visible:workspace.legacyOpen
                sourceComponent:Common.EditorPanel {objectName:"editorPanel";mode:"layers";compact:workspace.compact;holdFieldCommits:workspace.holdFieldCommits}
            }
            Common.ObjectPropertyPanel {
                id:properties;anchors.fill:parent;anchors.topMargin:workspace.compact?28:0;visible:workspace.editorOpen;compact:workspace.compact;collapsed:workspace.compact&&workspace.sheetLevel===0;holdFieldCommits:workspace.holdFieldCommits||workspace.searchOpen||editor.objectChooserOpen||fileMenu.visible||createMenu.visible
                onCloseRequested:workspace.editorOpen=false
            }
            Common.UiSheetHandle {id:workspaceHandle;objectName:"workspaceSheetHandle";snapHeights:side.snapHeights;panelHeight:side.height;onPanelHeightRequested:value=>side.dragHeight=value;anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;visible:workspace.compact;level:workspace.sheetLevel;onHeightChangedByUser:value=>workspace.sheetLevel=value}
            Common.UiButton {objectName:"closeSidePanel";symbol:"close";ToolTip.text:"닫기";visible:workspace.legacyOpen;anchors.right:parent.right;anchors.top:parent.top;onClicked:{workspace.navigationStarted();workspace.legacyOpen=false}}
        }
        Common.TerritorialSelectionToolbar {
            id:toolbar
            visible:!editor.geometryEditState.active && (workspace.editorOpen?editor.selectionItems.length===1:!workspace.mobileMode&&workspace.width>=600&&!!workspace.hoverData.key)
            parent:workspace.editorOpen?properties.selectionHeader:mapView
            width:workspace.editorOpen?parent.width:Math.min(workspace.compact?336:378,Math.max(0,parent.width-24))
            height:implicitHeight
            x:workspace.editorOpen?0:Math.max(12,Math.min(anchorPoint.x-width/2,parent.width-width-12))
            y:workspace.editorOpen?0:Math.max(12,Math.min(anchorPoint.y-height-18,parent.height-height-76))
            previewData:workspace.hoverData
            onActivatePreview:workspace.activateHover()
            onFlagRequested:trigger=>properties.showFlagMenu(trigger)
            onPreviewPopupChanged:if(!previewPopup&&!mapView.hoverLabel.ref)hoverDismiss.restart()
            onPreviewHoverChanged:if(previewHover)hoverDismiss.stop();else if(!mapView.hoverLabel.ref)hoverDismiss.restart()
            readonly property point anchorPoint:{
                if(!workspace.editorOpen)return mapView.hoverLabelPoint
                if(!visible)return Qt.point(mapView.width/2,mapView.height/2)
                const key=editor.primaryObject.key
                const labels=editor.placedLabels
                for(let i=0;i<labels.length;i++)if(labels[i].ref.key===key)return Qt.point(labels[i].x,labels[i].y)
                return Qt.point(mapView.width/2,mapView.height/2)
            }
            editorOpen:workspace.editorOpen
            collapsed:workspace.compact&&workspace.sheetLevel===0&&workspace.editorOpen
            holdFieldCommits:workspace.holdFieldCommits||workspace.searchOpen||editor.objectChooserOpen||fileMenu.visible||createMenu.visible
            onToggleEditor:workspace.toggleEditor()
            z:25
        }
        Rectangle {
            id:commandBar;objectName:"mapCommandToolbar"
            anchors.horizontalCenter:mapView.horizontalCenter;anchors.horizontalCenterOffset:workspace.sideOpen&&!workspace.compact?Math.max(0,side.width+12-(mapView.width-width)/2):0;anchors.bottom:mapView.bottom;anchors.bottomMargin:workspace.compact&&workspace.searchOpen?searchSurface.height+12:workspace.compact&&workspace.sideOpen?side.height+12:12
            width:commands.implicitWidth+8;height:commands.implicitHeight+8
            radius:9;color:workspace.colors.panel;border.color:workspace.colors.border;z:24
            visible:!editor.geometryEditState.active
            RowLayout {
                id:commands;anchors.centerIn:parent;spacing:2
                Common.UiButton {objectName:"createMenuButton";symbol:"plus";ToolTip.text:"추가";selected:createMenu.visible;onClicked:{workspace.navigationStarted();fileMenu.close();displayControls.close();createMenu.open()}}
                Common.UiButton {symbol:"image";ToolTip.text:"이미지 추가";onClicked:mapView.showReferenceImages(commandBar)}
                Common.UiButton {
                    objectName:"searchTab";symbol:"search";ToolTip.text:"검색";selected:workspace.searchOpen;focusPolicy:Qt.NoFocus
                    onPressed:workspace.navigationPointer(true);onReleased:workspace.navigationPointer(false);onCanceled:workspace.navigationPointer(false)
                    onClicked:{workspace.navigationStarted();workspace.searchOpen=!workspace.searchOpen;if(workspace.searchOpen&&workspace.compact){workspace.editorOpen=false;workspace.legacyOpen=false}}
                }
                Common.UiButton {objectName:"resetViewButton";symbol:"focus";ToolTip.text:"전체 지도 보기";onClicked:mapView.fit()}
                Common.UiButton {objectName:"openObjectEditor";symbol:"edit";ToolTip.text:"편집";enabled:editor.selectionItems.length>0;selected:workspace.editorOpen;focusPolicy:Qt.NoFocus;onClicked:workspace.openEditor()}
            }
        }
        Rectangle {
            id:searchSurface;objectName:"objectSearchSurface"
            width:workspace.compact?mapView.width:Math.min(340,mapView.width-16);height:workspace.compact?(workspace.searchDragHeight>=0?workspace.searchDragHeight:[84,mapView.height*.48,mapView.height*.86][workspace.searchSheetLevel]):Math.min(420,Math.max(80,mapView.height-80));clip:true;Behavior on height {enabled:!searchHandle.dragging;NumberAnimation {duration:170;easing.type:Easing.BezierSpline;easing.bezierCurve:[.25,.1,.25,1,1,1]}}
            anchors.horizontalCenter:mapView.horizontalCenter;anchors.horizontalCenterOffset:workspace.sideOpen&&!workspace.compact?Math.max(0,side.width+12-(mapView.width-width)/2):0;anchors.bottom:workspace.compact?mapView.bottom:commandBar.top;anchors.bottomMargin:workspace.compact?0:8
            visible:workspace.searchOpen;z:32;radius:9;color:workspace.colors.panel;border.color:workspace.colors.border
            Common.UiSheetHandle {id:searchHandle;objectName:"searchSheetHandle";snapHeights:[84,mapView.height*.48,mapView.height*.86];panelHeight:searchSurface.height;onPanelHeightRequested:value=>workspace.searchDragHeight=value;visible:workspace.compact;anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;level:workspace.searchSheetLevel;onHeightChangedByUser:value=>workspace.searchSheetLevel=value}
            Common.ObjectSearch {anchors.fill:parent;anchors.topMargin:workspace.compact?28:0;collapsed:workspace.compact&&workspace.searchSheetLevel===0;onSelectionStarted:workspace.suppressDirectEntry=true;onSelectionFinished:workspace.suppressDirectEntry=false;onNavigationStarted:workspace.navigationStarted();onSingleSelected:{workspace.searchOpen=false;workspace.openEditor()}}
        }
    }
}
