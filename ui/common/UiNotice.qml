import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "UiTokens.js" as Tokens
Rectangle {
    id:notice
    property string text:""
    property string kind:"info"
    property bool closable:false
    signal dismissed()
    readonly property var colors:Tokens.colors(editor.appearancePreferences)
    implicitHeight:message.implicitHeight+24
    color:colors.subtle;border.color:kind==="error"?"#d44848":colors.border;radius:8
    Accessible.name:text
    RowLayout {
        anchors.fill:parent;anchors.margins:12;spacing:8
        UiIcon {name:notice.kind==="error"?"close":"notes";color:notice.kind==="error"?"#d44848":notice.colors.accent;Layout.alignment:Qt.AlignTop}
        Label {id:message;Layout.fillWidth:true;text:notice.text;textFormat:Text.PlainText;wrapMode:Text.WrapAnywhere;color:notice.colors.text}
        UiButton {objectName:"notificationDismiss";visible:notice.closable;symbol:"close";ToolTip.text:"알림 닫기";Layout.alignment:Qt.AlignTop;onClicked:notice.dismissed()}
    }
}