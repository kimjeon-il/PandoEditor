import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "UiTokens.js" as Tokens
ToolButton {
    id:control
    property string symbol:""
    property string description:""
    property bool selected:false
    property bool outlined:false
    property bool menuItem:false
    readonly property var colors:Tokens.colors(editor.appearancePreferences)
    implicitHeight:description.length?68:(editor.mobileMode?44:36)
    implicitWidth:text.length?contentItem.implicitWidth+20:implicitHeight
    padding:8;font.pixelSize:14;hoverEnabled:true
    Accessible.name:text||ToolTip.text
    background:Rectangle {radius:6;color:control.highlighted?control.colors.accent:control.down||control.selected||control.checked?control.colors.selected:control.hovered?control.colors.hover:"transparent";border.color:control.activeFocus?control.colors.accent:control.outlined?control.colors.border:"transparent";border.width:control.activeFocus?2:1;Behavior on color {ColorAnimation {duration:140}} Behavior on border.color {ColorAnimation {duration:140}}}
    contentItem:RowLayout {
        spacing:6;opacity:control.enabled?1:.4
        UiIcon {visible:control.symbol!=="";name:control.symbol;color:control.highlighted?"#ffffff":control.colors.text;Layout.alignment:Qt.AlignVCenter}
        ColumnLayout {
            Layout.fillWidth:true;spacing:3
            Text {visible:control.text!=="";text:control.text;textFormat:Text.PlainText;font:control.font;color:control.highlighted?"#ffffff":control.colors.text;elide:Text.ElideRight;Layout.fillWidth:true;horizontalAlignment:control.menuItem||control.description.length?Text.AlignLeft:Text.AlignHCenter}
            Text {visible:control.description.length>0;text:control.description;textFormat:Text.PlainText;font.pixelSize:12;color:control.colors.muted;wrapMode:Text.Wrap;Layout.fillWidth:true}
        }
    }
    ToolTip.visible:hovered&&ToolTip.text.length>0
    ToolTip.delay:500
}
