import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import "UiTokens.js" as Tokens
Rectangle {
    id:bar
    objectName:"territorialToolbar"
    property bool editorOpen:false
    property bool collapsed:false
    property bool holdFieldCommits:false
    property bool navigating:false
    property var previewData:({})
    readonly property var displayed:editorOpen?editor.objectProperties:previewData
    readonly property var displayedRef:editorOpen?editor.primaryObject:previewData
    property bool previewHover:false
    readonly property bool previewPopup:colorPicker.visible
    signal flagRequested(var trigger)
    signal activatePreview()
    signal toggleEditor()
    function targetPreview(){if(!editorOpen)activatePreview()}
    visible:editor.selectionItems.length===1
    readonly property var colors:Tokens.colors(editor.appearancePreferences)
    readonly property string flagSource:editorOpen?editor.selectedFlagSource:(previewData.flagSource||"")
    readonly property bool compactPreview:Window.window!==null&&Window.window.width<800
    readonly property real previewFlagWidth:compactPreview?80:92
    readonly property real previewFlagHeight:compactPreview?60:68
    implicitHeight:editorOpen?(collapsed?56:116):142
    radius:editorOpen?0:16;color:colors.panel;border.color:editorOpen?"transparent":colors.border
    function navigationStarted(){navigating=true;Qt.callLater(function(){bar.navigating=false})}
    function dismissPopup(){if(colorPicker.visible){colorPicker.close();return true}return false}
    function closeTransient(){colorPicker.close()}
    property var lastSelectionRevision:-1
    Component.onCompleted:lastSelectionRevision=editor.selectionRevision
    Connections {target:editor;function onSelectionChanged(){if(bar.lastSelectionRevision!==editor.selectionRevision){bar.lastSelectionRevision=editor.selectionRevision;bar.closeTransient()}}function onGeometryChanged(){bar.closeTransient()}}
    Rectangle {visible:!bar.editorOpen;anchors.fill:parent;anchors.topMargin:5;anchors.bottomMargin:-5;color:"#16000000";radius:16;z:-2}
    Rectangle {objectName:"selectionCardTail";visible:!bar.editorOpen;width:12;height:12;x:parent.width/2-6;y:parent.height-6;rotation:45;color:bar.colors.panel;border.color:bar.colors.border;z:-1}
    UiButton {objectName:bar.editorOpen?"":"toggleObjectEditor";anchors.fill:parent;visible:!bar.editorOpen;Accessible.name:"상세 편집 · "+(bar.displayed.displayName||bar.displayed.name||"");background:Item{} contentItem:Item{} onClicked:{bar.targetPreview();bar.navigationStarted();bar.toggleEditor()}}
    Rectangle {x:bar.editorOpen||bar.compactPreview?12:16;y:bar.editorOpen?12:bar.compactPreview?14:18;width:bar.editorOpen?56:bar.compactPreview?94:108;height:bar.editorOpen?42:bar.compactPreview?76:86;radius:6;color:bar.colors.subtle}
    Image {
        objectName:"selectionCardFlag";x:bar.editorOpen?16:bar.compactPreview?19:24;y:bar.editorOpen?16:bar.compactPreview?22:27
        width:bar.editorOpen?48:bar.previewFlagWidth;height:bar.editorOpen?34:bar.previewFlagHeight
        sourceSize:Qt.size(Math.ceil(width*Screen.devicePixelRatio),Math.ceil(height*Screen.devicePixelRatio))
        source:bar.flagSource;fillMode:Image.PreserveAspectFit
    }
    UiButton {id:flagTrigger;objectName:"flagMenuButton";visible:bar.editorOpen&&bar.displayedRef.domain==="territorial";enabled:!!editor.objectProperties.editable;x:12;y:12;width:56;height:42;Accessible.name:"국기 변경";background:Item{} contentItem:Item{} onClicked:bar.flagRequested(flagTrigger)}
    UiIcon {x:bar.editorOpen?30:55;y:bar.editorOpen?22:50;name:bar.displayedRef.domain==="territorial"?"country":bar.displayedRef.domain==="label"?"place":bar.displayedRef.domain==="hydro"||bar.displayedRef.domain==="hydroBuiltin"?"river":"distribution";color:bar.colors.muted;visible:!bar.flagSource}
    Label {
        objectName:"selectionCardName";x:bar.editorOpen?80:142;y:bar.editorOpen?12:18
        width:Math.max(40,bar.width-x-(bar.editorOpen?52:16));height:38
        font.pixelSize:bar.editorOpen?16:22;font.weight:Font.DemiBold;color:bar.colors.text
        textFormat:Text.PlainText;text:bar.displayed.displayName||bar.displayed.name||"";elide:Text.ElideRight
    }
    RowLayout {
        visible:!bar.collapsed&&bar.displayedRef.domain==="territorial"
        anchors.right:parent.right;anchors.bottom:parent.bottom;anchors.rightMargin:12;anchors.bottomMargin:10;spacing:0
        UiButton {
            id:colorTrigger;objectName:"objectColorTrigger";Layout.preferredWidth:40;Layout.preferredHeight:40
            focusPolicy:Qt.NoFocus;enabled:bar.editorOpen?!!editor.objectProperties.colorEnabled:!!bar.previewData.editable;Accessible.name:"색상 · "+(editor.objectProperties.colorLabel||"")
            contentItem:Rectangle {color:bar.displayed.color||"transparent";border.color:bar.colors.border;radius:3;implicitWidth:22;implicitHeight:22}
            onClicked:{bar.targetPreview();colorPicker.showAt(colorTrigger)}
        }
        UiButton {objectName:"selectionVisibilityButton";symbol:bar.displayed.visible===false?"eyeOff":"eye";ToolTip.text:"표시·숨김";focusPolicy:Qt.NoFocus;onClicked:{bar.targetPreview();editor.toggleSelectionVisibility()}}
        UiButton {objectName:"selectionLockButton";symbol:bar.displayed.locked?"lock":"unlock";ToolTip.text:editor.objectProperties.lockLabel||"잠금";enabled:bar.editorOpen?!!editor.objectProperties.lockEnabled:!!bar.previewData.lockEnabled;focusPolicy:Qt.NoFocus;onClicked:{bar.targetPreview();editor.toggleObjectLock()}}
    }
    UiButton {objectName:bar.editorOpen?"toggleObjectEditor":"";visible:bar.editorOpen;symbol:"close";x:bar.width-width-12;y:12;ToolTip.text:"편집 닫기";focusPolicy:Qt.NoFocus;onPressedChanged:if(pressed)bar.navigationStarted();onClicked:{bar.targetPreview();bar.navigationStarted();bar.toggleEditor()}}
    HoverHandler {acceptedDevices:PointerDevice.Mouse|PointerDevice.TouchPad;onHoveredChanged:bar.previewHover=hovered}
    ObjectColorPicker {id:colorPicker}
}
