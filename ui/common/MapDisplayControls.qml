import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "UiTokens.js" as Tokens

Popup {
    id: popup
    objectName: "mapDisplayPopup"
    property string section: ""
    property int sheetLevel:1
    property real dragHeight:-1
    readonly property bool compactSheet:editor.mobileMode||(parent&&parent.width<800)
    readonly property var colors: Tokens.colors(editor.appearancePreferences)
    readonly property var group: editor.presentationGroups.find(function(row){return row.key===popup.section}) || ({})
    readonly property string sectionTitle: section==="projection"?"투영법":section==="terrain"?"지형":section==="selection"?"선택 객체":group.title||""
    width: compactSheet?parent.width:Math.min(section===""?280:320, parent ? parent.width-16 : 320)
    height: compactSheet?(dragHeight>=0?dragHeight:[84,parent.height*.48,parent.height*.86][sheetLevel]):Math.min(menu.implicitHeight + topPadding + bottomPadding, parent ? parent.height-y-8 : 600)
    Behavior on height {enabled:!viewHandle.dragging;NumberAnimation {duration:170;easing.type:Easing.BezierSpline;easing.bezierCurve:[.25,.1,.25,1,1,1]}}
    x: 8
    y: 8
    padding: 8
    modal: false
    dim: false
    focus: true
    closePolicy: Popup.CloseOnPressOutside
    background: Rectangle {color:popup.colors.panel;border.color:popup.colors.border;radius:9}
    function showSection(key){section=key;Qt.callLater(function(){scroller.contentItem.contentY=0;backButton.forceActiveFocus()})}
    function dismissPopup(){
        if(!visible)return false
        if(section!==""){section="";Qt.callLater(function(){projectionTrigger.forceActiveFocus()});return true}
        close();return true
    }
    onOpened: {section="";Qt.callLater(function(){projectionTrigger.forceActiveFocus()})}
    contentItem:ColumnLayout {spacing:4
        UiSheetHandle {id:viewHandle;objectName:"viewSheetHandle";snapHeights:[84,parent?popup.parent.height*.48:300,parent?popup.parent.height*.86:500];panelHeight:popup.height;onPanelHeightRequested:value=>popup.dragHeight=value;visible:popup.compactSheet;Layout.fillWidth:true;level:popup.sheetLevel;onHeightChangedByUser:value=>popup.sheetLevel=value}
        Label {visible:popup.compactSheet&&popup.sheetLevel===0;text:"보기";font.weight:Font.DemiBold}
        ScrollView {Layout.fillWidth:true;Layout.fillHeight:true;visible:!popup.compactSheet||popup.sheetLevel>0;
        id:scroller
        clip:true
        contentWidth:availableWidth
        Keys.onEscapePressed: function(event){popup.dismissPopup();event.accepted=true}
        Keys.onLeftPressed: function(event){if(popup.section!==""){popup.dismissPopup();event.accepted=true}}
        ColumnLayout {
            id:menu
            width:scroller.availableWidth
            spacing:2
            ColumnLayout {
                id:rootMenu
                visible:popup.section===""
                Layout.fillWidth:true;spacing:2
                UiButton {id:projectionTrigger;objectName:"viewProjectionMenu";menuItem:true;symbol:"globe";text:"투영법  ›";Layout.fillWidth:true;Keys.onRightPressed:popup.showSection("projection");onClicked:popup.showSection("projection")}
                Rectangle {Layout.fillWidth:true;height:1;color:popup.colors.border}
                Repeater {
                    model:editor.presentationGroups
                    delegate:UiButton {
                        required property var modelData
                        objectName:"viewGroup_"+modelData.key
                        Layout.fillWidth:true;menuItem:true
                        symbol:({countries:"country",subunits:"subunit",regions:"region",labels:"place",rivers:"river",lakes:"lake",distributions:"distribution",genericFeatures:"type"})[modelData.key]||"eye"
                        text:modelData.title+"  ›"
                        Keys.onRightPressed:popup.showSection(modelData.key);onClicked:popup.showSection(modelData.key)
                    }
                }
                UiButton {objectName:"viewTerrainMenu";menuItem:true;symbol:"terrain";text:"지형  ›";Layout.fillWidth:true;Keys.onRightPressed:popup.showSection("terrain");onClicked:popup.showSection("terrain")}
                Rectangle {Layout.fillWidth:true;height:1;color:popup.colors.border}
                UiButton {objectName:"viewSelectionMenu";menuItem:true;symbol:"focus";text:"선택 객체  ›";Layout.fillWidth:true;enabled:editor.selectionItems.length>0;Keys.onRightPressed:popup.showSection("selection");onClicked:popup.showSection("selection")}
            }
            ColumnLayout {
                visible:popup.section!==""
                Layout.fillWidth:true;spacing:6
                UiButton {id:backButton;objectName:"viewMenuBack";menuItem:true;symbol:"back";text:popup.sectionTitle;Layout.fillWidth:true;onClicked:popup.dismissPopup()}
                Rectangle {Layout.fillWidth:true;height:1;color:popup.colors.border}
                ColumnLayout {
                    visible:popup.section==="projection";Layout.fillWidth:true
                    UiButton {objectName:"projectionGlobeButton";menuItem:true;symbol:"globe";text:"지구본";selected:editor.projectionMode==="globe";Layout.fillWidth:true;onClicked:editor.setProjectionMode("globe")}
                    UiButton {objectName:"projectionFlatButton";menuItem:true;symbol:"flat";text:"평면지도";selected:editor.projectionMode==="flat";Layout.fillWidth:true;onClicked:editor.setProjectionMode("flat")}
                }
                ColumnLayout {
                    visible:popup.section==="terrain";Layout.fillWidth:true
                    UiButton {objectName:"terrainNoneButton";menuItem:true;symbol:"minus";text:"없음";selected:editor.terrainMode==="none";Layout.fillWidth:true;onClicked:editor.setTerrainMode("none")}
                    UiButton {objectName:"terrainGrayButton";menuItem:true;symbol:"terrain";text:"흑백";selected:editor.terrainMode==="gray";Layout.fillWidth:true;onClicked:editor.setTerrainMode("gray")}
                    UiButton {objectName:"terrainColorButton";menuItem:true;symbol:"terrain";text:"색채";selected:editor.terrainMode==="color";Layout.fillWidth:true;onClicked:editor.setTerrainMode("color")}
                }
                ColumnLayout {
                    visible:!!popup.group.key;Layout.fillWidth:true
                    UiSwitch {objectName:"viewVisibility";text:"표시";checked:popup.group.visible===true;onClicked:editor.setPresentationVisibility(popup.section,checked)}
                    ColumnLayout {
                        visible:popup.group.content!==true;Layout.fillWidth:true
                        UiSwitch {objectName:"viewBoundary";text:"경계";checked:popup.group.boundary===true;onClicked:editor.setPresentationBoundary(popup.section,checked)}
                        UiSwitch {objectName:"viewColor";text:"색상";checked:popup.group.colorVisible===true;onClicked:editor.setPresentationColorVisible(popup.section,checked)}
                        UiSwitch {objectName:"viewNames";text:"이름";checked:popup.group.names===true;onClicked:editor.setPresentationVisibility(popup.group.nameKey,checked)}
                        UiSwitch {objectName:"viewFlags";text:"국기";checked:popup.group.flags===true;onClicked:editor.setPresentationVisibility(popup.group.flagKey,checked)}
                    }
                    Label {text:"불투명도 "+Math.round((popup.group.opacity===undefined?1:popup.group.opacity)*100)+"%";color:popup.colors.muted}
                    Slider {objectName:"viewOpacity";Layout.fillWidth:true;from:0;to:1;value:popup.group.opacity===undefined?1:popup.group.opacity;onMoved:editor.setPresentationOpacity(popup.section,value)}
                }
                ColumnLayout {
                    visible:popup.section==="distributions";Layout.fillWidth:true
                    RadioButton {objectName:"distributionOverlap";text:"여러 분포 겹쳐 보기";checked:editor.distributionDisplay.mode==="overlap";onClicked:editor.setDistributionDisplay("overlap",editor.distributionDisplay.boundaryVisible)}
                    RadioButton {objectName:"distributionSingle";text:"분포 하나만 보기";checked:editor.distributionDisplay.mode==="single";onClicked:editor.setDistributionDisplay("single",editor.distributionDisplay.boundaryVisible)}
                    UiComboBox {objectName:"distributionActiveLayer";visible:editor.distributionDisplay.mode==="single";Layout.fillWidth:true;textRole:"name";valueRole:"id";model:editor.distributionDisplay.layers;currentIndex:Math.max(0,model.findIndex(function(row){return row.id===editor.distributionDisplay.activeLayerId}));onActivated:editor.setDistributionDisplay("single",editor.distributionDisplay.boundaryVisible,currentValue)}
                    UiSwitch {objectName:"distributionBoundary";text:"분포 경계";checked:editor.distributionDisplay.boundaryVisible;onClicked:editor.setDistributionDisplay(editor.distributionDisplay.mode,checked)}
                }
                ColumnLayout {
                    visible:popup.section==="selection";Layout.fillWidth:true
                    UiButton {menuItem:true;symbol:"eye";text:"선택 객체 표시/숨김";Layout.fillWidth:true;onClicked:editor.toggleSelectionVisibility()}
                    UiButton {menuItem:true;symbol:"lock";text:"라벨 고정";Layout.fillWidth:true;visible:editor.primaryObject.domain==="label"||editor.primaryObject.domain==="territorial";onClicked:editor.setLabelPinned(editor.primaryObject,true)}
                    UiButton {menuItem:true;symbol:"focus";text:"자동 위치";Layout.fillWidth:true;visible:editor.primaryObject.domain==="label"||editor.primaryObject.domain==="territorial";onClicked:editor.resetLabelPosition(editor.primaryObject)}
                }
            }
        }
    }
    }
}