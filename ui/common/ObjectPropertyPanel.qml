import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id:panel
    objectName:"objectPropertyPanel"
    property bool holdFieldCommits:false
    property bool navigating:false
    signal closeRequested()
    color:"#ffffff";border.color:"#ccd5df"
    function navigationStarted(){navigating=true;Qt.callLater(function(){panel.navigating=false})}
    function dismissPopup(){return multi.dismissPopup()}
    ColumnLayout {
        anchors.fill:parent;anchors.margins:10;spacing:8
        RowLayout {
            Layout.fillWidth:true
            Label { textFormat:Text.PlainText; text:"편집";font.bold:true;Layout.fillWidth:true }
            ToolButton { objectName:"closeObjectEditor";text:"닫기";focusPolicy:Qt.NoFocus;onClicked:{panel.navigationStarted();panel.closeRequested()} }
        }
        ScrollView {
            Layout.fillWidth:true;Layout.fillHeight:true;contentWidth:availableWidth;clip:true
            ColumnLayout {
                width:parent.width;spacing:10
                Label { textFormat:Text.PlainText; objectName:"preservedDataNotice";text:editor.documentNotice;Layout.fillWidth:true;wrapMode:Text.Wrap;font.pixelSize:11;color:"#5c6874" }
                Label { textFormat:Text.PlainText; text:editor.objectProperties.displayName||"";visible:editor.selectionItems.length===1;Layout.fillWidth:true;wrapMode:Text.Wrap }
                Label { textFormat:Text.PlainText; objectName:"nameConflictNotice";text:"같은 종류와 소속 국가에 같은 이름이 있습니다.";visible:!!editor.objectProperties.nameConflict;Layout.fillWidth:true;wrapMode:Text.Wrap }
                Label { textFormat:Text.PlainText; objectName:"objectLockStatus";text:editor.objectProperties.allLocked?"모두 잠김":editor.objectProperties.someLocked?"일부 잠김":"";visible:text!=="" }
                RowLayout {
                    Layout.fillWidth:true
                    Button { objectName:"objectLockButton";text:editor.objectProperties.lockLabel||"잠금";enabled:!!editor.objectProperties.lockEnabled;focusPolicy:Qt.NoFocus;onClicked:editor.toggleObjectLock() }
                    Button { objectName:"focusSelection";text:"선택 객체로 이동";visible:editor.selectionItems.length===1;focusPolicy:Qt.NoFocus;onClicked:{panel.navigationStarted();editor.focusObject()} }
                }
                MultiObjectProperties { id:multi }
                RegionValidityFields { holdCommits:panel.holdFieldCommits||panel.navigating }
                Label { textFormat:Text.PlainText; visible:editor.selectionItems.length===1&&!!editor.objectProperties.sovereignId;text:"소속 국가: "+(editor.objectProperties.sovereignId||"");Layout.fillWidth:true;wrapMode:Text.Wrap }
                Label { textFormat:Text.PlainText; visible:editor.selectionItems.length===1&&!!editor.objectProperties.parentId;text:"상위 단위: "+(editor.objectProperties.parentId||"");Layout.fillWidth:true;wrapMode:Text.Wrap }
                Label { textFormat:Text.PlainText; visible:editor.selectionItems.length===1&&editor.primaryObject.type!=="country";text:"소속·종류·형상 변경은 후속 단계입니다.";Layout.fillWidth:true;wrapMode:Text.Wrap;color:"#5c6874" }
            }
        }
    }
}
