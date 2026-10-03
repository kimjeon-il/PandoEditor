import QtQuick
import QtQuick.Controls
import "UiTokens.js" as Tokens
ComboBox {
    id:control
    readonly property var colors:Tokens.colors(editor.appearancePreferences)
    implicitHeight:editor.mobileMode?44:36
    padding:8;rightPadding:32
    palette.buttonText:colors.text;palette.base:colors.input;palette.text:colors.text;palette.highlight:colors.accent
    indicator:UiIcon {name:"chevronDown";width:16;height:16;x:control.width-width-10;y:(control.height-height)/2;color:control.colors.muted;opacity:control.enabled?1:.4}
    background:Rectangle {radius:8;color:control.colors.input;border.color:control.activeFocus?control.colors.accent:control.colors.border;border.width:control.activeFocus?2:1;opacity:control.enabled?1:.55;Behavior on border.color {ColorAnimation {duration:140}}}
}
