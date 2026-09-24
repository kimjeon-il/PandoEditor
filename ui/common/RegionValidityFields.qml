import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id:region
    property bool holdCommits:false
    visible:editor.objectProperties.dateFields===true
    Layout.fillWidth:true
    Label { text:"유효기간";font.bold:true }
    Repeater {
        model:[{field:"validFrom",label:"시작",name:"regionValidFrom"},{field:"validTo",label:"종료",name:"regionValidTo"}]
        ColumnLayout {
            id:entry
            required property var modelData
            property string token:""
            property bool edited:false
            property string owner:""
            Layout.fillWidth:true
            Label { text:entry.modelData.label }
            TextField {
                objectName:entry.modelData.name;Layout.fillWidth:true
                text:entry.modelData.field==="validFrom"?editor.validFromDraft:editor.validToDraft
                placeholderText:"YYYY 또는 YYYY-MM-DD";selectByMouse:true
                enabled:!editor.objectProperties.busy
                Accessible.name:"유효기간 "+entry.modelData.label
                onActiveFocusChanged:if(activeFocus){entry.edited=false;entry.owner=editor.selectedId;entry.token=editor.beginPropertyEdit(entry.modelData.field)}
                onTextEdited:{if(entry.owner!==editor.selectedId)return;if(!entry.token)entry.token=editor.beginPropertyEdit(entry.modelData.field);entry.edited=true;editor.updatePropertyEdit(entry.token,text)}
                onEditingFinished:{
                    if(inputMethodComposing)return
                    const token=entry.token;entry.token=""
                    if(token){if(entry.edited&&!region.holdCommits&&entry.owner===editor.selectedId)editor.confirmPropertyEdit(token);else editor.endPropertyEdit(token)}
                    entry.edited=false
                }
            }
            Connections{target:editor;function onSelectionChanged(){if(entry.owner!==editor.selectedId)entry.token=""}}
        }
    }
}
