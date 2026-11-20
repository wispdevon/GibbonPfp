import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: win
    width: 1440; height: 940
    minimumWidth: 1024; minimumHeight: 720
    visible: true
    title: "HeadshotFlow — Portrait workspace"
    color: bg
    readonly property color bg: backend.dark ? "#151617" : "#efece6"
    readonly property color panel: backend.dark ? "#202224" : "#f8f6f2"
    readonly property color strong: backend.dark ? "#2a2d30" : "#e7e0d5"
    readonly property color ink: backend.dark ? "#f2f0ea" : "#17181b"
    readonly property color muted: backend.dark ? "#a6abb0" : "#626970"
    readonly property color line: backend.dark ? "#414447" : "#d3cec5"
    readonly property color reviewInk: backend.dark ? "#e5bb75" : "#886015"
    readonly property color accent: backend.buttonAccent === "blue" ? (backend.dark ? "#91B8D8" : "#315F86") : (backend.dark ? "#bfc9d1" : "#1d1f23")
    Component.onCompleted: if (backend.recoveryPending) recoveryDialog.open()
    property bool queueOpen: workspace.width >= 1100
    property bool adjustmentsOpen: workspace.width >= 900
    readonly property bool compactHeader: workspace.width < 670
    readonly property bool compact: workspace.height < 650
    readonly property string outputUrl: hasPhoto && backend.result.width !== undefined ? "image://photos/output?" + backend.revision : ""
    property int brushMode: 0
    property string fileAction: ""
    property string filter: "All photos"
    readonly property bool hasPhoto: backend.current >= 0
    readonly property bool hasSelection: backend.selectedCount > 0
    readonly property var queuePhotos: backend.items
    readonly property var visibleQueuePhotos: queuePhotos.filter(photo => matchesQueueFilter(photo.state))
    readonly property int visiblePhotoPosition: visibleQueuePhotos.findIndex(photo => photo.row === backend.current)
    readonly property int hiddenSelectedCount: queuePhotos.filter(photo => photo.selected && !matchesQueueFilter(photo.state)).length
    readonly property bool canNavigate: hasPhoto && !backend.busy && !backend.qualityConfirmationPending
                                        && !applyAllDialog.visible && !exportDialog.visible
    readonly property int previousPhotoRow: adjacentVisiblePhoto(-1)
    readonly property int nextPhotoRow: adjacentVisiblePhoto(1)
    readonly property int nextReviewPhotoRow: findNextReviewPhoto()
    function matchesQueueFilter(state: string): bool {
        return filter === "All photos" || state === filter
    }
    function adjacentVisiblePhoto(direction: int): int {
        for (let row = backend.current + direction; row >= 0 && row < queuePhotos.length; row += direction) {
            if (matchesQueueFilter(queuePhotos[row].state))
                return row
        }
        return -1
    }
    function findNextReviewPhoto(): int {
        for (let offset = 1; offset < queuePhotos.length; ++offset) {
            const row = (backend.current + offset) % queuePhotos.length
            if (queuePhotos[row].state === "Needs review")
                return row
        }
        return -1
    }
    palette.window: panel
    palette.windowText: ink
    palette.text: ink
    palette.buttonText: ink
    palette.base: panel
    palette.mid: line
    palette.button: strong
    palette.highlight: accent
    palette.highlightedText: backend.dark ? "#17181b" : "#ffffff"
    Overlay.overlay.transform: Scale { xScale: backend.uiScale / 100; yScale: backend.uiScale / 100 }
    font.family: "Inter"
    font.pixelSize: 13
    font.weight: Font.Medium

    component WorkLabel: Label { font.family: "Inter"; font.weight: Font.Medium }
    component WorkCheckBox: CheckBox { font.family: "Inter"; font.weight: Font.Medium }
    component WorkMenuItem: MenuItem {
        font.family: "Inter"; font.weight: Font.Medium
        horizontalPadding: 12
        implicitHeight: Math.max(32, Math.max(implicitContentHeight, implicitIndicatorHeight)
                                 + topPadding + bottomPadding)
    }
    component WorkMenu: Menu {
        id: menu
        required property Item trigger
        readonly property int edge: 4
        property rect triggerBounds
        function updateAnchor() {
            triggerBounds = trigger.mapToItem(parent, Qt.rect(0, 0, trigger.width, trigger.height))
        }
        parent: Overlay.overlay
        popupType: Popup.Item
        // The overlay shares the workspace transform, so fit the menu in logical workspace units.
        margins: -1
        width: {
            let naturalWidth = implicitWidth
            for (let row = 0; row < count; ++row) {
                const item = itemAt(row)
                if (item)
                    naturalWidth = Math.max(naturalWidth, item.implicitWidth + leftPadding + rightPadding)
            }
            return Math.min(naturalWidth, workspace.width - 2 * edge)
        }
        height: Math.min(implicitHeight, workspace.height - 2 * edge)
        x: Math.max(edge, Math.min(triggerBounds.x + triggerBounds.width - width, workspace.width - width - edge))
        y: {
            const below = triggerBounds.y + triggerBounds.height + edge
            const above = triggerBounds.y - height - edge
            const preferred = below + height <= workspace.height - edge ? below : above
            return Math.max(edge, Math.min(preferred, workspace.height - height - edge))
        }
        onAboutToShow: updateAnchor()
        Connections {
            target: workspace
            function onWidthChanged() { if (menu.visible) Qt.callLater(menu.updateAnchor) }
            function onHeightChanged() { if (menu.visible) Qt.callLater(menu.updateAnchor) }
        }
    }
    component WorkDialog: Dialog {
        id: workDialog
        font.family: "Inter"; font.weight: Font.Medium
        header: WorkLabel { text: workDialog.title; padding: 12; font.family: "Space Grotesk"; font.pixelSize: 16; font.weight: Font.Bold; color: ink }
    }
    component Action: Button {
        id: action
        font.family: "Inter"; font.weight: Font.DemiBold
        property bool primary: false
        property bool quiet: false
        implicitHeight: 40
        leftPadding: 14; rightPadding: 14
        font.pixelSize: 13
        Accessible.name: text
        contentItem: Text { text: action.text; font: action.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; color: action.primary ? (backend.dark ? "#17181b" : "white") : ink; opacity: action.enabled ? 1 : 0.45 }
        background: Rectangle { radius: 8; color: action.primary ? (action.down ? Qt.darker(accent, 1.2) : action.hovered ? Qt.lighter(accent, 1.15) : accent) : action.down || action.hovered || action.checked ? strong : action.quiet ? "transparent" : panel; border.color: action.activeFocus || action.hovered ? accent : action.quiet ? "transparent" : muted; border.width: action.activeFocus ? 2 : 1; opacity: action.enabled ? 1 : .6 }
    }
    component Command: Action {
        id: command
        required property string symbol
        property bool iconOnly: false
        property string hint: text
        leftPadding: 10; rightPadding: 10
        implicitWidth: Math.max(40, graphic.implicitWidth + leftPadding + rightPadding)
        ToolTip.visible: hovered || iconOnly && visualFocus
        ToolTip.text: hint
        contentItem: RowLayout {
            id: graphic
            spacing: 7
            CommandIcon {
                Layout.alignment: Qt.AlignVCenter
                symbol: command.symbol
                tone: command.primary ? (backend.dark ? "#17181b" : "white") : ink
                opacity: command.enabled ? 1 : 0.45
            }
            Text {
                visible: !command.iconOnly
                color: command.primary ? (backend.dark ? "#17181b" : "white") : ink
                opacity: command.enabled ? 1 : 0.45
                text: command.text; font: command.font
            }
        }
    }
    component CommandIcon: Canvas {
        required property string symbol
        required property color tone
        implicitWidth: 18; implicitHeight: 18
        onToneChanged: requestPaint()
        onSymbolChanged: requestPaint()
        onPaint: {
            const c = getContext("2d")
            c.clearRect(0, 0, width, height)
            c.strokeStyle = tone
            c.fillStyle = tone
            c.lineWidth = 1.4
            c.lineJoin = "round"
            c.lineCap = "round"
            const path = points => {
                c.beginPath()
                c.moveTo(points[0][0], points[0][1])
                for (let i = 1; i < points.length; ++i)
                    c.lineTo(points[i][0], points[i][1])
                c.stroke()
            }
            switch (symbol) {
            case "photo":
                c.strokeRect(2, 3, 14, 12)
                path([[3,13],[7,9],[10,12],[12,10],[15,13]])
                c.beginPath(); c.arc(12,6.5,1,0,Math.PI*2); c.fill()
                break
            case "folder":
                path([[2,14],[2,4],[7,4],[9,6],[16,6],[16,14],[2,14]])
                break
            case "sample":
                c.beginPath(); c.arc(9,6,3,0,Math.PI*2); c.stroke()
                c.beginPath(); c.arc(9,16,6,Math.PI,0); c.stroke()
                break
            case "queue":
                c.strokeRect(2,3,14,12)
                path([[7,3],[7,15]])
                path([[9,7],[13,7]])
                path([[9,11],[13,11]])
                break
            case "inspector":
                path([[3,5],[15,5]]); path([[3,9],[15,9]]); path([[3,13],[15,13]])
                path([[7,3],[7,7]]); path([[11,7],[11,11]]); path([[6,11],[6,15]])
                break
            case "more":
                for (const x of [4,9,14]) {
                    c.beginPath(); c.arc(x,9,1,0,Math.PI*2); c.fill()
                }
                break
            case "previous": path([[11,4],[6,9],[11,14]]); break
            case "next": path([[7,4],[12,9],[7,14]]); break
            case "clear":
                path([[5,5],[13,13]]); path([[13,5],[5,13]])
                break
            case "approve": path([[3,9],[7,13],[15,5]]); break
            case "review":
                path([[2,4],[12,4],[16,9],[12,14],[2,14],[2,4]])
                path([[9,6],[9,9]])
                c.beginPath(); c.arc(9,11.5,.75,0,Math.PI*2); c.fill()
                break
            case "rotateLeft":
            case "rotateRight":
                c.save()
                if (symbol === "rotateLeft") { c.translate(18,0); c.scale(-1,1) }
                c.beginPath(); c.arc(9,9,5.25,.48*Math.PI,1.82*Math.PI); c.stroke()
                path([[13,2.5],[13,6.5],[9,6.5]])
                c.restore()
                break
            case "export":
                path([[3,11],[3,15],[15,15],[15,11]])
                path([[9,3],[9,11]]); path([[6,8],[9,11],[12,8]])
                break
            case "exportAs":
                path([[4,2],[10,2],[14,6],[14,16],[4,16],[4,2]])
                path([[10,2],[10,6],[14,6]])
                path([[7,9],[11,9]]); path([[7,12],[11,12]])
                break
            }
        }
    }
    component Caption: WorkLabel { color: ink; font.family: "Space Grotesk"; font.pixelSize: 16; font.weight: Font.Bold }
    component FieldLabel: WorkLabel { Layout.fillWidth: true; color: ink; font.pixelSize: 12; wrapMode: Text.WordWrap }
    component Choice: WorkspaceChoice {}

    Item {
        id: workspace
        width: win.width / scale; height: win.height / scale
        scale: backend.uiScale / 100
        transformOrigin: Item.TopLeft
        onWidthChanged: if (width < 1100 && win.queueOpen && win.adjustmentsOpen) win.queueOpen=false
    Canvas {
        id: canvasGrid
        anchors.fill: parent
        onPaint: { let c = getContext("2d"); c.clearRect(0,0,width,height); c.fillStyle = backend.dark ? "rgba(242,240,234,0.08)" : "rgba(23,24,27,0.09)"; for(let y=9;y<height;y+=18) for(let x=9;x<width;x+=18) {c.beginPath();c.arc(x,y,.8,0,Math.PI*2);c.fill();} }
        Connections { target: backend; function onChanged() { canvasGrid.requestPaint() } }
    }
    ColumnLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            objectName: "commandHeader"
            Layout.fillWidth: true; Layout.preferredHeight: 54; color: panel
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: workspace.width < 900 ? 4 : 6
                WorkLabel {
                    text: "HeadshotFlow"; color: ink; font.family: "Space Grotesk"
                    font.pixelSize: workspace.width < 900 ? 16 : 20; font.weight: Font.Bold
                    Layout.rightMargin: 8
                }
                Command { objectName: "addPhotos"; text: "Add photos…"; symbol: "photo"; enabled: !backend.busy; onClicked: photos.open() }
                Command { objectName: "addFolder"; text: "Add folder…"; symbol: "folder"; iconOnly: compactHeader; quiet: true; enabled: !backend.busy; onClicked: folders.open() }
                Command { objectName: "loadSample"; text: "Load sample"; symbol: "sample"; iconOnly: compactHeader; quiet: true; enabled: !backend.busy; onClicked: backend.loadSample() }
                Item { Layout.fillWidth: true }
                Command {
                    objectName: "toggleQueue"; text: queueOpen ? "Hide queue" : "Show queue"
                    symbol: "queue"; iconOnly: true; quiet: true; checkable: true; checked: queueOpen
                    onClicked: {queueOpen=checked;if(queueOpen&&workspace.width<1100)adjustmentsOpen=false}
                }
                Command {
                    objectName: "toggleInspector"; text: adjustmentsOpen ? "Hide inspector" : "Show inspector"
                    symbol: "inspector"; iconOnly: true; quiet: true; checkable: true; checked: adjustmentsOpen
                    enabled: hasPhoto
                    onClicked: {adjustmentsOpen=checked;if(adjustmentsOpen&&workspace.width<1100)queueOpen=false}
                }
                Command {
                    id: workspaceActions
                    objectName: "workspaceActions"; text: "Workspace actions"; symbol: "more"; iconOnly: true; quiet: true
                    onClicked: workspaceMenu.open()
                    WorkMenu {
                        id: workspaceMenu; objectName: "workspaceMenu"; trigger: workspaceActions
                        WorkMenuItem { text: "Open session…"; enabled: !backend.busy; onTriggered: {fileAction="loadSession";jsonOpen.open()} }
                        WorkMenuItem { text: "Save session…"; enabled: hasPhoto&&!backend.busy; onTriggered: {fileAction="saveSession";jsonSave.open()} }
                        MenuSeparator {}
                        WorkMenuItem { text: "Load preset…"; enabled: hasPhoto&&!backend.busy; onTriggered: {fileAction="loadPreset";jsonOpen.open()} }
                        WorkMenuItem { text: "Save preset…"; enabled: hasPhoto&&!backend.busy; onTriggered: {fileAction="savePreset";jsonSave.open()} }
                        MenuSeparator {}
                        WorkMenuItem { text: "Appearance…"; onTriggered: appearanceDialog.open() }
                        WorkMenuItem { text: "Models…"; enabled: !backend.busy; onTriggered: modelDialog.open() }
                    }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: line }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 12; spacing: 12
            Rectangle {
                visible: queueOpen; Layout.preferredWidth: 224; Layout.fillHeight: true; radius: 12; color: panel; border.color: line
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 10; spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Caption { text: "Photo queue" }
                        Item { Layout.fillWidth: true }
                        WorkLabel {
                            objectName: "queueSelectionCount"
                            text: backend.selectedCount + " of " + queuePhotos.length + " selected"
                            color: muted; font.pixelSize: 11
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Choice { objectName: "queueFilter"; Layout.fillWidth: true; model: ["All photos", "Needs review", "Ready", "Failed", "Exported"]; onActivated: win.filter=currentText }
                        Command {
                            id: queueActions
                            objectName: "queueActions"; text: "Queue actions"; symbol: "more"; iconOnly: true; quiet: true
                            onClicked: queueMenu.open()
                            WorkMenu {
                                id: queueMenu; objectName: "queueMenu"; trigger: queueActions
                                WorkMenuItem { objectName: "prepareSelected"; text: "Prepare selected"; enabled: hasSelection&&!backend.busy; onTriggered: backend.batch("",false) }
                                WorkMenuItem { objectName: "removeSelected"; text: "Remove selected"; enabled: hasSelection&&!backend.busy; onTriggered: backend.removeSelected() }
                            }
                        }
                    }
                    WorkLabel {
                        objectName: "queueHiddenSelectionCount"
                        visible: hiddenSelectedCount > 0
                        text: hiddenSelectedCount + " selected outside this filter"
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: muted; font.pixelSize: 11
                    }
                    ListView {
                        id: photoList; objectName: "photoQueue"
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 6
                        model: visibleQueuePhotos
                        currentIndex: visiblePhotoPosition
                        keyNavigationEnabled: false
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: queueRow
                            required property var modelData
                            width: ListView.view.width; height: 58
                            radius: 8; color: backend.current === modelData.row ? strong : panel; border.color: backend.current === modelData.row ? line : "transparent"
                            MouseArea { anchors.fill: parent; enabled: canNavigate; onClicked: backend.current = queueRow.modelData.row }
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 7; spacing: 7
                                WorkCheckBox { checked: modelData.selected; implicitWidth: 26; enabled: !backend.busy; Accessible.name: "Select "+modelData.name; onClicked: backend.select(modelData.row, checked) }
                                Rectangle { Layout.preferredWidth: 30; Layout.preferredHeight: 40; radius: 4; color: bg; clip: true
                                    Image { objectName: "queueThumbnail"; anchors.fill: parent; source: modelData.thumb; fillMode: Image.PreserveAspectCrop; cache: false }
                                }
                                ColumnLayout { Layout.fillWidth: true; spacing: 5
                                    WorkLabel { text: modelData.name; Layout.fillWidth: true; elide: Text.ElideMiddle; color: ink; font.pixelSize: 11; font.weight: Font.DemiBold }
                                    WorkLabel { text: modelData.state; color: modelData.state==="Needs review" ? reviewInk : muted; font.pixelSize: 10 }
                                }
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 10
                Rectangle {
                    id: emptyCanvas
                    visible: !hasPhoto; Layout.fillWidth: true; Layout.fillHeight: true
                    color: strong; radius: 12
                    DropArea { anchors.fill: parent; onDropped: drop => {if(drop.hasUrls&&!backend.busy)backend.add(drop.urls,true)} }
                    WorkLabel {
                        anchors.centerIn: parent
                        text: "Drop photos or a folder here"
                        color: muted
                    }
                }
                SplitView {
                    id: panes; objectName: "panes"
                    visible: hasPhoto
                    Layout.fillWidth: true; Layout.fillHeight: true
                    orientation: Qt.Horizontal
                    handle: Rectangle { implicitWidth: 10; color: SplitHandle.pressed ? accent : "transparent"
                        Rectangle { anchors.centerIn: parent; width: 2; height: 40; radius: 1; color: SplitHandle.hovered ? accent : muted }
                    }
                    ColumnLayout {
                        objectName: "leftPane"
                        SplitView.preferredWidth: (panes.width-10) / 2; SplitView.minimumWidth: 90
                        spacing: 8
                        Caption { text: "Preview / Crop"; Layout.preferredHeight: 38; verticalAlignment: Text.AlignVCenter; Layout.fillWidth: true; elide: Text.ElideRight }
                        Rectangle {
                            Layout.fillWidth: true; Layout.fillHeight: true; color: strong; radius: 12; clip: true
                            Image {
                                id: portrait; anchors.fill: parent; anchors.margins: 12
                                fillMode: Image.PreserveAspectFit; cache: false
                                source: outputUrl ? "image://photos/source?"+backend.revision : ""
                                Rectangle { anchors.fill: parent; color: "#88000000"; visible: !!outputUrl }
                                Rectangle {
                                    id: cropBox
                                    visible: !!outputUrl
                                    readonly property real ox: (portrait.width-portrait.paintedWidth)/2
                                    readonly property real oy: (portrait.height-portrait.paintedHeight)/2
                                    x: ox+(backend.result.cropX||0)*portrait.paintedWidth
                                    y: oy+(backend.result.cropY||0)*portrait.paintedHeight
                                    width: (backend.result.cropW||1)*portrait.paintedWidth
                                    height: (backend.result.cropH||1)*portrait.paintedHeight
                                    Checkerboard { anchors.fill: parent }
                                    Image { objectName: "cropOutput"; anchors.fill: parent; source: outputUrl; cache: false; fillMode: Image.Stretch }
                                    Rectangle { anchors.fill: parent; color: "transparent"; border.color: cropInput.activeFocus ? accent : "white"; border.width: 2 }
                                    Repeater { model: 2; Rectangle { required property int index; x: (index+1)*cropBox.width/3; height: cropBox.height; width: 1; color: "#66ffffff" } }
                                    Repeater { model: 2; Rectangle { required property int index; y: (index+1)*cropBox.height/3; width: cropBox.width; height: 1; color: "#66ffffff" } }
                                    MouseArea {
                                        id: cropInput; objectName: "cropInput"
                                        anchors.fill: parent; enabled: !backend.busy; cursorShape: Qt.SizeAllCursor
                                        activeFocusOnTab: true
                                        Accessible.name: "Crop frame: drag or use arrow keys"
                                        property real startX; property real startY
                                        function restore() {
                                            cropBox.x=Qt.binding(()=>cropBox.ox+(backend.result.cropX||0)*portrait.paintedWidth)
                                            cropBox.y=Qt.binding(()=>cropBox.oy+(backend.result.cropY||0)*portrait.paintedHeight)
                                        }
                                        onPressed: mouse => {forceActiveFocus();startX=mouse.x;startY=mouse.y}
                                        onPositionChanged: mouse => {if(pressed){cropBox.x=Math.max(cropBox.ox,Math.min(portrait.width-cropBox.ox-cropBox.width,cropBox.x+mouse.x-startX));cropBox.y=Math.max(cropBox.oy,Math.min(portrait.height-cropBox.oy-cropBox.height,cropBox.y+mouse.y-startY));}}
                                        onReleased: {backend.setCrop((cropBox.x-cropBox.ox)/portrait.paintedWidth,(cropBox.y-cropBox.oy)/portrait.paintedHeight,cropBox.width/portrait.paintedWidth,cropBox.height/portrait.paintedHeight);restore()}
                                        onCanceled: restore()
                                        Keys.onLeftPressed: backend.nudgeCrop(-.01,0)
                                        Keys.onRightPressed: backend.nudgeCrop(.01,0)
                                        Keys.onUpPressed: backend.nudgeCrop(0,-.01)
                                        Keys.onDownPressed: backend.nudgeCrop(0,.01)
                                    }
                                }
                            }
                            DropArea { anchors.fill: parent; onDropped: drop => {if(drop.hasUrls&&!backend.busy)backend.add(drop.urls,true)} }
                        }
                    }
                    ColumnLayout {
                        SplitView.fillWidth: true; SplitView.minimumWidth: 90; spacing: 8
                        RowLayout {
                            id: resultHeader
                            Layout.fillWidth: true; Layout.preferredHeight: 38
                            Caption { text: "Result"; Layout.fillWidth: true; elide: Text.ElideRight }
                            WorkLabel {
                                visible: resultHeader.width >= 135 && !!backend.result.width
                                text: backend.result.width + " × " + backend.result.height + " px"
                                font.family: "Geist Mono"; color: muted; font.pixelSize: 11
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true; Layout.fillHeight: true; color: strong; radius: 12; clip: true
                            Image {
                                id: resultImage; objectName: "resultImage"
                                anchors.fill: parent; anchors.margins: 12; fillMode: Image.PreserveAspectFit; cache: false
                                source: outputUrl
                                Checkerboard { z: -1; x: (resultImage.width-resultImage.paintedWidth)/2; y: (resultImage.height-resultImage.paintedHeight)/2; width: resultImage.paintedWidth; height: resultImage.paintedHeight }
                                MouseArea {
                                    objectName: "brushInput"
                                    x: (resultImage.width-resultImage.paintedWidth)/2; y: (resultImage.height-resultImage.paintedHeight)/2
                                    width: resultImage.paintedWidth; height: resultImage.paintedHeight
                                    enabled: !!outputUrl && adjustmentsOpen && inspectorTabs.currentIndex === 1
                                             && brushMode !== 0 && !backend.busy && backend.settings.background !== "off"
                                    cursorShape: Qt.CrossCursor
                                    property var points: []
                                    function point(mouse) {return [Math.max(0,Math.min(1,mouse.x/width)),Math.max(0,Math.min(1,mouse.y/height))]}
                                    onPressed: mouse => {points=[point(mouse)]}
                                    onPositionChanged: mouse => {if(pressed)points.push(point(mouse))}
                                    onReleased: backend.stroke(points,brushMode===1,.025)
                                    onCanceled: points=[]
                                }
                            }
                            WorkLabel { anchors.centerIn: parent; visible: !outputUrl; text: "Export preview"; color: muted }
                            Rectangle { anchors.fill: parent; visible: backend.busy && !backend.qualityConfirmationPending; color: backend.dark ? "#aa151617" : "#aaefece6"
                                Column { anchors.centerIn: parent; spacing: 8
                                    BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: backend.busy }
                                    WorkLabel { text: "Updating…"; color: ink }
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    visible: hasPhoto; Layout.fillWidth: true
                    WorkLabel {
                        objectName: "photoPosition"
                        Layout.fillWidth: true; Layout.minimumWidth: 55
                        text: "Photo " + (backend.current + 1) + " of " + queuePhotos.length
                              + (filter === "All photos" ? "" : visiblePhotoPosition === -1 ? " · Outside filter"
                                 : " · " + (visiblePhotoPosition + 1) + " of " + visibleQueuePhotos.length + " visible")
                        color: muted; font.family: "Geist Mono"; font.pixelSize: 11; wrapMode: Text.WordWrap
                    }
                    Command {
                        id: previousPhoto; objectName: "previousPhoto"; text: "Previous photo"
                        symbol: "previous"; quiet: true; iconOnly: true
                        enabled: canNavigate && previousPhotoRow >= 0
                        hint: text + " (" + previousPhotoShortcut.nativeText + ")"
                        onClicked: backend.current = previousPhotoRow
                    }
                    Command {
                        id: nextPhoto; objectName: "nextPhoto"; text: "Next photo"
                        symbol: "next"; quiet: true; iconOnly: true
                        enabled: canNavigate && nextPhotoRow >= 0
                        hint: text + " (" + nextPhotoShortcut.nativeText + ")"
                        onClicked: backend.current = nextPhotoRow
                    }
                    Command {
                        id: nextReviewPhoto; objectName: "nextReviewPhoto"; text: "Next review"
                        symbol: "review"; quiet: true; iconOnly: workspace.width < 900
                        enabled: canNavigate && nextReviewPhotoRow >= 0
                        hint: text + " (" + nextReviewPhotoShortcut.nativeText + ")"
                        onClicked: backend.current = nextReviewPhotoRow
                    }
                        id: photoActions
                    Command {
                        objectName: "photoActions"; text: "Photo actions"; symbol: "more"; iconOnly: true; quiet: true
                        onClicked: photoMenu.open()
                        WorkMenu {
                            id: photoMenu; objectName: "photoMenu"; trigger: photoActions
                            WorkMenuItem { objectName: "applyAll"; text: "Apply to all…"; enabled: hasPhoto&&!backend.busy; onTriggered: applyAllDialog.open() }
                            WorkMenuItem { text: "Fully automatic batch"; checkable: true; checked: backend.settings.automatic; enabled: hasPhoto&&!backend.busy; onTriggered: backend.set("automatic",checked) }
                            MenuSeparator {}
                            WorkMenuItem { text: "Undo"; enabled: hasPhoto&&!backend.busy; onTriggered: backend.undo() }
                            WorkMenuItem { text: "Reset photo"; enabled: hasPhoto&&!backend.busy; onTriggered: backend.reset() }
                            WorkMenuItem { text: "Refresh preview"; enabled: hasPhoto&&!backend.busy; onTriggered: backend.preview() }
                        }
                    }
                }
                RowLayout {
                    visible: !!backend.result.warnings; Layout.fillWidth: true; spacing: 8
                    Rectangle {
                        Layout.fillWidth: true; implicitHeight: reviewText.implicitHeight+16; radius: 8; color: panel; border.color: line
                        WorkLabel {
                            id: reviewText; objectName: "reviewReason"
                            anchors.fill: parent; anchors.margins: 8
                            text: (backend.result.review === true ? "Needs review · " : "") + (backend.result.warnings || "")
                            color: backend.result.review === true ? reviewInk : muted
                            wrapMode: Text.WordWrap; font.pixelSize: 11; maximumLineCount: compact ? 3 : 4; elide: Text.ElideRight
                            ToolTip.visible: warningHover.hovered
                            ToolTip.text: text
                            HoverHandler { id: warningHover }
                        }
                    }
                    Command {
                        objectName: "approveFraming"; text: compact ? "Approve framing" : "Approve current framing"
                        symbol: "approve"; visible: backend.result.review === true
                        enabled: !backend.busy; onClicked: backend.approve()
                    }
                }
            }
            Rectangle {
                visible: hasPhoto && adjustmentsOpen; Layout.preferredWidth: 270; Layout.fillHeight: true; radius: 12; color: panel; border.color: line
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 8
                    TabBar {
                        id: inspectorTabs; objectName: "inspectorTabs"
                        Layout.fillWidth: true
                        currentIndex: 0
                        background: Rectangle { color: strong; radius: 8 }
                        InspectorTab { objectName: "frameTab"; text: "Frame" }
                        InspectorTab { objectName: "refineTab"; text: "Refine" }
                    }
                    StackLayout {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        currentIndex: inspectorTabs.currentIndex
                        ScrollView {
                            id: frameScroll; objectName: "frameScroll"
                            clip: true; contentWidth: availableWidth
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                            ColumnLayout {
                                width: frameScroll.availableWidth; spacing: 10
                                enabled: hasPhoto && !backend.busy
                                WorkCheckBox { text: "Automatic portrait crop"; checked: backend.settings.autoCrop; onClicked: {backend.set("autoCrop",checked);backend.preview()} }
                                RowLayout { Layout.fillWidth: true; FieldLabel { text: "Headroom" } Item {Layout.fillWidth: true} WorkLabel { text: Math.round(backend.settings.headroom*100)+"%"; color: muted; font.family: "Geist Mono"; font.pixelSize: 11 } }
                                Slider { Layout.fillWidth: true; enabled: backend.settings.autoCrop; from: 0; to: .25; stepSize: .01; value: backend.settings.headroom; Accessible.name: "Headroom"; onMoved: {backend.set("headroom",value);if(!pressed)backend.preview()} onPressedChanged: if(!pressed)backend.preview() }
                                RowLayout { Layout.fillWidth: true
                                    FieldLabel { text: "Crop zoom" }
                                    WorkLabel { objectName: "cropZoomPercent"; text: Math.round(cropZoom.pressed ? cropZoom.value : (backend.settings.cropZoom||100))+"%"; color: ink; font.family: "Geist Mono"; font.pixelSize: 12 }
                                }
                                Slider { id: cropZoom; objectName: "cropZoom"; Layout.fillWidth: true; from: 40; to: 100; stepSize: 1; value: backend.settings.cropZoom||100; Accessible.name: "Crop zoom percentage"; onMoved: if(!pressed)backend.setCropZoom(value); onPressedChanged: if(!pressed)backend.setCropZoom(value) }
                                RowLayout { Layout.fillWidth: true
                                    FieldLabel { text: "Rotate" }
                                    Command { text: "Rotate left"; symbol: "rotateLeft"; quiet: true; iconOnly: true; onClicked: {backend.set("rotation",backend.settings.rotation-90);backend.preview()} }
                                    Command { text: "Rotate right"; symbol: "rotateRight"; quiet: true; iconOnly: true; onClicked: {backend.set("rotation",backend.settings.rotation+90);backend.preview()} }
                                }
                            }
                        }
                        ScrollView {
                            id: refineScroll; objectName: "refineScroll"
                            clip: true; contentWidth: availableWidth
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                            ColumnLayout {
                                width: refineScroll.availableWidth; spacing: 10
                                enabled: hasPhoto && !backend.busy
                                RowLayout { Layout.fillWidth: true; FieldLabel { text: "Perceived brightness" } Item {Layout.fillWidth: true} WorkLabel { text: Number(backend.settings.brightness).toFixed(2); color: muted; font.family: "Geist Mono"; font.pixelSize: 11 } }
                                Slider { objectName: "brightnessSlider"; Layout.fillWidth: true; from: -1; to: 1; stepSize: .02; value: backend.settings.brightness; Accessible.name: "Perceptual brightness"; onMoved: backend.set("brightness",value); onPressedChanged: if(!pressed)backend.preview() }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Command {
                                        text: backend.settings.reference ? "Change reference" : "Match reference…"
                                        symbol: "photo"; Layout.fillWidth: true; onClicked: referencePhoto.open()
                                    }
                                    Command {
                                        text: "Remove reference"; symbol: "clear"; quiet: true; iconOnly: true
                                        visible: !!backend.settings.reference
                                        onClicked: {backend.set("reference","");backend.preview()}
                                    }
                                }
                                FieldLabel { text: "Background removal" }
                                Choice { Layout.fillWidth: true; model: ["Off · keep original", "Fast · portrait", "High Quality · portrait"]; currentIndex: ["off","fast","quality"].indexOf(backend.settings.background); onActivated: {backend.set("background",["off","fast","quality"][currentIndex]);backend.preview()} }
                                RowLayout {
                                    visible: backend.settings.background !== "off"; Layout.fillWidth: true
                                    Choice {
                                        Layout.fillWidth: true
                                        model: ["Brush off", "Keep brush", "Remove brush"]
                                        currentIndex: brushMode; onActivated: brushMode = currentIndex
                                    }
                                    Command {
                                        objectName: "clearStrokes"; text: "Clear strokes"; symbol: "clear"
                                        quiet: true; iconOnly: true; visible: backend.settings.strokes.length > 0
                                        onClicked: {backend.set("strokes",[]);backend.preview()}
                                    }
                                }
                                RowLayout { visible: backend.settings.background!=="off"; Layout.fillWidth: true
                                    FieldLabel { text: "Edge feather" }
                                    Item { Layout.fillWidth: true }
                                    WorkLabel { objectName: "featherValue"; text: Number(featherSlider.pressed ? featherSlider.value : backend.settings.feather).toFixed(1)+" px"; color: ink; font.family: "Geist Mono"; font.pixelSize: 12 }
                                }
                                Slider { id: featherSlider; objectName: "featherSlider"; visible: backend.settings.background!=="off"; Layout.fillWidth: true; from: 0; to: 10; stepSize: .5; value: backend.settings.feather; Accessible.name: "Edge feather"; onMoved: backend.set("feather",value); onPressedChanged: if(!pressed)backend.preview() }
                                Disclosure { text: "RAW development"; Layout.fillWidth: true
                                    content: ColumnLayout { Layout.fillWidth: true; spacing: 10
                                        FieldLabel { text: "White balance" }
                                        Choice { Layout.fillWidth: true; model: ["Camera", "Daylight", "Cloudy", "Tungsten", "Custom"]; currentIndex: ["camera","daylight","cloudy","tungsten","custom"].indexOf(backend.settings.whiteBalance); onActivated: {backend.set("whiteBalance",currentText.toLowerCase());backend.preview()} }
                                        FieldLabel { text: "Temperature · "+backend.settings.temperature+" K" }
                                        Slider { Layout.fillWidth: true; from: 2000; to: 12000; stepSize: 100; value: backend.settings.temperature; enabled: backend.settings.whiteBalance==="custom"; Accessible.name: "RAW temperature"; onMoved: backend.set("temperature",value); onPressedChanged: if(!pressed)backend.preview() }
                                        FieldLabel { text: "Tint · "+Number(backend.settings.tint).toFixed(2) }
                                        Slider { Layout.fillWidth: true; from: .5; to: 2; stepSize: .05; value: backend.settings.tint; enabled: backend.settings.whiteBalance!=="camera"; Accessible.name: "RAW tint"; onMoved: backend.set("tint",value); onPressedChanged: if(!pressed)backend.preview() }
                                        FieldLabel { text: "Highlight recovery · "+backend.settings.highlight }
                                        Slider { Layout.fillWidth: true; from: 0; to: 9; stepSize: 1; value: backend.settings.highlight; Accessible.name: "RAW highlight recovery"; onMoved: backend.set("highlight",value); onPressedChanged: if(!pressed)backend.preview() }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        WorkLabel { objectName: "autosaveError"; visible: backend.autosaveError.length > 0; text: backend.autosaveError; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; wrapMode: Text.WordWrap; color: ink; font.pixelSize: 12 }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 54; color: panel
            Rectangle { width: parent.width; height: 1; color: line }
            RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
                Rectangle { width: 7; height: 7; radius: 4; color: backend.busy ? "#bb934f" : "#819483" }
                WorkLabel { text: backend.processingProgress.length ? backend.processingProgress : backend.message; Layout.fillWidth: true; color: muted; font.pixelSize: 11; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                Action { visible: backend.busy; text: "Cancel"; onClicked: backend.cancel() }
                WorkLabel {
                    visible: hasPhoto && !backend.busy && text.length > 0
                    text: backend.quickExportScope
                    color: muted; font.pixelSize: 11
                }
                Command { objectName: "exportAs"; text: "Export as…"; symbol: "exportAs"; enabled: hasPhoto&&!backend.busy; onClicked: exportDialog.begin(false) }
                Command {
                    objectName: "quickExport"; text: "Quick Export"; symbol: "export"; primary: true
                    enabled: hasPhoto&&!backend.busy
                    hint: backend.quickExportDescription
                    Accessible.description: hint
                    onClicked: exportDialog.begin(true)
                }
            }
        }
    }
    component InspectorTab: TabButton {
        id: tab
        font.family: "Inter"; font.weight: tab.checked ? Font.DemiBold : Font.Medium
        implicitHeight: 36
        Accessible.name: text
        contentItem: Text { text: tab.text; font: tab.font; color: ink; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        background: Rectangle {
            color: tab.checked || tab.hovered ? panel : "transparent"
            border.color: tab.activeFocus ? accent : tab.checked ? line : "transparent"
            border.width: tab.activeFocus ? 2 : 1
            radius: 8
        }
    }
    component Checkerboard: Canvas {
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            let c=getContext("2d"); c.clearRect(0,0,width,height)
            for(let y=0;y<height;y+=10) for(let x=0;x<width;x+=10) {
                c.fillStyle=(x/10+y/10)%2 ? "#aaaaaa" : "#dddddd"
                c.fillRect(x,y,10,10)
            }
        }
    }
    WorkDialog {
        id: appearanceDialog; objectName: "appearanceDialog"; title: "Appearance"; anchors.centerIn: parent; modal: true; width: Math.min(380, workspace.width-32); standardButtons: Dialog.Close
        ColumnLayout { width: parent.width; spacing: 14
            FieldLabel { text: "Theme" }
            Choice { Layout.fillWidth: true; model: ["Light", "Dark"]; currentIndex: backend.dark ? 1 : 0; onActivated: backend.dark=currentIndex===1 }
            FieldLabel { text: "UI scale" }
            Choice { objectName: "appearanceScale"; Layout.fillWidth: true; model: ["80%", "90%", "100%", "110%", "125%", "150%"]; currentIndex: [80,90,100,110,125,150].indexOf(backend.uiScale); onActivated: backend.uiScale=[80,90,100,110,125,150][currentIndex] }
            FieldLabel { text: "Button accent" }
            Choice { Layout.fillWidth: true; model: ["Graphite", "Blue"]; currentIndex: backend.buttonAccent==="blue" ? 1 : 0; onActivated: backend.buttonAccent=currentIndex===1 ? "blue" : "graphite" }
        }
    }
    component Disclosure: ColumnLayout {
        property string text
        property alias content: holder.data
        spacing: 10
        Action { id: toggle; text: (checked ? "− " : "+ ")+parent.text; checkable: true; Layout.fillWidth: true }
        ColumnLayout { id: holder; visible: toggle.checked; Layout.fillWidth: true }
    }
    ExportDialog { id: exportDialog; controller: backend; hiddenCheckedCount: win.hiddenSelectedCount }
    FileDialog { id: photos; title: "Add photos to the queue"; fileMode: FileDialog.OpenFiles; nameFilters: ["Photos (*.jpg *.jpeg *.png *.webp *.heic *.heif *.tif *.tiff *.dng *.cr2 *.cr3 *.nef *.arw *.raf *.orf *.rw2 *.pef *.raw)","All files (*)"]; onAccepted: backend.add(selectedFiles) }
    FolderDialog { id: folders; title: "Add folder (including subfolders)"; onAccepted: backend.add([selectedFolder], true) }
    FileDialog { id: referencePhoto; title: "Choose reference portrait"; onAccepted: backend.setReference(selectedFile) }
    FileDialog { id: jsonOpen; title: "Open "+(fileAction==="loadPreset"?"preset":"session"); nameFilters: ["JSON (*.json)"]; onAccepted: {if(fileAction==="loadPreset")backend.loadPreset(selectedFile);else backend.loadSession(selectedFile)} }
    FileDialog { id: jsonSave; title: "Save "+(fileAction==="savePreset"?"preset":"session"); fileMode: FileDialog.SaveFile; defaultSuffix: "json"; nameFilters: ["JSON (*.json)"]; onAccepted: {if(fileAction==="savePreset")backend.savePreset(selectedFile);else backend.saveSession(selectedFile)} }
    WorkDialog {
        id: applyAllDialog; objectName: "applyAllConfirmation"; title: "Apply settings to all photos?"
        anchors.centerIn: parent; modal: true; width: Math.min(460, workspace.width - 32)
        closePolicy: Popup.CloseOnEscape
        onOpened: cancelApplyAll.forceActiveFocus()
        onAccepted: backend.applyAll()
        ColumnLayout {
            width: parent.width; spacing: 16
            WorkLabel {
                text: "This copies the current settings to every queued photo, including unchecked photos. It resets manual framing, mask brush strokes, and approvals. Future imports will use these settings."
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: ink
            }
            RowLayout {
                Layout.fillWidth: true
                Action { id: cancelApplyAll; objectName: "cancelApplyAll"; text: "Cancel"; Layout.fillWidth: true; onClicked: applyAllDialog.reject() }
                Action { objectName: "confirmApplyAll"; text: "Apply to all"; primary: true; Layout.fillWidth: true; onClicked: applyAllDialog.accept() }
            }
        }
    }
    WorkDialog { id: recoveryDialog; objectName: "recoveryDialog"; title: "Restore workspace?"; anchors.centerIn: parent; modal: true; width: Math.min(460, workspace.width - 32); closePolicy: Popup.NoAutoClose
        ColumnLayout { width: parent.width; spacing: 16
            WorkLabel { text: backend.recoveryMessage; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: ink }
            WorkLabel { visible: backend.autosaveError.length > 0; text: backend.autosaveError; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: muted }
            RowLayout { Layout.fillWidth: true
                Action { text: "Start fresh"; Layout.fillWidth: true; onClicked: backend.resolveRecovery(false) }
                Action { text: "Restore"; primary: true; enabled: backend.recoveryRestorable; Layout.fillWidth: true; onClicked: backend.resolveRecovery(true) }
            }
        }
    }
    Connections {
        target: backend
        function onRecoveryChanged() { if (!backend.recoveryPending) recoveryDialog.close() }
        function onHighQualityConfirmationRequested() { qualityConfirmation.open() }
        function onChanged() { if (!backend.qualityConfirmationPending && qualityConfirmation.visible) qualityConfirmation.close() }
    }
    WorkDialog {
        id: qualityConfirmation; objectName: "qualityConfirmation"
        title: "Load High Quality?"; anchors.centerIn: parent; modal: true
        width: Math.min(440, workspace.width-32)
        closePolicy: Popup.CloseOnEscape
        onRejected: backend.confirmHighQuality(false)
        onClosed: if (backend.qualityConfirmationPending) backend.confirmHighQuality(false)
        ColumnLayout { width: parent.width; spacing: 14
            WorkLabel { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: ink
                text: "Load the portrait model into RAM? Allow roughly 8–10 GB of available memory. Compatible NVIDIA GPUs are used automatically; otherwise processing uses the CPU. GPU memory needs vary, and CPU processing may take tens of seconds per image. These are estimates, not guaranteed requirements."
            }
            WorkLabel { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: ink
                text: "Once loaded, the model stays in memory until you release models or close HeadshotFlow—even when you switch to Fast or Off. You will not be asked again while it remains loaded."
            }
            RowLayout { Layout.fillWidth: true
                Action { objectName: "cancelQuality"; text: "Cancel"; Layout.fillWidth: true; onClicked: qualityConfirmation.reject() }
                Action { objectName: "acceptQuality"; text: "Load High Quality"; primary: true; Layout.fillWidth: true; onClicked: {backend.confirmHighQuality(true);qualityConfirmation.close()} }
            }
        }
    }
    WorkDialog { id: modelDialog; objectName: "modelDialog"; title: "Local portrait models"; anchors.centerIn: parent; modal: true; width: 420; standardButtons: Dialog.Close
        contentItem: ScrollView {
            id: modelScroll; clip: true; contentWidth: availableWidth
            implicitHeight: Math.min(modelContents.implicitHeight, workspace.height - 140)
            ColumnLayout { id: modelContents; width: modelScroll.availableWidth; spacing: 16
            WorkLabel { text: "Face detection and Fast are bundled. High Quality downloads 973 MB and can use about 7 GB of memory; allow tens of seconds per photo on CPU. Photos stay on this device."; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: ink }
            WorkLabel { text: "Processing device"; color: ink }
            Choice { objectName: "processingDevice"; Layout.fillWidth: true; model: ["Automatic", "CPU"]; currentIndex: backend.processingDevice === "cpu" ? 1 : 0; enabled: !backend.busy; onActivated: backend.processingDevice = currentIndex === 1 ? "cpu" : "automatic" }
            WorkLabel { visible: backend.cpuOverride; text: "GIBBON_INFERENCE_DEVICE=cpu overrides this preference until the app exits."; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: muted }
            WorkLabel { objectName: "inferenceStatus"; text: backend.inferenceStatus; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: ink }
            WorkLabel { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: muted; text: "Automatic tries NVIDIA CUDA when this runtime supports it. Published packages include CPU-only inference. This engine does not enable AMD/Intel GPU or Apple GPU acceleration. CPU fallback keeps processing available." }
            WorkLabel { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: muted; text: "CUDA can fail if a driver or CUDA/cuDNN library is missing or incompatible, or GPU memory is insufficient. These are possible causes; Technical details shows the actual runtime error. Release loaded models to retry after correcting the problem." }
            WorkCheckBox { id: runtimeDetails; text: "Technical details" }
            WorkLabel { visible: runtimeDetails.checked; text: backend.inferenceDetails; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: muted }
            Action { objectName: "releaseModels"; text: "Release loaded models"; Layout.fillWidth: true; enabled: !backend.busy; onClicked: backend.releaseModels() }
            WorkLabel { text: backend.modelDescription(); color: muted; font.family: "Geist Mono"; font.pixelSize: 12 }
            WorkLabel { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: muted; text: backend.highQualityLoaded ? "High Quality is loaded. Release loaded models to free its session." : "High Quality will ask for confirmation before its first load." }
            Action { text: "Download High Quality · 973 MB"; Layout.fillWidth: true; onClicked: {modelDialog.close();backend.installModel("quality")} }
            Action { text: "Repair face detector"; Layout.fillWidth: true; onClicked: {modelDialog.close();backend.installModel("face")} }
            Action { text: "Repair Fast model"; Layout.fillWidth: true; onClicked: {modelDialog.close();backend.installModel("fast")} }
            }
        }
    }
    } // scaled workspace
    Shortcut { sequences: [StandardKey.Open]; enabled: !backend.busy && !exportDialog.visible; onActivated: photos.open() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: canNavigate; onActivated: backend.undo() }
    Shortcut { sequence: "Ctrl+Return"; enabled: canNavigate; onActivated: backend.preview() }
    Shortcut { id: previousPhotoShortcut; sequence: "Alt+Left"; enabled: previousPhoto.enabled; onActivated: backend.current = previousPhotoRow }
    Shortcut { id: nextPhotoShortcut; sequence: "Alt+Right"; enabled: nextPhoto.enabled; onActivated: backend.current = nextPhotoRow }
    Shortcut { id: nextReviewPhotoShortcut; sequence: "Alt+Shift+R"; enabled: nextReviewPhoto.enabled; onActivated: backend.current = nextReviewPhotoRow }
}
