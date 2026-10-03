import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "UiTokens.js" as Tokens
Popup {
    id: notes
    objectName: "objectNotesPopover"
    parent: Overlay.overlay
    property string ownerId: ""
    property string ownerType: ""
    property string editToken: ""
    property bool wasEdited:false
    property string initialText:""
    property bool holdCommits: false
    property point anchorPoint: Qt.point(8,8)
    width: Math.min(340,parent ? parent.width-16 : 324)
    height: Math.min(220,parent ? parent.height-16 : 220)
    x: Math.max(8,Math.min(anchorPoint.x,parent ? parent.width-width-8 : 8))
    y: Math.max(8,Math.min(anchorPoint.y,parent ? parent.height-height-8 : 8))
    padding: 10; focus: true
    background:Rectangle {color:Tokens.colors(editor.appearancePreferences).panel;border.color:Tokens.colors(editor.appearancePreferences).border;radius:9}
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnReleaseOutside
    function finish() {
        const token=editToken;editToken=""
        if (token) {
            if (wasEdited && !textArea.readOnly && !holdCommits && ownerId===editor.selectedId) editor.confirmPropertyEdit(token)
            else editor.endPropertyEdit(token)
        }
        wasEdited=false
    }
    function showAt(trigger) {
        if(visible) { close();return }
        ownerId=editor.selectedId;ownerType=editor.primaryObject.type
        const below=trigger.mapToItem(parent,0,trigger.height+4)
        const above=trigger.parent.mapToItem(parent,0,0)
        anchorPoint=Qt.point(below.x,below.y+height<=parent.height-8?below.y:Math.max(8,above.y-height-8))
        open();Qt.callLater(function(){if(notes.visible)textArea.forceActiveFocus()})
    }
    onClosed: finish()
    Connections {
        target:editor
        function onSelectionChanged(){if(notes.ownerId!==editor.selectedId){notes.editToken="";notes.close()}}
        function onGeometryChanged(){notes.editToken="";notes.close()}
    }
    contentItem: ColumnLayout {
        RowLayout {
            Layout.fillWidth:true
            Label { text:"메모";font.bold:true;Layout.fillWidth:true }
            ToolButton { objectName:"closeObjectNotes";text:"닫기";onClicked:notes.close() }
        }
        ScrollView {
            Layout.fillWidth:true;Layout.fillHeight:true;clip:true
            TextArea {
                id:textArea
                objectName: notes.ownerType==="country"?"countryMemo":notes.ownerType==="subunit"?"subunitMemo":"regionMemo"
                text:editor.memoDraft
                readOnly:!editor.objectProperties.editable
                selectByMouse:true;wrapMode:TextEdit.Wrap
                Accessible.name:"메모"
                onActiveFocusChanged:{if(activeFocus){notes.wasEdited=false;notes.initialText=text;notes.editToken=editor.beginPropertyEdit("notes")}else notes.finish()}
                onTextChanged: if(!readOnly && activeFocus && notes.editToken && notes.ownerId===editor.selectedId){notes.wasEdited=text!==notes.initialText;editor.updatePropertyEdit(notes.editToken,text)}
                Keys.onEscapePressed:{notes.close();event.accepted=true}
                Keys.onBackPressed:{notes.close();event.accepted=true}
            }
        }
    }
}
