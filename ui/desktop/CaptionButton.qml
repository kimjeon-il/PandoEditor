import QtQuick
import QtQuick.Controls

AbstractButton {
    id: control
    required property int captionAction
    required property var frame
    property bool maximized: false
    property bool windowActive: true
    property bool darkAppearance: false
    readonly property bool highlighted: frame.hoveredButton === captionAction || hovered
    readonly property bool held: frame.pressedButton === captionAction || down
    readonly property color ink: captionAction === 3 && highlighted ? "white"
                                : windowActive ? (darkAppearance ? "#edf1f5" : "#1b1b1b") : "#858585"
    width: 46
    height: 32
    padding: 0
    focusPolicy: Qt.TabFocus
    Accessible.name: captionAction === 1 ? "최소화" : captionAction === 3 ? "닫기"
                                 : maximized ? "이전 크기로 복원" : "최대화"
    onClicked: frame.invoke(captionAction)
    background: Rectangle {
        color: control.highlighted
               ? control.captionAction === 3 ? (control.held ? "#b62b22" : "#c42b1c")
                                     : control.darkAppearance ? (control.held ? "#29313a" : "#222931") : (control.held ? "#d6d6d6" : "#e5e5e5")
               : "transparent"
        Rectangle {
            anchors.fill: parent
            anchors.margins: 3
            color: "transparent"
            border.width: control.visualFocus ? 1 : 0
            border.color: control.ink
        }
    }
    contentItem: Item {
        // Small vector primitives, not font glyphs: stable at every DPI/font.
        Item {
            anchors.centerIn: parent
            width: 10
            height: 10
            Rectangle {
                visible: control.captionAction === 1
                width: 10; height: 1
                anchors.verticalCenter: parent.verticalCenter
                color: control.ink
            }
            Rectangle {
                visible: control.captionAction === 2 && !control.maximized
                anchors.fill: parent
                color: "transparent"
                border.width: 1; border.color: control.ink
            }
            Item {
                visible: control.captionAction === 2 && control.maximized
                anchors.fill: parent
                Rectangle { x: 2; y: 0; width: 8; height: 1; color: control.ink }
                Rectangle { x: 9; y: 0; width: 1; height: 8; color: control.ink }
                Rectangle { x: 2; y: 0; width: 1; height: 2; color: control.ink }
                Rectangle { x: 8; y: 7; width: 2; height: 1; color: control.ink }
                Rectangle { x: 0; y: 2; width: 8; height: 8; color: "transparent"; border.width: 1; border.color: control.ink }
            }
            Item {
                visible: control.captionAction === 3
                anchors.fill: parent
                Rectangle { anchors.centerIn: parent; width: 13; height: 1; rotation: 45; color: control.ink; antialiasing: true }
                Rectangle { anchors.centerIn: parent; width: 13; height: 1; rotation: -45; color: control.ink; antialiasing: true }
            }
        }
    }
}
