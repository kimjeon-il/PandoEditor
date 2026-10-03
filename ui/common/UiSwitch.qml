import QtQuick
import QtQuick.Controls
import "UiTokens.js" as Tokens
Switch {
    id:control
    readonly property var colors:Tokens.colors(editor.appearancePreferences)
    implicitHeight:editor.mobileMode?44:36
    implicitWidth:indicator.width+contentItem.implicitWidth+24
    padding:4
    indicator:Rectangle {
        implicitWidth:34;implicitHeight:20;x:control.leftPadding;y:(control.height-height)/2;radius:10
        color:control.checked?control.colors.accent:control.colors.border;opacity:control.enabled?1:.45
        Behavior on color {ColorAnimation {duration:140}}
        Rectangle {width:16;height:16;radius:8;y:2;x:control.checked?parent.width-width-2:2;color:"white";Behavior on x {NumberAnimation {duration:140;easing.type:Easing.InOutQuad}}}
    }
    contentItem:Text {text:control.text;textFormat:Text.PlainText;font:control.font;color:control.colors.text;opacity:control.enabled?1:.45;leftPadding:control.indicator.width+8;verticalAlignment:Text.AlignVCenter;wrapMode:Text.WordWrap}
}
