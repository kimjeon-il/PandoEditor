import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "UiTokens.js" as Tokens
Rectangle {
    id:panel
    objectName:"objectPropertyPanel"
    property bool compact:false
    property bool holdFieldCommits:false
    property bool navigating:false
    signal closeRequested()
    readonly property var colors:Tokens.colors(editor.appearancePreferences)
    property alias selectionHeader:headerSlot
    color:colors.panel;border.color:colors.border
    function navigationStarted(){navigating=true;Qt.callLater(function(){panel.navigating=false})}
    function dismissPopup(){return multi.dismissPopup()}
    ColumnLayout {
        anchors.fill:parent;spacing:0
        Item {id:headerSlot;Layout.fillWidth:true;Layout.preferredHeight:editor.selectionItems.length===1?80:0}
        RowLayout {
            Layout.fillWidth:true
            visible:editor.selectionItems.length!==1
            Label {textFormat:Text.PlainText;text:"편집";font.bold:true;Layout.fillWidth:true}
            UiButton {objectName:"closeObjectEditor";symbol:"close";ToolTip.text:"닫기";focusPolicy:Qt.NoFocus;onClicked:{panel.navigationStarted();panel.closeRequested()}}
        }
        TabBar {
            id:tabs;objectName:"objectEditorTabs";Layout.fillWidth:true
            UiTabButton {objectName:"objectInfoTab";text:"정보";onPressed:panel.navigationStarted()}
            UiTabButton {objectName:"objectActionsTab";text:"편집";onPressed:panel.navigationStarted()}
            UiTabButton {objectName:"objectRelationsTab";text:"관계";onPressed:panel.navigationStarted()}
        }
        ScrollView {
            Layout.fillWidth:true;Layout.fillHeight:true;contentWidth:availableWidth;clip:true;padding:16
            ColumnLayout {
                width:parent.width;spacing:10
                Label {textFormat:Text.PlainText;objectName:"preservedDataNotice";text:"미해석 데이터가 보존되어 있습니다. 관련 편집이 제한될 수 있습니다.";Layout.fillWidth:true;wrapMode:Text.Wrap;font.pixelSize:11;color:panel.colors.muted;visible:editor.hasPreservedData}
                Label { textFormat:Text.PlainText; objectName:"nameConflictNotice";text:"같은 종류와 소속 국가에 같은 이름이 있습니다.";visible:!!editor.objectProperties.nameConflict;Layout.fillWidth:true;wrapMode:Text.Wrap }
                Label { textFormat:Text.PlainText; objectName:"objectLockStatus";text:editor.objectProperties.allLocked?"모두 잠김":editor.objectProperties.someLocked?"일부 잠김":"";visible:text!=="" }
                RowLayout {
                    Layout.fillWidth:true
                    visible:tabs.currentIndex===1||editor.selectionItems.length>1
                    UiButton {objectName:"objectLockButton";text:editor.objectProperties.lockLabel||"잠금";enabled:!!editor.objectProperties.lockEnabled;focusPolicy:Qt.NoFocus;onClicked:editor.toggleObjectLock()}
                    UiButton {
                        objectName:"focusSelection";text:"선택 객체로 이동"
                        visible:editor.selectionItems.length===1;focusPolicy:Qt.NoFocus
                        onClicked:{
                            panel.navigationStarted()
                            if(panel.compact){
                                panel.closeRequested()
                                Qt.callLater(function(){editor.focusObject()})
                            }else editor.focusObject()
                        }
                    }
                }
                MultiObjectProperties { id:multi }
                RegionValidityFields {visible:tabs.currentIndex===0&&editor.objectProperties.dateFields===true;holdCommits:panel.holdFieldCommits||panel.navigating}
                ColumnLayout {
                    visible:tabs.currentIndex===0;Layout.fillWidth:true
                    ObjectMetadataField {field:"name";visible:editor.selectionItems.length===1;holdCommits:panel.holdFieldCommits||panel.navigating}
                    ObjectMetadataField {field:"notes";visible:editor.selectionItems.length===1;holdCommits:panel.holdFieldCommits||panel.navigating}
                }
                ColumnLayout {
                    visible:tabs.currentIndex===1;Layout.fillWidth:true
                    UiButton {objectName:"editorGeometryAction";text:"도형 편집";outlined:true;Layout.fillWidth:true;enabled:editor.selectedEditable;onClicked:editor.beginGeometryEdit("edit")}
                    UiButton {text:"다시 그리기";outlined:true;Layout.fillWidth:true;enabled:editor.selectedEditable;onClicked:editor.beginGeometryDraw()}
                    UiButton {objectName:"editorMergeAction";text:"합병";outlined:true;Layout.fillWidth:true;enabled:editor.selectionItems.length===1&&editor.selectedEditable;onClicked:editor.beginMergeSelection()}
                    UiButton {objectName:"editorAnnexAction";text:"그린 영역 편입";outlined:true;Layout.fillWidth:true;enabled:editor.selectionItems.length===1&&editor.selectedEditable;onClicked:editor.beginAnnexGeometry()}
                    UiButton {objectName:"editorSplitAction";text:"절단선 분할";outlined:true;Layout.fillWidth:true;enabled:editor.selectionItems.length===1&&editor.selectedEditable;onClicked:editor.beginSplitGeometry()}
                    UiButton {text:"공유 국경";outlined:true;Layout.fillWidth:true;enabled:editor.selectionItems.length===2;onClicked:editor.beginSharedBoundaryGeometry()}
                    UiButton {text:"삭제";outlined:true;Layout.fillWidth:true;enabled:editor.selectionItems.length>0;onClicked:editor.beginDeleteSelection()}
                }
                ColumnLayout {
                    visible:tabs.currentIndex===2;Layout.fillWidth:true
                    Label {textFormat:Text.PlainText;text:"소속 국가: "+(editor.objectProperties.sovereignId||"없음");Layout.fillWidth:true;wrapMode:Text.Wrap}
                    Label {textFormat:Text.PlainText;text:"상위 단위: "+(editor.objectProperties.parentId||"없음");Layout.fillWidth:true;wrapMode:Text.Wrap}
                    UiButton {text:"종류 전환";outlined:true;enabled:editor.selectedEditable;onClicked:editor.beginTypeConversion()}
                }
            }
        }
    }
}
