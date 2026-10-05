import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "UiTokens.js" as Tokens
Rectangle {
    id:panel
    objectName:"objectPropertyPanel"
    property bool compact:false
    property bool collapsed:false
    property bool advancedOpen:false
    property bool holdFieldCommits:false
    property bool navigating:false
    property var shownSelectionRevision:-1
    Component.onCompleted:shownSelectionRevision=editor.selectionRevision
    Connections {target:editor;function onSelectionChanged(){if(panel.shownSelectionRevision!==editor.selectionRevision){panel.shownSelectionRevision=editor.selectionRevision;tabs.currentIndex=0}}}
    signal closeRequested()
    readonly property var colors:Tokens.colors(editor.appearancePreferences)
    property alias selectionHeader:headerSlot
    color:colors.panel;border.color:colors.border
    function showFlagMenu(trigger){tabs.currentIndex=0;contentFields.showFlagMenu(trigger)}
    function changeTab(){nameMetadata.finish(true);notesMetadata.finish(true);navigationStarted()}
    function navigationStarted(){navigating=true;Qt.callLater(function(){panel.navigating=false})}
    function dismissPopup(){return contentFields.dismissPopup()||(advanced.item&&advanced.item.dismissPopup())||multi.dismissPopup()}
    function showContentCreation(){tabs.currentIndex=0;contentFields.showCreationTools=true;contentFields.autoSelection=false;editor.cancelContentEdit()}
    function startContentCreation(domain,type){tabs.currentIndex=0;contentFields.showCreationTools=false;contentFields.start(domain,type,true)}
    ColumnLayout {
        anchors.fill:parent;spacing:0
        Item {id:headerSlot;Layout.fillWidth:true;Layout.preferredHeight:editor.selectionItems.length===1?(panel.collapsed?56:116):0}
        RowLayout {
            Layout.fillWidth:true
            visible:editor.selectionItems.length!==1
            Label {textFormat:Text.PlainText;text:"편집";font.bold:true;Layout.fillWidth:true}
            UiButton {objectName:"closeObjectEditor";symbol:"close";ToolTip.text:"닫기";focusPolicy:Qt.NoFocus;onClicked:{panel.navigationStarted();panel.closeRequested()}}
        }
        TabBar {
            id:tabs;objectName:"objectEditorTabs";visible:!panel.collapsed;Layout.fillWidth:true
            UiTabButton {objectName:"objectInfoTab";text:"정보";onPressed:panel.changeTab()}
            UiTabButton {objectName:"objectActionsTab";text:"편집";onPressed:panel.changeTab()}
            UiTabButton {objectName:"objectRelationsTab";visible:editor.primaryObject.domain==="territorial";text:"관계";onPressed:panel.changeTab()}
        }
        ScrollView {
            id:propertyScroll;visible:!panel.collapsed
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
                ContentPanel {
                    id:contentFields
                    integrated:true
                    visible:tabs.currentIndex===0
                    Layout.fillWidth:true
                    Layout.preferredHeight:contentHeight + 16
                    holdCommits:panel.holdFieldCommits||panel.navigating
                }
                RegionValidityFields {visible:tabs.currentIndex===0&&editor.objectProperties.dateFields===true;holdCommits:panel.holdFieldCommits||panel.navigating}
                ColumnLayout {
                    visible:tabs.currentIndex===0;Layout.fillWidth:true
                    ObjectMetadataField {id:nameMetadata;field:"name";visible:editor.selectionItems.length===1 && !contentFields.selectionContent;holdCommits:panel.holdFieldCommits||panel.navigating}
                    ObjectMetadataField {id:notesMetadata;field:"notes";visible:editor.selectionItems.length===1 && !contentFields.selectionContent;holdCommits:panel.holdFieldCommits||panel.navigating}
                }
                UiButton {
                    objectName:"countryTab";menuItem:true;symbol:"edit";text:"고급 영토 속성  ›"
                    visible:tabs.currentIndex===0&&!contentFields.selectionContent
                    Layout.fillWidth:true
                    onClicked:{panel.advancedOpen=true;Qt.callLater(function(){propertyScroll.contentItem.contentY=Math.max(0,propertyScroll.contentHeight-propertyScroll.availableHeight)})}
                }
                UiButton {objectName:"closeAdvancedProperties";text:"기본 정보로 돌아가기";visible:panel.advancedOpen&&tabs.currentIndex===0;onClicked:panel.advancedOpen=false}
                Loader {
                    id:advanced
                    active:panel.advancedOpen
                    visible:panel.advancedOpen&&tabs.currentIndex===0&&!contentFields.selectionContent
                    Layout.fillWidth:true;Layout.preferredHeight:Math.max(140,panel.height-230)
                    sourceComponent:EditorPanel {mode:"country";ownsStructureDialog:false;compact:panel.compact;holdFieldCommits:panel.holdFieldCommits||panel.navigating}
                }
                ColumnLayout {
                    visible:tabs.currentIndex===1;Layout.fillWidth:true
                    UiButton {objectName:"editorGeometryAction";text:"도형 편집";symbol:"edit";description:"지도에서 점과 경계를 편집합니다.";outlined:true;Layout.fillWidth:true;enabled:contentFields.selectionContent ? contentFields.editState.active && !contentFields.editState.previewReady && !contentFields.editState.drawing && contentFields.editState.domain!=="distributionLayer" : editor.selectedEditable;onClicked:contentFields.selectionContent ? editor.beginContentGeometry() : editor.beginGeometryEdit("edit")}
                    UiButton {text:"다시 그리기";symbol:"country";description:"현재 대상의 모양을 새로 그립니다.";visible:editor.primaryObject.domain==="territorial";outlined:true;Layout.fillWidth:true;enabled:editor.selectedEditable && editor.primaryObject.domain==="territorial";onClicked:editor.beginGeometryDraw()}
                    UiButton {objectName:"editorMergeAction";text:"합병";symbol:"plus";description:"같은 종류의 제공 영역을 대상에 합칩니다.";visible:editor.primaryObject.domain==="territorial";outlined:true;Layout.fillWidth:true;enabled:editor.selectionItems.length===1&&editor.selectedEditable&&editor.primaryObject.domain==="territorial";onClicked:editor.beginMergeSelection()}
                    UiButton {objectName:"editorAnnexAction";text:"영역 편입";symbol:"region";description:editor.primaryObject.type==="general"&&!editor.objectProperties.parentId?"절단선, 다각형, 구성 영역으로 편입할 영토를 선택합니다.":"제공 영역 중 직접 그린 범위를 편입합니다.";visible:editor.primaryObject.domain==="territorial";outlined:true;Layout.fillWidth:true;enabled:editor.selectionItems.length===1&&editor.selectedEditable&&editor.primaryObject.domain==="territorial";onClicked:editor.beginAnnexGeometry()}
                    UiButton {objectName:"editorSplitAction";text:"절단선 분할";symbol:"subunit";description:"선택한 영역을 새 객체로 나눕니다.";visible:editor.primaryObject.domain==="territorial";outlined:true;Layout.fillWidth:true;enabled:editor.selectionItems.length===1&&editor.selectedEditable&&editor.primaryObject.domain==="territorial"&&editor.primaryObject.type==="general";onClicked:editor.beginSplitGeometry()}
                    UiButton {text:"공유 국경";symbol:"country";description:"두 영토 사이의 경계를 함께 조정합니다.";visible:editor.selectionItems.length===2&&editor.selectionItems.every(function(ref){return ref.domain==="territorial"});outlined:true;Layout.fillWidth:true;enabled:editor.selectionItems.length===2;onClicked:editor.beginSharedBoundaryGeometry()}
                    UiButton {text:"삭제";symbol:"close";description:"검토 후 선택 객체를 삭제합니다.";outlined:true;Layout.fillWidth:true;enabled:editor.selectionItems.length>0;onClicked:{if(contentFields.selectionContent){tabs.currentIndex=0;editor.previewContentEdit(true)}else editor.beginDeleteSelection()}}
                }
                ColumnLayout {
                    visible:tabs.currentIndex===2;Layout.fillWidth:true
                    Label {textFormat:Text.PlainText;objectName:"relationParent";text:"상위 단위: "+(editor.objectProperties.parentName||"없음");Layout.fillWidth:true;wrapMode:Text.Wrap}
                    UiButton {text:"종류 전환";symbol:"type";description:"영토의 종류와 소속을 변경합니다.";visible:editor.primaryObject.domain==="territorial";outlined:true;enabled:editor.selectedEditable;onClicked:editor.beginTypeConversion()}
                }
            }
        }
    }
}
