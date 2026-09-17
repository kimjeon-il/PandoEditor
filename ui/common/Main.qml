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

    property string pendingAction: ""
    property bool allowClose: false
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
        if (!(editor.mobileMode && action === "import") && !editor.commitPendingEdits()) return
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
    Desktop.DesktopWorkspace {
        id: workspace
        anchors.fill: parent
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
        title: "저장 파일 복구 필요"
        modal: true
        closePolicy: Popup.NoAutoClose
        contentItem: Label {
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
