import QtQuick
import QtQuick.Controls
import "UiTokens.js" as Tokens
TabButton {
    id:tab
    readonly property var colors:Tokens.colors(editor.appearancePreferences)
    implicitHeight:40
    font.pixelSize:14
    background:Rectangle {
        color:tab.hovered?tab.colors.hover:tab.colors.panel
        Rectangle {anchors.horizontalCenter:parent.horizontalCenter;anchors.bottom:parent.bottom;width:30;height:3;radius:1.5;color:tab.colors.accent;visible:tab.checked}
    }
    contentItem:Text {text:tab.text;textFormat:Text.PlainText;font:tab.font;color:tab.checked?tab.colors.text:tab.colors.muted;horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter;elide:Text.ElideRight}
}
