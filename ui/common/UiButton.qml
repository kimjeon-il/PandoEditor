import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "UiTokens.js" as Tokens
ToolButton {
    id:control
    property string symbol:""
    property bool selected:false
    property bool outlined:false
    property bool menuItem:false
    readonly property var colors:Tokens.colors(editor.appearancePreferences)
    implicitHeight:editor.mobileMode?44:36
    implicitWidth:text.length?contentItem.implicitWidth+20:implicitHeight
    padding:8;font.pixelSize:14;hoverEnabled:true
    Accessible.name:text||ToolTip.text
    background:Rectangle {radius:6;color:control.down||control.selected?control.colors.selected:control.hovered?control.colors.hover:"transparent";border.color:control.activeFocus?control.colors.accent:control.outlined?control.colors.border:"transparent";border.width:control.activeFocus?2:1}
    contentItem:RowLayout {
        spacing:6;opacity:control.enabled?1:.4
        UiIcon {visible:control.symbol!=="";name:control.symbol;color:control.colors.text;Layout.alignment:Qt.AlignVCenter}
        Text {visible:control.text!=="";text:control.text;textFormat:Text.PlainText;font:control.font;color:control.colors.text;elide:Text.ElideRight;Layout.fillWidth:true;verticalAlignment:Text.AlignVCenter;horizontalAlignment:control.menuItem?Text.AlignLeft:Text.AlignHCenter}
    }
    ToolTip.visible:hovered&&ToolTip.text.length>0
    ToolTip.delay:500
}
