import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    Layout.fillWidth:true
    visible:editor.selectionItems.length>1
    Label { text:editor.selectionItems.length+"개 객체 선택";font.bold:true }
    Button {
        id:color;objectName:"multiObjectColorTrigger";text:"색상";enabled:!!editor.objectProperties.colorEnabled
        onClicked:picker.showAt(color)
    }
    ObjectColorPicker { id:picker }
    function dismissPopup(){if(picker.visible){picker.close();return true}return false}
}
