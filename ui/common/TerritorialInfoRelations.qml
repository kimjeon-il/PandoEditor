import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "UiTokens.js" as Tokens
ColumnLayout {
    id:relations
    readonly property var parentRows:editor.objectProperties.parentRows||[]
    readonly property var childRows:editor.objectProperties.childRows||[]
    readonly property var colors:Tokens.colors(editor.appearancePreferences)
    visible:parentRows.length>0||childRows.length>0
    Layout.fillWidth:true
    spacing:10
    // Web a1555722 relation names are inert; only the GPS button focuses the
    // related object, retaining the primary selection and property panel.
    Repeater {
        model:[{label:"소속",rows:relations.parentRows},{label:"산하",rows:relations.childRows}]
        delegate:ColumnLayout {
            required property var modelData
            id:group
            visible:modelData.rows.length>0
            Layout.fillWidth:true
            spacing:4
            Label {text:group.modelData.label;color:relations.colors.muted;font.pixelSize:14}
            Repeater {
                model:group.modelData.rows
                delegate:RowLayout {
                    required property var modelData
                    id:row
                    Layout.fillWidth:true
                    spacing:8
                    Item {
                        Layout.preferredWidth:24;Layout.preferredHeight:24
                        Image {anchors.fill:parent;source:row.modelData.flagSource||"";fillMode:Image.PreserveAspectFit;asynchronous:true}
                    }
                    Label {
                        objectName:"territorialInfoName_"+row.modelData.id
                        textFormat:Text.PlainText;text:row.modelData.name
                        Layout.fillWidth:true;Layout.minimumWidth:0
                        elide:Text.ElideRight;color:relations.colors.text;font.pixelSize:14
                    }
                    UiButton {
                        objectName:"territorialInfoFocus_"+row.modelData.id
                        symbol:"focus";focusPolicy:Qt.NoFocus
                        ToolTip.text:"선택 객체로 이동"
                        Accessible.name:row.modelData.name+"으로 이동"
                        onClicked:editor.focusObject(row.modelData.ref)
                    }
                }
            }
        }
    }
}
