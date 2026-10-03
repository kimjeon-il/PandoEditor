import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "UiTokens.js" as Tokens
ColumnLayout {
    id:entry
    property string field:"name"
    property bool holdCommits:false
    property string token:""
    property string owner:""
    property bool edited:false
    readonly property var colors:Tokens.colors(editor.appearancePreferences)
    Layout.fillWidth:true
    spacing:6
    function begin(){owner=editor.selectedId;edited=field==="name"&&!!editor.objectProperties.namePending;token=editor.beginPropertyEdit(field)}
    function update(text){
        if(owner!==editor.selectedId)return
        if(!token)begin()
        // Undo/Redo can invalidate a session while this input keeps focus.
        // Reacquire the controller token; never keep editing an expired session.
        if(!editor.updatePropertyEdit(token,text)){
            token=editor.beginPropertyEdit(field)
            if(token)editor.updatePropertyEdit(token,text)
        }
        edited=true
    }
    function finish(){
        const current=token;token=""
        if(current){if(edited&&!holdCommits&&owner===editor.selectedId)editor.confirmPropertyEdit(current);else editor.endPropertyEdit(current)}
        edited=false
    }
    Label {text:entry.field==="name"?(editor.primaryObject.type==="country"?"국명":"이름"):"메모";color:entry.colors.muted;font.pixelSize:14}
    TextField {
        id:nameInput;objectName:entry.field==="name"?"detailObjectName":"";visible:entry.field==="name";Layout.fillWidth:true;Layout.preferredHeight:36
        text:editor.nameDraft;enabled:!!editor.objectProperties.editable;selectByMouse:true
        font.pixelSize:14;color:entry.colors.text;padding:10
        background:Rectangle {color:entry.colors.input;border.color:nameInput.activeFocus?entry.colors.accent:entry.colors.border;radius:6}
        onActiveFocusChanged:if(activeFocus)entry.begin()
        onTextEdited:entry.update(text)
        onEditingFinished:if(!inputMethodComposing)entry.finish()
    }
    ScrollView {
        visible:entry.field==="notes";Layout.fillWidth:true;Layout.preferredHeight:112;clip:true
        TextArea {
            id:notesInput;objectName:entry.field==="notes"?"detailObjectNotes":"";text:editor.memoDraft;readOnly:!editor.objectProperties.editable
            placeholderText:"객체에 대한 메모를 입력하세요.";wrapMode:TextEdit.Wrap;selectByMouse:true
            font.pixelSize:14;color:entry.colors.text;padding:10
            background:Rectangle {color:entry.colors.input;border.color:notesInput.activeFocus?entry.colors.accent:entry.colors.border;radius:6}
            onActiveFocusChanged:{if(activeFocus)entry.begin();else entry.finish()}
            onTextChanged:if(activeFocus&&entry.owner===editor.selectedId&&entry.token)entry.update(text)
        }
    }
    Connections {
        target:editor
        function onSelectionChanged(){if(entry.owner!==editor.selectedId){entry.token="";entry.edited=false;nameInput.focus=false;notesInput.focus=false}}
    }
}
