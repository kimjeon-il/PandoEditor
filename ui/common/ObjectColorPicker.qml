import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ColorPalette.js" as Palette
Popup {
    id:picker
    objectName:"objectColorPicker"
    parent:Overlay.overlay
    property bool customMode:false
    property var restoreTrigger:null
    property bool restoreOnClose:false
    property point anchorPoint:Qt.point(8,8)
    width:Math.min(364,parent?parent.width-16:344)
    height:Math.min(contentItem.implicitHeight+16,parent?parent.height-16:520)
    x:Math.max(8,Math.min(anchorPoint.x,parent?parent.width-width-8:8))
    y:editor.mobileMode?Math.max(8,(parent?parent.height:540)-height-8):Math.max(8,Math.min(anchorPoint.y,parent?parent.height-height-8:8))
    padding:8;modal:false;focus:true
    closePolicy:Popup.CloseOnEscape|Popup.CloseOnPressOutside
    function showAt(trigger){
        if(visible){close();return}
        if(!editor.beginColorEdit())return
        anchorPoint=trigger.mapToItem(parent,0,trigger.height+4)
        restoreTrigger=trigger;restoreOnClose=false;customMode=false;open()
    }
    function commit(value,reset){restoreOnClose=true;if(editor.confirmColorEdit(value,reset))close();else {restoreOnClose=false;custom.message="색상을 적용하지 못했습니다. 입력과 편집 대상을 확인하세요."}}
    function cancelFromControl(){restoreOnClose=true;close()}
    onClosed:{editor.cancelColorEdit();if(restoreOnClose&&restoreTrigger&&restoreTrigger.visible&&restoreTrigger.enabled)restoreTrigger.forceActiveFocus();restoreOnClose=false}
    onOpened: { if(defaultColor.visible)defaultColor.forceActiveFocus();else if(swatches.count)swatches.itemAt(0).forceActiveFocus() }
    Connections {target:editor;function onColorEditChanged(){if(!editor.colorEditOpen)picker.close()}}
    contentItem:ColumnLayout {
        spacing:8
        ColumnLayout {
            visible:!picker.customMode
            Layout.fillWidth:true
            Button {
                id:defaultColor;objectName:"defaultObjectColor";Layout.fillWidth:true
                visible:editor.selectionItems.length===1
                text:editor.primaryObject.type==="subunit"?"국가색 상속":"기본 색상"
                icon.width:16;icon.height:16
                contentItem:RowLayout {
                    Rectangle { Layout.preferredWidth:20;Layout.preferredHeight:20;color:editor.objectProperties.defaultColor||"#cccccc";border.color:"#657480" }
                    Label { text:defaultColor.text;Layout.fillWidth:true }
                    Label { text:"✓";visible:editor.objectProperties.colorExplicit===false }
                }
                onClicked:picker.commit("",true)
            }
            GridLayout {
                objectName:"webColorPalette";Layout.fillWidth:true;columns:13;columnSpacing:2;rowSpacing:4
                Repeater {
                    id:swatches
                    model:Palette.colors
                    delegate:Button {
                        required property var modelData
                        objectName:"palette"+modelData.color.slice(1)
                        Layout.fillWidth:true;Layout.minimumWidth:0;Layout.preferredHeight:24;padding:0
                        Accessible.name:modelData.label+" ("+modelData.color.toUpperCase()+") 색상"
                        background:Rectangle {color:modelData.color;radius:2;border.width:editor.objectProperties.colorExplicit&&editor.objectProperties.color===modelData.color?3:1;border.color:"#344352"}
                        contentItem:Label {
                            readonly property bool chosen:editor.objectProperties.colorExplicit===true&&editor.objectProperties.color===modelData.color
                            text:chosen?"✓":"";horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter
                            color:{const c=modelData.color;const l=(parseInt(c.slice(1,3),16)*0.2126+parseInt(c.slice(3,5),16)*0.7152+parseInt(c.slice(5,7),16)*0.0722)/255;return l>=0.62?"#15222e":"white"}
                        }
                        onClicked:picker.commit(modelData.color,false)
                    }
                }
            }
            Button {objectName:"customColorButton";text:"사용자 지정";Layout.fillWidth:true;onClicked:{picker.customMode=true;custom.open(editor.objectProperties.color||"#3f6fae")}}
        }
        CustomColorEditor {
            id:custom;visible:picker.customMode;Layout.fillWidth:true
            onApplyRequested:function(value){picker.commit(value,false)}
            onCancelRequested:picker.cancelFromControl()
        }
    }
}
