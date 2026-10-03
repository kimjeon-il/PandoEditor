import QtQuick
import QtQuick.Controls
import "UiTokens.js" as Tokens
TextField {
    id:control
    readonly property var colors:Tokens.colors(editor.appearancePreferences)
    implicitHeight:editor.mobileMode?44:36
    padding:10;color:colors.text;placeholderTextColor:colors.muted;selectionColor:colors.accent
    background:Rectangle {radius:8;color:control.colors.input;border.color:control.activeFocus?control.colors.accent:control.colors.border;border.width:control.activeFocus?2:1;opacity:control.enabled?1:.55;Behavior on border.color {ColorAnimation {duration:140}}}
}
