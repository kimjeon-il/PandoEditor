import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "UiTokens.js" as Tokens
ColumnLayout {
    id:entry
    property bool holdCommits:false
    property string token:""
    property string owner:""
    property string ownerProject:""
    property var ownerRevision:0
    property bool edited:false
    property bool submitting:false
    property bool refreshing:false
    property string errorText:""
    readonly property var colors:Tokens.colors(editor.appearancePreferences)
    Layout.fillWidth:true
    spacing:6
    // Web a1555722 owns one complete local input. Endpoints never become
    // independent drafts, and a rejected edit remains visible until edited.
    function reload(){
        owner=editor.selectedId;ownerProject=editor.projectInstanceId;ownerRevision=editor.revision
        periodInput.text=editor.objectProperties.periodInput||"";errorText="";edited=false
    }
    function begin(){
        owner=editor.selectedId;ownerProject=editor.projectInstanceId;ownerRevision=editor.revision
        token=editor.beginPropertyEdit("validity")
    }
    // Web presentFields resets the canonical value after document publication.
    // Focus alone cannot keep a local token from an undone or replaced project.
    function refreshCanonical(){
        if(submitting||refreshing)return
        refreshing=true
        const ownerChanged=owner!==editor.selectedId||ownerProject!==editor.projectInstanceId
        if(ownerChanged||ownerRevision!==editor.revision){
            const current=token;token=""
            if(current)editor.endPropertyEdit(current)
            if(ownerChanged)periodInput.focus=false
            reload()
            if(periodInput.activeFocus&&periodInput.enabled)begin()
        }else if(!periodInput.activeFocus&&!edited&&!errorText)reload()
        refreshing=false
    }
    function finish(force){
        if(periodInput.inputMethodComposing)return
        const current=token;token=""
        if(!current)return
        if(!edited||(holdCommits&&force!==true)||owner!==editor.selectedId||ownerProject!==editor.projectInstanceId||ownerRevision!==editor.revision){editor.endPropertyEdit(current);edited=false;return}
        submitting=true
        const accepted=editor.commitTerritorialPeriod(current,periodInput.text)
        submitting=false
        edited=false
        if(accepted)reload()
        else if(!errorText)errorText="존속기간을 변경할 수 없습니다. 편집 대상과 잠금 상태를 확인하세요."
    }
    Label {text:"존속기간";color:entry.colors.muted;font.pixelSize:14}
    UiTextField {
        id:periodInput
        objectName:"territorialPeriodInput"
        Layout.fillWidth:true
        selectByMouse:true
        enabled:!!editor.objectProperties.editable
        Accessible.name:"존속기간"
        Accessible.description:entry.errorText
        background:Rectangle {
            radius:8;color:entry.colors.input
            border.color:entry.errorText?"#c43c3c":periodInput.activeFocus?entry.colors.accent:entry.colors.border
            border.width:periodInput.activeFocus||entry.errorText?2:1
            opacity:periodInput.enabled?1:.55
        }
        Component.onCompleted:entry.reload()
        onActiveFocusChanged:if(activeFocus){entry.edited=false;entry.begin()}
        onTextEdited:{
            if(entry.owner!==editor.selectedId)return
            entry.errorText=""
            if(!entry.token)entry.begin()
            entry.edited=true
        }
        onEditingFinished:entry.finish()
    }
    Label {
        objectName:"territorialPeriodValidationError"
        textFormat:Text.PlainText;text:entry.errorText
        visible:text.length>0
        Layout.fillWidth:true;wrapMode:Text.Wrap
        color:"#c43c3c";font.pixelSize:12
        Accessible.name:text
    }
    Connections {
        target:editor
        function onErrorOccurred(message){if(entry.submitting)entry.errorText=message}
        function onSelectionChanged(){entry.refreshCanonical()}
        function onPropertyChanged(){entry.refreshCanonical()}
    }
}
