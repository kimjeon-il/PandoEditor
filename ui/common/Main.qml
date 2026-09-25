import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as Native
import "../desktop" as Desktop
import Pandoeditor.Windowing 1.0

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
        width: window.width
        height: expanded ? contentHeight+12 : 26
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
    readonly property bool desktopFrameEnabled: nativeFrame.active
    property bool maximized: visibility === Window.Maximized
    flags: Qt.Window
    WindowsFrame {
        id: nativeFrame
        objectName: "windowsFrame"
        window: window
        enabled: !editor.mobileMode && Qt.platform.os === "windows"
        blocked: unsaved.visible || errorDialog.visible || recoveryDialog.visible || webReport.visible || gisPanel.visible || gisExportPanel.visible
        caption: desktopTitleBar
        minimizeButton: minimizeWindowButton
        maximizeButton: maximizeWindowButton
        closeButton: closeWindowButton
    }

    property bool webImportFlowActive: false
    property var gisExportSelection: []
    function requestWebImport() {
        webImportFlowActive=true
        webOpenDialog.open()
    }
    function beginWebImport(url) {
        webImportFlowActive=true
        webReport.extraError=""
        editor.prepareWebImport(url)
        webReport.open()
    }
    function cancelWebImportFlow() {
        editor.cancelWebImport()
        webReport.close()
        webImportFlowActive=false
    }
    function finishWebImport(disposition, saveUrl) {
        if (editor.confirmWebImport(webReport.reviewedHash, disposition, saveUrl)) {
            webReport.close()
            webImportFlowActive=false
        }
    }
    property string pendingAction: ""
    property bool allowClose: false
    function toggleMaximized() {
        if (maximized) showNormal()
        else showMaximized()
    }
    Component.onCompleted: {
        if (editor.mobileMode)
            Qt.callLater(function() { editor.restorePrivateProject() })
        Qt.callLater(function() { if(editor.presentationRecoveryAvailable) presentationRecoveryDialog.open() })
    }
    function finishAction() {
        let action=pendingAction
        pendingAction=""
        if (action === "open" || action === "import") openDialog.open()
        if (action === "close") { allowClose=true; window.close() }
    }
    function requestAction(action) {
        if (action !== "open" && action !== "import" && !editor.contentEditState.active && !editor.geometryEditState.active && !editor.commitPendingEdits()) return
        pendingAction=action
        if (editor.dirty) unsaved.open()
        else finishAction()
    }
    function requestSave(asNew) {
        if (!editor.contentEditState.active && !editor.geometryEditState.active && !editor.commitPendingEdits()) return
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
        if (historicalPanel.visible) { historicalPanel.close(); return }
        if (gisPanel.visible) { gisPanel.close(); return }
        if (gisExportPanel.visible) { gisExportPanel.close(); return }
        if (webReport.visible) { cancelWebImportFlow(); return }
        if (errorDialog.visible) { errorDialog.close(); return }
        if (recoveryDialog.visible) { recoveryDialog.close(); return }
        if (unsaved.visible) { unsaved.close(); pendingAction=""; return }
        if (workspace.dismissPopup()) return
        requestAction("close")
    }
    onClosing: function(close) {
        if (allowClose) return
        if (webReport.visible) { close.accepted=false; cancelWebImportFlow(); return }
        if (gisPanel.visible) { close.accepted=false; gisPanel.close(); return }
        if (gisExportPanel.visible) { close.accepted=false; gisExportPanel.close(); return }
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
        color: window.active ? "#f3f3f3" : "#fafafa"
        Rectangle {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: 1 / Screen.devicePixelRatio
            color: window.active ? "#dddddd" : "#e8e8e8"
        }
        Label {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.right: windowControls.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: window.title
            elide: Text.ElideRight
            color: window.active ? "#222222" : "#777777"
            font: nativeFrame.captionFont
        }
        Row {
            id: windowControls
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            Desktop.CaptionButton {
                id: minimizeWindowButton
                objectName: "minimizeWindowButton"
                captionAction: 1
                frame: nativeFrame
                windowActive: window.active
            }
            Desktop.CaptionButton {
                id: maximizeWindowButton
                objectName: "maximizeWindowButton"
                captionAction: 2
                frame: nativeFrame
                maximized: window.maximized
                windowActive: window.active
            }
            Desktop.CaptionButton {
                id: closeWindowButton
                objectName: "closeWindowButton"
                captionAction: 3
                frame: nativeFrame
                windowActive: window.active
            }
        }
    }
    Desktop.DesktopWorkspace {
        id: workspace
        anchors.left: parent.left
        anchors.top: desktopTitleBar.bottom
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        mobileMode: editor.mobileMode
        holdFieldCommits: window.webImportFlowActive || gisPanel.visible || gisExportPanel.visible
        onWebImportRequested: window.requestWebImport()
        onHistoricalLibraryRequested: historicalPanel.open()
        onGisImportRequested: gisPanel.open()
        onGisExportRequested: gisExportPanel.open()
        onOpenRequested: window.requestAction(editor.mobileMode ? "import" : "open")
        onSaveRequested: window.requestSave(false)
        onSaveAsRequested: editor.mobileMode ? window.requestExport() : window.requestSave(true)
    }
    Shortcut { sequences: [StandardKey.Undo]; enabled: editor.canUndo && !window.webImportFlowActive && !editor.colorEditOpen; onActivated: editor.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: editor.canRedo && !window.webImportFlowActive && !editor.colorEditOpen; onActivated: editor.redo() }
    Shortcut { sequence: StandardKey.Save; enabled: !window.webImportFlowActive && !editor.colorEditOpen; onActivated: window.requestSave(false) }
    Shortcut { sequence: StandardKey.Open; enabled: !window.webImportFlowActive && !editor.colorEditOpen; onActivated: window.requestAction(editor.mobileMode ? "import" : "open") }
    HistoricalLibraryPanel {
        id: historicalPanel
        onLibraryFileRequested: historicalFileDialog.open()
    }
    GisImportPanel {
        id: gisPanel
        onFileRequested: gisFileDialog.open()
    }
    GisExportPanel {
        id: gisExportPanel
        onDestinationRequested: function(format,selected) {
            window.gisExportSelection=selected
            if(format==="geojson-zip")gisZipSaveDialog.open()
            else gisPackageSaveDialog.open()
        }
    }
    Native.FileDialog {
        id: gisZipSaveDialog
        objectName: "gisZipSaveDialog"
        title: "GIS GeoJSON ZIP 저장"
        fileMode: Native.FileDialog.SaveFile
        defaultSuffix: "zip"
        nameFilters: ["GeoJSON ZIP (*.zip)"]
        onAccepted: editor.exportGisData(selectedFile,"geojson-zip",window.gisExportSelection)
    }
    Native.FileDialog {
        id: gisPackageSaveDialog
        objectName: "gisPackageSaveDialog"
        title: "GIS GeoPackage 저장"
        fileMode: Native.FileDialog.SaveFile
        defaultSuffix: "gpkg"
        nameFilters: ["GIS GeoPackage (*.gpkg)"]
        onAccepted: editor.exportGisData(selectedFile,"geopackage",window.gisExportSelection)
    }
    Native.FileDialog {
        id: gisFileDialog
        objectName: "gisFileDialog"
        title: "GIS 데이터 선택"
        nameFilters: ["GIS 데이터 (*.geojson *.json *.zip *.gpkg)"]
        onAccepted: editor.loadGisSource(selectedFile)
    }
    Native.FileDialog {
        id: historicalFileDialog
        title: "역사 라이브러리 선택"
        nameFilters: ["역사 라이브러리 JSON (*.json)"]
        onAccepted: editor.loadHistoricalLibrary(selectedFile)
    }
    Native.FileDialog {
        id: webOpenDialog
        objectName: "webOpenDialog"
        title: "웹 프로젝트 가져오기"
        nameFilters: ["판도연구소 웹 완전 저장본 (*.json)"]
        onAccepted: window.beginWebImport(selectedFile)
        onRejected: window.webImportFlowActive=false
    }
    Native.FileDialog {
        id: webSaveDialog
        objectName: "webImportSaveDialog"
        title: "기존 작업 저장"
        fileMode: Native.FileDialog.SaveFile
        defaultSuffix: "pando.json"
        nameFilters: ["Pandoeditor 프로젝트 (*.pando.json)"]
        onAccepted: window.finishWebImport("save",selectedFile)
    }
    WebImportDialog {
        id: webReport
        onCancelRequested: window.cancelWebImportFlow()
        onConfirmRequested: function(disposition) {
            if (disposition === "save" && !editor.mobileMode && !editor.hasFile()) webSaveDialog.open()
            else window.finishWebImport(disposition, "")
        }
        onSaveLocationRequested: webSaveDialog.open()
    }
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
            Button { objectName: "discardUnsaved"; text: "버리기"; DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole; onClicked: { if(editor.discardPresentationRecovery()){unsaved.close(); window.finishAction()} } }
            Button { objectName: "cancelUnsaved"; text: "취소"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: { unsaved.close(); window.pendingAction="" } }
        }
    }
    Dialog {
        id: errorDialog
        objectName: "errorDialog"
        anchors.centerIn: parent
        width: Math.min(460,window.width-24)
        title: "작업을 완료하지 못했습니다"
        modal: true
        standardButtons: Dialog.Ok
        property string message: ""
        contentItem: Label { textFormat:Text.PlainText; text: errorDialog.message; wrapMode: Text.WrapAnywhere }
    }
    Dialog {
        id: presentationRecoveryDialog
        objectName: "presentationRecoveryDialog"
        anchors.centerIn: parent; width: Math.min(360,window.width-24)
        modal: true; title: "자동저장 복구본"
        contentItem: Label { text: "이전 작업의 복구본을 열까요?"; wrapMode: Text.Wrap }
        footer: DialogButtonBox {
            Button { text:"복구"; onClicked:if(editor.restorePresentationRecovery())presentationRecoveryDialog.close() }
            Button { text:"복구본 버리기"; onClicked:if(editor.discardPresentationRecovery())presentationRecoveryDialog.close() }
            Button { text:"나중에"; onClicked:presentationRecoveryDialog.close() }
        }
    }
    Dialog {
        id: recoveryDialog
        objectName: "recoveryDialog"
        anchors.centerIn: parent
        width: Math.min(460,window.width-24)
        contentWidth: availableWidth
        contentHeight: recoveryText.implicitHeight
        height: topPadding + bottomPadding + contentHeight
                + (header ? header.implicitHeight + spacing : 0)
                + (footer ? footer.implicitHeight + spacing : 0)
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
        function onErrorOccurred(message) {
            if (window.webImportFlowActive) webReport.extraError=message
            else { errorDialog.message=message; errorDialog.open() }
        }
        function onPrivateRecoveryRequiredChanged() {
            if (editor.privateRecoveryRequired) {
                errorDialog.close()
                recoveryDialog.open()
            }
        }
    }
}
