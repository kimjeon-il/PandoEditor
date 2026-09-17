import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as Native
import "../desktop" as Desktop

ApplicationWindow {
    id: window
    objectName: "mainWindow"
    width: 1100
    height: 720
    minimumWidth: editor.mobileMode ? 0 : 360
    minimumHeight: editor.mobileMode ? 0 : 560
    visible: true
    title: (editor.dirty ? "* " : "") + editor.fileName + " — Pandoeditor " + Qt.application.version
    color: "#f3f5f7"
    footer: Label {
        objectName: "documentFormatNotice"
        property bool expanded: false
        height: expanded ? implicitHeight+12 : 26
        leftPadding: 8
        rightPadding: 8
        verticalAlignment: Text.AlignVCenter
        text: editor.documentNotice
        wrapMode: expanded ? Text.Wrap : Text.NoWrap
        elide: expanded ? Text.ElideNone : Text.ElideRight
        font.pixelSize: 11
        background: Rectangle { color: "#edf1f5" }
        TapHandler { onTapped: parent.expanded = !parent.expanded }
    }
    property bool desktopFrameEnabled: !editor.mobileMode && Qt.platform.os === "windows"
    property bool maximized: visibility === Window.Maximized
    flags: desktopFrameEnabled ? (Qt.FramelessWindowHint | Qt.Window) : Qt.Window

    property string pendingAction: ""
    property bool allowClose: false
    function toggleMaximized() {
        if (maximized) showNormal()
        else showMaximized()
    }
    Component.onCompleted: {
        if (editor.mobileMode)
            Qt.callLater(function() { editor.restorePrivateProject() })
    }
    function finishAction() {
        let action=pendingAction
        pendingAction=""
        if (action === "open" || action === "import") openDialog.open()
        if (action === "close") { allowClose=true; window.close() }
    }
    function requestAction(action) {
        if (action !== "open" && action !== "import" && !editor.commitPendingEdits()) return
        pendingAction=action
        if (editor.dirty) unsaved.open()
        else finishAction()
    }
    function requestSave(asNew) {
        if (!editor.commitPendingEdits()) return
        if (editor.mobileMode) {
            if (editor.savePrivate()) finishAction()
            else {
                pendingAction=""
                if (editor.privateRecoveryRequired) {
                    errorDialog.close()
                    recoveryDialog.open()
                }
            }
            return
        }
        if (!asNew && editor.hasFile()) {
            if (editor.save()) finishAction()
            else pendingAction=""
        } else saveDialog.open()
    }
    function requestExport() {
        if (!editor.mobileMode) { requestSave(true); return }
        if (editor.savePrivate()) exportDialog.open()
        else if (editor.privateRecoveryRequired) {
            errorDialog.close()
            recoveryDialog.open()
        }
    }
    function handleBack() {
        if (Qt.inputMethod.visible) { Qt.inputMethod.hide(); return }
        if (workspace.dismissPopup()) return
        if (errorDialog.visible) { errorDialog.close(); return }
        if (recoveryDialog.visible) { recoveryDialog.close(); return }
        if (unsaved.visible) { unsaved.close(); pendingAction=""; return }
        requestAction("close")
    }
    onClosing: function(close) {
        if (allowClose) return
        if (editor.mobileMode) { close.accepted=false; handleBack(); return }
        if (editor.dirty) { close.accepted=false; requestAction("close") }
    }
    Rectangle {
        id: desktopTitleBar
        objectName: "desktopTitleBar"
        visible: window.desktopFrameEnabled
        height: visible ? 32 : 0
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        z: 100
        color: "#f3f3f3"
        border.color: "#d5d5d5"
        border.width: 1
        Label {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.right: windowControls.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: window.title
            elide: Text.ElideRight
            color: "#222222"
            font.pixelSize: 13
        }
        MouseArea {
            objectName: "titleBarDragArea"
            anchors.fill: parent
            anchors.rightMargin: windowControls.width
            property point pressPoint
            property bool moving: false
            onPressed: function(mouse) {
                pressPoint = Qt.point(mouse.x, mouse.y)
                moving = false
            }
            onPositionChanged: function(mouse) {
                if (!pressed || moving || Math.hypot(mouse.x-pressPoint.x, mouse.y-pressPoint.y) < Qt.styleHints.startDragDistance) return
                moving = true
                if (window.maximized) {
                    let globalPoint = mapToGlobal(mouse.x, mouse.y)
                    let horizontalRatio = pressPoint.x / window.width
                    window.showNormal()
                    window.x = Math.round(globalPoint.x - window.width * horizontalRatio)
                    window.y = Math.round(globalPoint.y - pressPoint.y)
                }
                window.startSystemMove()
            }
            onDoubleClicked: function(mouse) {
                if (mouse.button === Qt.LeftButton) window.toggleMaximized()
            }
        }
        Row {
            id: windowControls
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            ToolButton {
                objectName: "minimizeWindowButton"
                Accessible.name: "최소화"
                width: 46
                height: parent.height
                text: "−"
                font.pixelSize: 17
                onClicked: window.showMinimized()
            }
            ToolButton {
                objectName: "maximizeWindowButton"
                Accessible.name: window.maximized ? "이전 크기로 복원" : "최대화"
                width: 46
                height: parent.height
                text: window.maximized ? "❐" : "□"
                font.pixelSize: 15
                onClicked: window.toggleMaximized()
            }
            ToolButton {
                objectName: "closeWindowButton"
                Accessible.name: "닫기"
                width: 46
                height: parent.height
                text: "×"
                font.pixelSize: 20
                onClicked: window.close()
                background: Rectangle { color: parent.hovered ? "#c42b1c" : "transparent" }
                contentItem: Text {
                    text: parent.text
                    color: parent.hovered ? "white" : "#222222"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font: parent.font
                }
            }
        }
    }
    component ResizeHandle: MouseArea {
        property int resizeEdges: 0
        enabled: window.desktopFrameEnabled && !window.maximized
        z: 200
        onPressed: function(mouse) {
            if (mouse.button === Qt.LeftButton) window.startSystemResize(resizeEdges)
        }
    }
    ResizeHandle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 6; resizeEdges: Qt.LeftEdge; cursorShape: Qt.SizeHorCursor }
    ResizeHandle { anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 6; resizeEdges: Qt.RightEdge; cursorShape: Qt.SizeHorCursor }
    ResizeHandle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 6; resizeEdges: Qt.TopEdge; cursorShape: Qt.SizeVerCursor }
    ResizeHandle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 6; resizeEdges: Qt.BottomEdge; cursorShape: Qt.SizeVerCursor }
    ResizeHandle { anchors.left: parent.left; anchors.top: parent.top; width: 8; height: 8; resizeEdges: Qt.LeftEdge | Qt.TopEdge; cursorShape: Qt.SizeFDiagCursor }
    ResizeHandle { anchors.right: parent.right; anchors.top: parent.top; width: 8; height: 8; resizeEdges: Qt.RightEdge | Qt.TopEdge; cursorShape: Qt.SizeBDiagCursor }
    ResizeHandle { anchors.left: parent.left; anchors.bottom: parent.bottom; width: 8; height: 8; resizeEdges: Qt.LeftEdge | Qt.BottomEdge; cursorShape: Qt.SizeBDiagCursor }
    ResizeHandle { anchors.right: parent.right; anchors.bottom: parent.bottom; width: 8; height: 8; resizeEdges: Qt.RightEdge | Qt.BottomEdge; cursorShape: Qt.SizeFDiagCursor }
    Desktop.DesktopWorkspace {
        id: workspace
        anchors.left: parent.left
        anchors.top: desktopTitleBar.bottom
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        mobileMode: editor.mobileMode
        onOpenRequested: window.requestAction(editor.mobileMode ? "import" : "open")
        onSaveRequested: window.requestSave(false)
        onSaveAsRequested: editor.mobileMode ? window.requestExport() : window.requestSave(true)
    }
    Shortcut { sequences: [StandardKey.Undo]; enabled: editor.canUndo; onActivated: editor.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: editor.canRedo; onActivated: editor.redo() }
    Shortcut { sequence: StandardKey.Save; onActivated: window.requestSave(false) }
    Shortcut { sequence: StandardKey.Open; onActivated: window.requestAction(editor.mobileMode ? "import" : "open") }
    Native.FileDialog {
        id: openDialog
        objectName: "openDialog"
        title: editor.mobileMode ? "프로젝트 가져오기" : "프로젝트 열기"
        nameFilters: ["Pandoeditor 프로젝트 (*.pando.json)", "JSON (*.json)"]
        onAccepted: editor.mobileMode ? editor.importProject(selectedFile) : editor.openFile(selectedFile)
    }
    Native.FileDialog {
        id: exportDialog
        objectName: "exportDialog"
        title: "프로젝트 내보내기"
        fileMode: Native.FileDialog.SaveFile
        defaultSuffix: "pando.json"
        nameFilters: ["Pandoeditor 프로젝트 (*.pando.json)"]
        onAccepted: editor.exportProject(selectedFile)
    }
    Native.FileDialog {
        id: saveDialog
        objectName: "saveDialog"
        title: "프로젝트 저장"
        fileMode: Native.FileDialog.SaveFile
        defaultSuffix: "pando.json"
        nameFilters: ["Pandoeditor 프로젝트 (*.pando.json)"]
        onAccepted: {
            if (editor.saveFile(selectedFile)) window.finishAction()
            else window.pendingAction=""
        }
        onRejected: window.pendingAction=""
    }
    Dialog {
        id: unsaved
        objectName: "unsavedDialog"
        anchors.centerIn: parent
        width: Math.min(360,window.width-24)
        title: "변경 내용을 저장할까요?"
        modal: true
        closePolicy: Popup.NoAutoClose
        contentItem: Label { text: "저장하지 않은 편집 내용이 있습니다."; wrapMode: Text.WordWrap }
        footer: DialogButtonBox {
            Button { objectName: "saveUnsaved"; text: "저장"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole; onClicked: { unsaved.close(); window.requestSave(false) } }
            Button { objectName: "discardUnsaved"; text: "버리기"; DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole; onClicked: { unsaved.close(); window.finishAction() } }
            Button { objectName: "cancelUnsaved"; text: "취소"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: { unsaved.close(); window.pendingAction="" } }
        }
    }
    Dialog {
        id: errorDialog
        anchors.centerIn: parent
        width: Math.min(460,window.width-24)
        title: "작업을 완료하지 못했습니다"
        modal: true
        standardButtons: Dialog.Ok
        property string message: ""
        contentItem: Label { text: errorDialog.message; wrapMode: Text.WrapAnywhere }
    }
    Dialog {
        id: recoveryDialog
        objectName: "recoveryDialog"
        anchors.centerIn: parent
        width: Math.min(460,window.width-24)
        contentWidth: availableWidth
        contentHeight: recoveryText.implicitHeight
        title: "저장 파일 복구 필요"
        modal: true
        closePolicy: Popup.NoAutoClose
        contentItem: Label {
            id: recoveryText
            width: recoveryDialog.availableWidth
            text: "손상된 기기 저장 파일을 보존했습니다. 현재 프로젝트로 덮어쓰려면 먼저 원본의 복구용 사본을 만든 뒤 허용을 선택하세요."
            wrapMode: Text.WordWrap
        }
        footer: DialogButtonBox {
            Button {
                objectName: "confirmRecovery"
                text: "복구용 사본 만들고 허용"
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                onClicked: if (editor.confirmPrivateRecovery()) recoveryDialog.close()
            }
            Button {
                objectName: "cancelRecovery"
                text: "취소"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: recoveryDialog.close()
            }
        }
    }
    Connections {
        target: editor
        function onErrorOccurred(message) { errorDialog.message=message; errorDialog.open() }
        function onPrivateRecoveryRequiredChanged() {
            if (editor.privateRecoveryRequired) {
                errorDialog.close()
                recoveryDialog.open()
            }
        }
    }
}
