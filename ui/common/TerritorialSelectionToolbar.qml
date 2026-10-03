import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "UiTokens.js" as Tokens
Rectangle {
    id:bar
    objectName:"territorialToolbar"
    property bool editorOpen:false
    property bool holdFieldCommits:false
    property bool navigating:false
    property string editingOwner:""
    property string presentedKey:""
    property string editToken:""
    property bool nameEdited:false
    signal toggleEditor()
    visible:editor.selectionItems.length===1
    readonly property var colors:Tokens.colors(editor.appearancePreferences)
    readonly property var selectedVisual:editor.countryVisuals[editor.selectedId]||({})
    implicitHeight:editorOpen?80:142
    radius:editorOpen?0:16;color:colors.panel;border.color:editorOpen?"transparent":colors.border
    function navigationStarted(){navigating=true;Qt.callLater(function(){bar.navigating=false})}
    function finishName(){
        const token=editToken;editToken=""
        if(token){
            if(nameEdited&&!holdFieldCommits&&!navigating&&editingOwner===editor.selectedId)editor.confirmPropertyEdit(token)
            else editor.endPropertyEdit(token)
        }
        nameEdited=false
    }
    function dismissPopup(){
        if(colorPicker.visible){colorPicker.close();return true}
        if(notes.visible){notes.close();return true}
        return false
    }
    function closeTransient(){colorPicker.close();notes.close()}
    Connections {
        target:editor
        function onSelectionChanged(){if(bar.presentedKey!==(editor.primaryObject.key||"")){bar.presentedKey=editor.primaryObject.key||"";bar.editToken="";bar.nameEdited=false;nameField.focus=false;Qt.inputMethod.reset();bar.editingOwner="";notes.close();colorPicker.close()}}
        function onGeometryChanged(){bar.editToken="";bar.editingOwner="";bar.closeTransient()}
    }
    Image {
        objectName:"selectionCardFlag"
        x:bar.editorOpen?12:16;y:bar.editorOpen?12:18
        width:bar.editorOpen?56:108;height:bar.editorOpen?42:86
        source:bar.selectedVisual.flagSource||""
        fillMode:Image.PreserveAspectFit
    }
    UiIcon {
        x:bar.editorOpen?30:55;y:bar.editorOpen?22:50
        name:"globe";color:bar.colors.muted
        visible:!bar.selectedVisual.flagAvailable
    }
    TextField {
            id:nameField
            objectName:editor.primaryObject.type==="country"?"countryName":editor.primaryObject.type==="subunit"?"subunitName":"regionName"
            x:bar.editorOpen?80:142;y:bar.editorOpen?12:18
            width:Math.max(40,bar.width-x-(bar.editorOpen?52:16));height:38
            padding:0;font.pixelSize:bar.editorOpen?16:22;font.weight:Font.DemiBold
            color:bar.colors.text
            background:Rectangle {color:"transparent";radius:4;border.color:nameField.activeFocus?bar.colors.accent:"transparent"}
            text:editor.nameDraft
            placeholderText:editor.primaryObject.type==="country"?"국명":editor.primaryObject.type==="subunit"?"하위단위명":"지방명"
            Accessible.name:placeholderText
            enabled:!!editor.objectProperties.editable
            selectByMouse:true
            onActiveFocusChanged:{
                if(activeFocus){bar.nameEdited=!!editor.objectProperties.namePending;bar.editingOwner=editor.selectedId;bar.editToken=editor.beginPropertyEdit("name")}
            }
            onTextEdited:{
                if(bar.editingOwner!==editor.selectedId)return
                if(!bar.editToken)bar.editToken=editor.beginPropertyEdit("name")
                bar.nameEdited=true;editor.updatePropertyEdit(bar.editToken,text)
            }
            onEditingFinished:if(!inputMethodComposing)bar.finishName()
    }
    RowLayout {
        visible:!bar.editorOpen
        anchors.right:parent.right;anchors.bottom:parent.bottom;anchors.rightMargin:12;anchors.bottomMargin:10;spacing:0
        UiButton {
            id:colorTrigger;objectName:"objectColorTrigger"
            Layout.preferredWidth:40;Layout.preferredHeight:40
            focusPolicy:Qt.NoFocus;enabled:!!editor.objectProperties.colorEnabled
            Accessible.name:"색상 · "+(editor.objectProperties.colorLabel||"")
            contentItem:Rectangle { color:editor.objectProperties.color||"transparent";border.color:"#536576";radius:3;implicitWidth:22;implicitHeight:22 }
            onClicked:colorPicker.showAt(colorTrigger)
        }
        UiButton {objectName:"selectionVisibilityButton";symbol:"eye";ToolTip.text:"표시·숨김";focusPolicy:Qt.NoFocus;onClicked:editor.toggleSelectionVisibility()}
        UiButton {symbol:editor.objectProperties.locked?"lock":"unlock";ToolTip.text:editor.objectProperties.lockLabel||"잠금";enabled:!!editor.objectProperties.lockEnabled;focusPolicy:Qt.NoFocus;onClicked:editor.toggleObjectLock()}
        UiButton {id:notesTrigger;objectName:"objectNotesTrigger";symbol:"notes";ToolTip.text:"메모";focusPolicy:Qt.NoFocus;onClicked:notes.showAt(notesTrigger)}
        UiButton {objectName:!bar.editorOpen?"toggleObjectEditor":"";symbol:"edit";ToolTip.text:"상세 편집";focusPolicy:Qt.NoFocus;Accessible.name:ToolTip.text;onClicked:{bar.navigationStarted();bar.toggleEditor()}}
    }
    UiButton {objectName:bar.editorOpen?"toggleObjectEditor":"";visible:bar.editorOpen;symbol:"close";x:bar.width-width-12;y:12;ToolTip.text:"편집 닫기";focusPolicy:Qt.NoFocus;onClicked:{bar.navigationStarted();bar.toggleEditor()}}
    ObjectColorPicker { id:colorPicker }
    ObjectNotesPopover { id:notes;holdCommits:bar.holdFieldCommits||bar.navigating }
}
