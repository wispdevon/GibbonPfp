import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: dialog
    objectName: "exportDialog"
    required property var controller
    property int hiddenCheckedCount: 0
    property string scope: "current"
    property bool mixed: false
    property int sizeMode: 0
    property int quickHeight: 0
    property url archiveToReplace
    property var style: ({})
    property var defaults: ({})
    property string notice: ""
    property string error: ""
    property Item returnFocus
    readonly property bool zip: scope === "zip"
    readonly property bool perPhoto: scope !== "current" && mixed
    readonly property bool jpeg: style.format === "jpeg"
    readonly property real uiScale: controller.uiScale / 100
    readonly property color panel: controller.dark ? "#202224" : "#f8f6f2"
    readonly property color ink: controller.dark ? "#f2f0ea" : "#17181b"
    readonly property color muted: controller.dark ? "#a6abb0" : "#626970"
    readonly property color line: controller.dark ? "#414447" : "#d3cec5"
    readonly property color accent: controller.buttonAccent === "blue"
                                    ? (controller.dark ? "#91B8D8" : "#315F86")
                                    : (controller.dark ? "#bfc9d1" : "#1d1f23")
    readonly property color accentText: controller.dark ? "#17181b" : "#ffffff"
    readonly property string validation: !visible || perPhoto ? "" : controller.exportStyleError(style)
    readonly property var scopes: ["current", "checked", "zip"]

    function begin(quick: bool, initialScope = "", heightOverride = 0) {
        if (controller.busy || controller.current < 0)
            return
        if (quick && controller.quickExport(heightOverride))
            return
        defaults = controller.exportOptions(quick)
        scope = initialScope || defaults.scope
        mixed = initialScope ? scope !== "current" : defaults.mixed
        quickHeight = quick ? heightOverride : 0
        style = defaults.style
        sizeMode = sizeIndex()
        notice = defaults.notice
        error = ""
        returnFocus = ApplicationWindow.window ? ApplicationWindow.window.activeFocusItem : null
        contentScroll.contentItem.contentY = 0
        open()
    }
    function edit(fields: var) {
        style = Object.assign({}, style, fields)
        error = ""
    }
    function selectScope(row: int) {
        if (scope === "current" && row !== 0)
            mixed = true
        scope = scopes[row]
        notice = ""
        error = ""
    }
    function sizeIndex(): int {
        if (style.capped && (style.width === 0 || style.width === 768))
            return 0
        if (style.width === 360) return 5
        if (!style.capped) {
            if (style.width === 0) return 1
            if (style.width === 720) return 2
            if (style.width === 1080) return 3
        }
        return 4
    }
    function changeSize(row: int) {
        sizeMode = row
        const requested = [768, 0, 720, 1080, 600, 360][row]
        edit({ capped: row === 0, width: requested, height: requested / 3 * 4 })
    }
    function chooseDestination() {
        error = ""
        if (zip) {
            zipPicker.currentFolder = defaults.folder
            zipPicker.selectedFile = defaults.zipFile
            zipPicker.open()
        } else {
            folderPicker.currentFolder = defaults.folder
            folderPicker.open()
        }
    }
    function deliver(destination: url, confirmedReplacement: bool) {
        if (controller.startExport({ scope: scope, mixed: perPhoto, style: style,
                                     sizeOverrideHeight: quickHeight },
                                   destination, confirmedReplacement)) {
            close()
        } else {
            error = controller.message
        }
    }

    title: "Export as…"
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: parent
    width: Math.min(620, (ApplicationWindow.window?.width ?? 960) / uiScale - 32)
    height: Math.min(implicitHeight, (ApplicationWindow.window?.height ?? 720) / uiScale - 32)
    padding: 18
    font.family: "Inter"
    font.weight: Font.Medium
    font.pixelSize: 13
    palette.windowText: ink
    palette.text: ink
    palette.buttonText: ink
    palette.base: panel
    palette.mid: muted
    palette.button: panel
    palette.highlight: accent
    palette.highlightedText: accentText
    onOpened: scopePicker.forceActiveFocus()
    onClosed: if (returnFocus) returnFocus.forceActiveFocus()

    component FieldLabel: Label {
        Layout.fillWidth: true
        font.family: "Inter"
        font.weight: Font.Medium
        color: dialog.ink
        wrapMode: Text.WordWrap
    }
    component Tick: CheckBox {
        Layout.fillWidth: true
        font.family: "Inter"
        font.weight: Font.Medium
    }
    component Field: TextField {
        id: entry
        Layout.fillWidth: true
        implicitHeight: 38
        leftPadding: 12
        rightPadding: 12
        selectByMouse: true
        font.family: "Inter"
        font.weight: Font.Medium
        color: dialog.ink
        placeholderTextColor: dialog.muted
        background: Rectangle {
            radius: 8
            color: dialog.panel
            border.color: entry.activeFocus ? dialog.accent : dialog.muted
            border.width: entry.activeFocus ? 2 : 1
        }
    }
    component Command: Button {
        id: button
        property bool primary: false
        implicitHeight: 40
        leftPadding: 14
        rightPadding: 14
        font.family: "Inter"
        font.weight: Font.DemiBold
        contentItem: Text {
            text: button.text
            font: button.font
            color: button.primary ? dialog.accentText : dialog.ink
            opacity: button.enabled ? 1 : .45
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 8
            color: button.primary ? dialog.accent : dialog.panel
            border.color: button.activeFocus || button.hovered ? dialog.accent : dialog.muted
            border.width: button.activeFocus ? 2 : 1
            opacity: button.enabled ? 1 : .55
        }
    }
    background: Rectangle { color: dialog.panel; radius: 12; border.color: dialog.line }
    header: Label {
        text: dialog.title
        padding: 18
        bottomPadding: 10
        color: dialog.ink
        font.family: "Space Grotesk"
        font.weight: Font.Bold
        font.pixelSize: 20
    }
    contentItem: ScrollView {
        id: contentScroll
        implicitHeight: fields.implicitHeight
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: fields
            width: contentScroll.availableWidth
            spacing: 12
            GridLayout {
                Layout.fillWidth: true
                columns: dialog.perPhoto || dialog.quickHeight ? 1 : 2
                uniformCellWidths: true
                columnSpacing: 18
                rowSpacing: 8
                FieldLabel { text: "Photos" }
                FieldLabel { visible: !dialog.perPhoto && !dialog.quickHeight; text: "Output size" }

                WorkspaceChoice {
                    Layout.fillWidth: true
                    id: scopePicker
                    objectName: "exportScope"
                    Layout.preferredWidth: 1
                    Accessible.name: "Photos to export"
                    model: [
                        "Current photo",
                        "Checked photos (" + dialog.controller.selectedCount + ")",
                        "Whole queue ZIP (" + dialog.controller.items.length + ")"
                    ]
                    currentIndex: dialog.scopes.indexOf(dialog.scope)
                    onActivated: dialog.selectScope(currentIndex)
                }
                WorkspaceChoice {
                    Layout.fillWidth: true
                    objectName: "exportSize"
                    Layout.preferredWidth: 1
                    Accessible.name: "Output size"
                    visible: !dialog.perPhoto && !dialog.quickHeight
                    model: ["Up to 768 × 1024", "Source crop size", "Up to 720 × 960",
                            "Up to 1080 × 1440", "Custom limit", "Up to 360 × 480"]
                    currentIndex: dialog.sizeMode
                    onActivated: dialog.changeSize(currentIndex)
                }
            }
            FieldLabel {
                visible: dialog.quickHeight > 0
                text: "Quick Export size · Up to " + dialog.quickHeight + " px tall"
            }
            FieldLabel {
                visible: dialog.scope === "checked" &&
                         (dialog.controller.selectedCount === 0 || dialog.hiddenCheckedCount > 0)
                text: dialog.controller.selectedCount === 0 ? "Check photos in the queue to use this scope."
                    : dialog.hiddenCheckedCount + " checked photos are hidden by the queue filter."
                color: dialog.muted
                font.pixelSize: 12
            }
            Tick {
                objectName: "exportPerPhoto"
                visible: dialog.scope !== "current"
                text: "Use each photo’s output settings"
                checked: dialog.mixed
                onClicked: dialog.mixed = checked
            }
            RowLayout {
                visible: !dialog.perPhoto && !dialog.quickHeight && dialog.sizeMode === 4
                Layout.fillWidth: true
                FieldLabel { Layout.fillWidth: false; text: "Maximum width" }
                Field {
                    objectName: "exportWidth"
                    Layout.preferredWidth: 100
                    Accessible.name: "Maximum width in pixels"
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 0; top: 60000 }
                    text: dialog.style.width > 0 ? String(dialog.style.width) : ""
                    onTextEdited: {
                        const width = Number(text)
                        dialog.edit({ capped: false, width: width,
                                      height: width > 0 ? Math.round(width / 3 * 4) : 1 })
                    }
                }
                FieldLabel {
                    text: "× " + (dialog.style.height ?? 0) + " px"
                    font.family: "Geist Mono"
                    Accessible.name: "Calculated height in pixels"
                }
            }
            GridLayout {
                visible: !dialog.perPhoto
                Layout.fillWidth: true
                columns: 2
                uniformCellWidths: true
                columnSpacing: 18
                rowSpacing: 8
                FieldLabel { text: "Format" }
                FieldLabel { text: "Filename prefix" }
                WorkspaceChoice {
                    Layout.fillWidth: true
                    objectName: "exportFormat"
                    Accessible.name: "File format"
                    model: ["JPEG · solid backdrop", "PNG · supports transparency"]
                    currentIndex: dialog.jpeg ? 0 : 1
                    onActivated: dialog.edit({ format: currentIndex === 0 ? "jpeg" : "png" })
                }
                Field {
                    objectName: "exportPrefix"
                    Accessible.name: "Filename prefix"
                    placeholderText: "Optional"
                    text: dialog.style.prefix ?? ""
                    onTextEdited: dialog.edit({ prefix: text })
                }
                FieldLabel { visible: dialog.jpeg; text: "JPEG quality · " + dialog.style.quality }
                FieldLabel { visible: dialog.jpeg; text: "JPEG backdrop" }
                Slider {
                    objectName: "exportQuality"
                    visible: dialog.jpeg
                    Layout.fillWidth: true
                    Accessible.name: "JPEG quality"
                    from: 1
                    to: 100
                    stepSize: 1
                    value: dialog.style.quality ?? 80
                    onMoved: dialog.edit({ quality: Math.round(value) })
                }
                Command {
                    objectName: "exportBackdrop"
                    visible: dialog.jpeg
                    Layout.fillWidth: true
                    text: dialog.style.backgroundColor ?? "#ffffff"
                    Accessible.name: "JPEG backdrop color"
                    onClicked: {
                        backdrop.selectedColor = dialog.style.backgroundColor
                        backdrop.open()
                    }
                }
                Tick {
                    objectName: "exportSharpen"
                    text: "Sharpen for screen"
                    checked: dialog.style.sharpenScreen ?? false
                    onClicked: dialog.edit({ sharpenScreen: checked })
                }
                WorkspaceChoice {
                    Layout.fillWidth: true
                    objectName: "exportSharpening"
                    Accessible.name: "Screen sharpening level"
                    enabled: !!dialog.style.sharpenScreen
                    model: ["Low", "Standard", "High"]
                    currentIndex: ["low", "standard", "high"].indexOf(dialog.style.sharpening)
                    onActivated: dialog.edit({ sharpening: ["low", "standard", "high"][currentIndex] })
                }
            }
            FieldLabel {
                Layout.fillWidth: true
                color: dialog.muted
                font.pixelSize: 12
                text: dialog.perPhoto ? (dialog.quickHeight ? "Each photo keeps its other output settings."
                                                            : "This export uses the output settings already assigned to each photo.")
                      : "Editor preview · " + (dialog.controller.settings.format === "png" ? "PNG" : "JPEG")
                        + (dialog.controller.result.width
                           ? " · " + dialog.controller.result.width + " × " + dialog.controller.result.height : "")
            }
            FieldLabel {
                objectName: "exportNotice"
                visible: text.length > 0
                text: dialog.validation || dialog.error || dialog.notice
            }
        }
    }
    footer: RowLayout {
        spacing: 8
        Command {
            objectName: "saveExportPreset"
            Layout.leftMargin: 18
            text: "Save preset…"
            visible: !dialog.perPhoto && !dialog.quickHeight
            enabled: !dialog.validation
            onClicked: presetPicker.open()
        }
        Item { Layout.fillWidth: true }
        Command { objectName: "cancelExport"; text: "Cancel"; onClicked: dialog.reject() }
        Command {
            objectName: "chooseExportDestination"
            Layout.topMargin: 10
            Layout.bottomMargin: 16
            Layout.rightMargin: 18
            text: dialog.zip ? "Save ZIP…" : "Choose folder…"
            primary: true
            enabled: !dialog.validation && (dialog.scope !== "checked" || dialog.controller.selectedCount > 0)
            onClicked: dialog.chooseDestination()
        }
    }
    FolderDialog {
        id: folderPicker
        title: "Choose the export folder"
        onAccepted: dialog.deliver(selectedFolder, false)
    }
    FileDialog {
        id: zipPicker
        title: "Save the whole queue as ZIP"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zip"
        nameFilters: ["ZIP archive (*.zip)"]
        options: FileDialog.DontConfirmOverwrite
        onAccepted: {
            if (dialog.controller.archiveExists(selectedFile)) {
                dialog.archiveToReplace = selectedFile
                replaceArchive.open()
            } else {
                dialog.deliver(selectedFile, false)
            }
        }
    }
    Dialog {
        id: replaceArchive
        objectName: "replaceArchive"
        title: "Replace existing ZIP?"
        modal: true
        anchors.centerIn: parent
        width: Math.min(390, dialog.width)
        font.family: "Inter"
        header: Label {
            text: replaceArchive.title
            padding: 16
            font.family: "Space Grotesk"
            font.weight: Font.Bold
            font.pixelSize: 16
            color: dialog.ink
        }
        background: Rectangle { color: dialog.panel; radius: 12; border.color: dialog.line }
        contentItem: FieldLabel { text: "The selected filename already exists." }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            Command {
                text: "Cancel"
                Layout.topMargin: 10
                Layout.bottomMargin: 12
                onClicked: replaceArchive.reject()
            }
            Command {
                text: "Replace ZIP"
                primary: true
                Layout.rightMargin: 12
                onClicked: replaceArchive.accept()
            }
        }
        onAccepted: dialog.deliver(dialog.archiveToReplace, true)
    }
    FileDialog {
        id: presetPicker
        title: "Save export preset"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["JSON (*.json)"]
        onAccepted: dialog.controller.saveExportPreset(selectedFile, dialog.style)
    }
    ColorDialog {
        id: backdrop
        title: "JPEG backdrop"
        onAccepted: dialog.edit({ backgroundColor: selectedColor.toString() })
    }
}
