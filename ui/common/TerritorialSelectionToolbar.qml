import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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
    implicitHeight:48
    radius:6;color:"#ffffff";border.color:"#bec8d2"
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
    RowLayout {
        anchors.fill:parent;anchors.margins:5;spacing:4
        TextField {
            id:nameField
            objectName:editor.primaryObject.type==="country"?"countryName":editor.primaryObject.type==="subunit"?"subunitName":"regionName"
            Layout.fillWidth:true;Layout.minimumWidth:40
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
        Button {
            id:colorTrigger;objectName:"objectColorTrigger"
            Layout.preferredWidth:42;Layout.preferredHeight:36
            focusPolicy:Qt.NoFocus;enabled:!!editor.objectProperties.colorEnabled
            Accessible.name:"색상 · "+(editor.objectProperties.colorLabel||"")
            contentItem:Rectangle { color:editor.objectProperties.color||"transparent";border.color:"#536576";radius:3;implicitWidth:22;implicitHeight:22 }
            onClicked:colorPicker.showAt(colorTrigger)
        }
        ToolButton { id:notesTrigger;objectName:"objectNotesTrigger";text:"메모";focusPolicy:Qt.NoFocus;onClicked:notes.showAt(notesTrigger) }
        ToolButton { objectName:"toggleObjectEditor";text:bar.editorOpen?"닫기":"편집";focusPolicy:Qt.NoFocus;Accessible.name:bar.editorOpen?"편집 닫기":"편집 열기";onClicked:{bar.navigationStarted();bar.toggleEditor()} }
    }
    ObjectColorPicker { id:colorPicker }
    ObjectNotesPopover { id:notes;holdCommits:bar.holdFieldCommits||bar.navigating }
}
