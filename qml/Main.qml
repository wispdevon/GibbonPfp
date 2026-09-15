import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: win
    width: 1440; height: 940
    minimumWidth: 1024; minimumHeight: 720
    visible: true
    title: "GibbonPfp — Portrait workspace"
    color: bg
    readonly property color bg: backend.dark ? "#151617" : "#efece6"
    readonly property color panel: backend.dark ? "#202224" : "#f8f6f2"
    readonly property color strong: backend.dark ? "#2a2d30" : "#e7e0d5"
    readonly property color ink: backend.dark ? "#f2f0ea" : "#17181b"
    readonly property color muted: backend.dark ? "#a6abb0" : "#626970"
    readonly property color line: backend.dark ? "#414447" : "#d3cec5"
    readonly property color accent: backend.buttonAccent === "blue" ? (backend.dark ? "#91B8D8" : "#315F86") : (backend.dark ? "#bfc9d1" : "#1d1f23")
    property int viewMode: 0
    property bool queueOpen: workspace.width >= 1100
    property bool adjustmentsOpen: workspace.width >= 900
    readonly property bool compact: workspace.height < 650
    readonly property string outputUrl: hasPhoto && backend.result.width !== undefined ? "image://photos/output?" + backend.revision : ""
    property int brushMode: 0
    property string fileAction: ""
    property string folderAction: "import"
    property string filter: "All photos"
    readonly property bool hasPhoto: backend.current >= 0
    palette.window: panel
    palette.windowText: ink
    palette.text: ink
    palette.buttonText: ink
    palette.base: panel
    palette.button: strong
    palette.highlight: accent
    palette.highlightedText: backend.dark ? "#17181b" : "#ffffff"
    Overlay.overlay.transform: Scale { xScale: backend.uiScale / 100; yScale: backend.uiScale / 100 }
    font.family: "Inter"
    font.pixelSize: 13

    component Action: Button {
        id: action
        property bool primary: false
        implicitHeight: 40
        leftPadding: 14; rightPadding: 14
        font.pixelSize: 13
        Accessible.name: text
        contentItem: Text { text: action.text; font: action.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; color: action.primary ? (backend.dark ? "#17181b" : "white") : ink; opacity: action.enabled ? 1 : 0.45 }
        background: Rectangle { radius: 8; color: action.primary ? (action.down ? Qt.darker(accent, 1.2) : action.hovered ? Qt.lighter(accent, 1.15) : accent) : action.down ? strong : action.hovered ? strong : panel; border.color: action.activeFocus || action.hovered ? accent : muted; border.width: action.activeFocus ? 2 : 1; opacity: action.enabled ? 1 : .6 }
    }
    component Caption: Label { color: ink; font.family: "Space Grotesk"; font.pixelSize: 16; font.weight: Font.Bold }
    component FieldLabel: Label { Layout.fillWidth: true; color: ink; font.pixelSize: 12; wrapMode: Text.WordWrap }
    component Rule: Rectangle { Layout.fillWidth: true; height: 2; color: line }
    component Entry: TextField { implicitHeight: 38; color: ink; placeholderTextColor: muted; selectByMouse: true; font.pixelSize: 12; background: Rectangle { color: panel; radius: 8; border.color: parent.activeFocus ? accent : line } }
    component Choice: ComboBox { implicitHeight: 38; font.pixelSize: 12; palette.button: panel; palette.text: ink; palette.buttonText: ink; background: Rectangle { color: panel; radius: 8; border.color: parent.activeFocus ? accent : line } }

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
            Layout.fillWidth: true; Layout.preferredHeight: compact ? 64 : 88; color: panel
            RowLayout {
                anchors.fill: parent; anchors.margins: compact ? 12 : 22; spacing: 14
                Rectangle { width: 42; height: 42; radius: 10; color: accent
                    Text { anchors.centerIn: parent; text: "g"; font.family: "Space Grotesk"; font.pixelSize: 34; font.weight: Font.Bold; color: backend.dark ? bg : panel }
                }
                ColumnLayout { spacing: 1
                    Label { text: "GibbonPfp"; font.family: "Space Grotesk"; font.pixelSize: 26; font.weight: Font.Bold; color: ink }
                    Caption { text: "PORTRAIT WORKSPACE"; font.pixelSize: 9 }
                }
                Item { Layout.fillWidth: true }
                Action { text: "Workspace"; onClicked: workspaceMenu.popup()
                    Menu { id: workspaceMenu
                        MenuItem { text: "Open session…"; enabled: !backend.busy; onTriggered: {fileAction="loadSession";jsonOpen.open()} }
                        MenuItem { text: "Save session…"; enabled: hasPhoto&&!backend.busy; onTriggered: {fileAction="saveSession";jsonSave.open()} }
                        MenuItem { text: "Models…"; enabled: !backend.busy; onTriggered: modelDialog.open() }
                    }
                }
                Action { text: "Appearance"; onClicked: appearanceDialog.open() }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: line }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.margins: compact ? 12 : 20; Layout.bottomMargin: 10; spacing: 10
            ColumnLayout { visible: workspace.width >= 900; spacing: 3
                Label { text: "A good first impression."; font.family: "Space Grotesk"; font.pixelSize: 23; font.weight: Font.Bold; color: ink }
                Label { text: "Frame, refine, and export your portraits. All on your device."; color: muted; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            Action { text: queueOpen ? "Hide queue" : "Queue"; onClicked: {queueOpen=!queueOpen;if(queueOpen&&workspace.width<1100)adjustmentsOpen=false} }
            Action { text: adjustmentsOpen ? "Hide adjustments" : "Adjustments"; onClicked: {adjustmentsOpen=!adjustmentsOpen;if(adjustmentsOpen&&workspace.width<1100)queueOpen=false} }
            Action { text: "+ Add photos"; primary: true; enabled: !backend.busy; onClicked: photos.open() }
            Action { text: "Add folder"; enabled: !backend.busy; onClicked: {folderAction="import";folders.open()} }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.leftMargin: 20; Layout.rightMargin: 20; Layout.bottomMargin: 14; spacing: 16
            Rectangle {
                visible: queueOpen; Layout.preferredWidth: 235; Layout.fillHeight: true; radius: 12; color: panel; border.color: line
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 14; spacing: 12
                    RowLayout { Layout.fillWidth: true; Caption { text: "Photo queue" } Item {Layout.fillWidth: true} Label { text: backend.items.length.toString().padStart(2,"0"); color: muted; font.family: "Geist Mono"; font.pixelSize: 13 } }
                    Choice { Layout.fillWidth: true; model: ["All photos", "Needs review", "Ready", "Failed", "Exported"]; onActivated: win.filter=currentText }
                    ListView {
                        id: photoList; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 6; model: backend.items
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            readonly property bool shown: win.filter==="All photos" || modelData.state===win.filter
                            width: ListView.view.width; height: shown ? 83 : 0; visible: shown
                            radius: 8; color: backend.current===index ? strong : panel; border.color: backend.current===index ? line : "transparent"
                            MouseArea { anchors.fill: parent; enabled: !backend.busy; onClicked: backend.current=parent.index }
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 7; spacing: 7
                                CheckBox { checked: modelData.selected; implicitWidth: 26; enabled: !backend.busy; Accessible.name: "Select "+modelData.name; onClicked: backend.select(index,checked) }
                                Rectangle { width: 39; height: 52; radius: 4; color: bg; clip: true
                                    Label { anchors.centerIn: parent; text: "3:4"; color: muted; font.pixelSize: 10 }
                                    Image { anchors.fill: parent; source: modelData.state!=="Imported" ? modelData.thumb : ""; fillMode: Image.PreserveAspectCrop; cache: false }
                                }
                                ColumnLayout { Layout.fillWidth: true; spacing: 5
                                    Label { text: modelData.name; Layout.fillWidth: true; elide: Text.ElideMiddle; color: ink; font.pixelSize: 11; font.weight: Font.DemiBold }
                                    Label { text: modelData.state; color: modelData.state==="Needs review" ? (backend.dark?"#e5bb75":"#886015") : muted; font.pixelSize: 10 }
                                }
                            }
                        }
                    }
                    Label { visible: backend.items.length===0; text: "Your photos will appear here."; color: muted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Action { text: "Prepare selected"; Layout.fillWidth: true; enabled: hasPhoto&&!backend.busy; onClicked: backend.batch("",false) }
                    Action { text: "Remove selected"; Layout.fillWidth: true; enabled: hasPhoto&&!backend.busy; onClicked: backend.removeSelected() }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 10
                SplitView {
                    id: panes; objectName: "panes"
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
                            Label { anchors.centerIn: parent; width: parent.width-24; visible: !outputUrl; text: "Drop portraits here"; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; color: muted }
                            DropArea { anchors.fill: parent; onDropped: drop => {if(drop.hasUrls&&!backend.busy)backend.add(drop.urls,true)} }
                        }
                    }
                    ColumnLayout {
                        SplitView.fillWidth: true; SplitView.minimumWidth: 90; spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Caption { text: "Result"; visible: parent.width > 180 }
                            Choice { Layout.fillWidth: true; model: ["Result", "Mask"]; currentIndex: viewMode; onActivated: viewMode=currentIndex; Accessible.name: "Result inspection" }
                        }
                        Rectangle {
                            Layout.fillWidth: true; Layout.fillHeight: true; color: strong; radius: 12; clip: true
                            Image {
                                id: resultImage; objectName: "resultImage"
                                anchors.fill: parent; anchors.margins: 12; fillMode: Image.PreserveAspectFit; cache: false
                                source: outputUrl ? (viewMode===1 ? "image://photos/mask?"+backend.revision : outputUrl) : ""
                                Checkerboard { z: -1; x: (resultImage.width-resultImage.paintedWidth)/2; y: (resultImage.height-resultImage.paintedHeight)/2; width: resultImage.paintedWidth; height: resultImage.paintedHeight }
                                MouseArea {
                                    objectName: "brushInput"
                                    x: (resultImage.width-resultImage.paintedWidth)/2; y: (resultImage.height-resultImage.paintedHeight)/2
                                    width: resultImage.paintedWidth; height: resultImage.paintedHeight
                                    enabled: !!outputUrl && brushMode!==0 && !backend.busy && backend.settings.background!=="off"
                                    cursorShape: Qt.CrossCursor
                                    property var points: []
                                    function point(mouse) {return [Math.max(0,Math.min(1,mouse.x/width)),Math.max(0,Math.min(1,mouse.y/height))]}
                                    onPressed: mouse => {points=[point(mouse)]}
                                    onPositionChanged: mouse => {if(pressed)points.push(point(mouse))}
                                    onReleased: backend.stroke(points,brushMode===1,.025)
                                    onCanceled: points=[]
                                }
                            }
                            Label { anchors.centerIn: parent; visible: !outputUrl; text: "Export preview"; color: muted }
                            Rectangle { anchors.fill: parent; visible: backend.busy; color: backend.dark ? "#aa151617" : "#aaefece6"
                                Column { anchors.centerIn: parent; spacing: 8
                                    BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: backend.busy }
                                    Label { text: "Updating…"; color: ink }
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: backend.result.width ? backend.result.width+" × "+backend.result.height+" px · "+Math.round((backend.result.bytes||0)/1024)+" KB" : "360 × 480 px · 3:4 portrait"; font.family: "Geist Mono"; color: muted; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                    Action { text: "Undo"; enabled: hasPhoto&&!backend.busy; onClicked: backend.undo() }
                    Action { text: "Reset"; enabled: hasPhoto&&!backend.busy; onClicked: backend.reset() }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    Rectangle {
                        Layout.fillWidth: true; implicitHeight: reviewText.implicitHeight+16; radius: 8; color: panel; border.color: line
                        Label { id: reviewText; anchors.fill: parent; anchors.margins: 8; text: backend.result.warnings || "Auto-crop aims for 8% headroom. Adjust framing to suit your portrait."; color: muted; wrapMode: Text.WordWrap; font.pixelSize: 11; maximumLineCount: compact ? 2 : 4; elide: Text.ElideRight
                            ToolTip.visible: warningHover.hovered
                            ToolTip.text: text
                            HoverHandler { id: warningHover }
                        }
                    }
                    Action { text: compact ? "Approve framing" : "Approve current framing"; visible: backend.result.review===true; enabled: !backend.busy; onClicked: backend.approve() }
                }
            }
            Rectangle {
                visible: adjustmentsOpen; Layout.preferredWidth: 300; Layout.fillHeight: true; radius: 12; color: panel; border.color: line
                ScrollView {
                    id: adjustmentsScroll; objectName: "adjustmentsScroll"
                    anchors.fill: parent; anchors.margins: 16; clip: true; contentWidth: availableWidth
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: adjustmentsScroll.availableWidth; spacing: 13; enabled: hasPhoto&&!backend.busy
                        Caption { text: "01 / Frame" }
                        CheckBox { text: "Automatic portrait crop"; checked: backend.settings.autoCrop; onClicked: {backend.set("autoCrop",checked);backend.set("crop",[0,0,0,0]);backend.preview()} }
                        RowLayout { Layout.fillWidth: true; FieldLabel { text: "Headroom" } Item {Layout.fillWidth: true} Label { text: Math.round(backend.settings.headroom*100)+"%"; color: muted; font.family: "Geist Mono"; font.pixelSize: 11 } }
                        Slider { Layout.fillWidth: true; enabled: backend.settings.autoCrop; from: 0; to: .25; stepSize: .01; value: backend.settings.headroom; Accessible.name: "Headroom"; onMoved: backend.set("headroom",value); onPressedChanged: if(!pressed){backend.set("crop",[0,0,0,0]);backend.preview()} }
                        FieldLabel { text: "Crop zoom · drag or use arrow keys on the left" }
                        Slider { Layout.fillWidth: true; from: 1; to: 4; value: 1; Accessible.name: "Crop zoom"; onPressedChanged: if(!pressed&&backend.result.sourceWidth){let r=backend.result;let h=Math.min(r.sourceHeight,r.sourceWidth/0.75)/value;let w=h*.75;let nw=w/r.sourceWidth;let nh=h/r.sourceHeight;backend.setCrop(Math.max(0,Math.min(1-nw,r.cropX+r.cropW/2-nw/2)),Math.max(0,Math.min(1-nh,r.cropY+r.cropH/2-nh/2)),nw,nh)} }
                        RowLayout { Layout.fillWidth: true
                            Action { text: "Rotate ↶"; Layout.fillWidth: true; onClicked: {backend.set("rotation",backend.settings.rotation-90);backend.set("crop",[0,0,0,0]);backend.preview()} }
                            Action { text: "Rotate ↷"; Layout.fillWidth: true; onClicked: {backend.set("rotation",backend.settings.rotation+90);backend.set("crop",[0,0,0,0]);backend.preview()} }
                        }
                        Rule {}
                        Caption { text: "02 / Refine" }
                        RowLayout { Layout.fillWidth: true; FieldLabel { text: "Perceived brightness" } Item {Layout.fillWidth: true} Label { text: Number(backend.settings.brightness).toFixed(2); color: muted; font.family: "Geist Mono"; font.pixelSize: 11 } }
                        Slider { objectName: "brightnessSlider"; Layout.fillWidth: true; from: -1; to: 1; stepSize: .02; value: backend.settings.brightness; Accessible.name: "Perceptual brightness"; onMoved: backend.set("brightness",value); onPressedChanged: if(!pressed)backend.preview() }
                        Label { text: "Gentle midtone adjustment with protected black and white endpoints."; color: muted; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 10 }
                        RowLayout { Layout.fillWidth: true; Action { text: backend.settings.reference ? "Change reference" : "Match reference…"; Layout.fillWidth: true; onClicked: referencePhoto.open() } Action { text: "Clear"; visible: !!backend.settings.reference; onClicked: {backend.set("reference","");backend.preview()} } }
                        FieldLabel { text: "Background removal" }
                        Choice { Layout.fillWidth: true; model: ["Off · keep original", "Fast · lightweight", "High Quality · portrait"]; currentIndex: ["off","fast","quality"].indexOf(backend.settings.background); onActivated: {backend.set("background",["off","fast","quality"][currentIndex]);backend.preview()} }
                        RowLayout { visible: backend.settings.background!=="off"; Layout.fillWidth: true; Choice { Layout.fillWidth: true; model: ["Inspect", "Keep brush", "Remove brush"]; currentIndex: brushMode; onActivated: {brushMode=currentIndex;viewMode=1} } Action { text: "Clear mask"; onClicked: {backend.set("strokes",[]);backend.preview()} } }
                        FieldLabel { visible: backend.settings.background!=="off"; text: "Edge feather · output pixels" }
                        Slider { visible: backend.settings.background!=="off"; Layout.fillWidth: true; from: 0; to: 10; stepSize: .5; value: backend.settings.feather; Accessible.name: "Mask feather"; onMoved: backend.set("feather",value); onPressedChanged: if(!pressed)backend.preview() }
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
                        Rule {}
                        Caption { text: "03 / Export" }
                        CheckBox { text: "Limit to 360 × 480"; checked: backend.settings.capped; onClicked: {backend.set("capped",checked);backend.setSize(0,0);backend.preview()} }
                        Choice { visible: !backend.settings.capped; Layout.fillWidth: true; model: ["Source crop size", "720 × 960", "1080 × 1440", "Custom…"]; currentIndex: backend.settings.width===0?0:backend.settings.width===720?1:backend.settings.width===1080?2:3; onActivated: {if(currentIndex===3)sizeDialog.open();else{let n=[0,240,360][currentIndex];backend.setSize(n*3,n*4);backend.preview()}} }
                        Choice { Layout.fillWidth: true; model: ["JPEG · solid background", "PNG · transparency"]; currentIndex: backend.settings.format==="png"?1:0; onActivated: {backend.set("format",currentIndex===0?"jpeg":"png");backend.preview()} }
                        RowLayout { visible: backend.settings.format==="jpeg"; Layout.fillWidth: true; FieldLabel { text: "Backdrop" } Action { text: backend.settings.backgroundColor; Layout.fillWidth: true; onClicked: backdrop.open() } }
                        FieldLabel { visible: backend.settings.format==="jpeg"; text: "JPEG quality · "+backend.settings.quality }
                        Slider { visible: backend.settings.format==="jpeg"; Layout.fillWidth: true; from: 1; to: 100; stepSize: 1; value: backend.settings.quality; Accessible.name: "JPEG quality"; onMoved: backend.set("quality",value); onPressedChanged: if(!pressed)backend.preview() }
                        Entry { Layout.fillWidth: true; placeholderText: "Filename prefix (optional)"; text: backend.settings.prefix; Accessible.name: "Filename prefix"; onEditingFinished: backend.set("prefix",text) }
                        CheckBox { text: "Fully automatic batch"; checked: backend.settings.automatic; onClicked: backend.set("automatic",checked) }
                        Label { text: backend.settings.automatic ? "Uses the largest face or center crop. Warnings are recorded." : "Uncertain crops wait for your review."; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: muted; font.pixelSize: 10 }
                        Action { text: "Apply settings to selected"; Layout.fillWidth: true; onClicked: backend.applySelected() }
                        RowLayout { Layout.fillWidth: true; Action { text: "Load preset"; Layout.fillWidth: true; onClicked: {fileAction="loadPreset";jsonOpen.open()} } Action { text: "Save preset"; Layout.fillWidth: true; onClicked: {fileAction="savePreset";jsonSave.open()} } }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: compact ? 56 : 70; color: panel
            Rectangle { width: parent.width; height: 1; color: line }
            RowLayout { anchors.fill: parent; anchors.margins: 16; spacing: 10
                Rectangle { width: 7; height: 7; radius: 4; color: backend.busy ? "#bb934f" : "#819483" }
                Label { text: backend.message; Layout.fillWidth: true; color: muted; font.pixelSize: 11; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                Action { visible: backend.busy; text: "Cancel"; onClicked: backend.cancel() }
                Action { text: workspace.width < 900 ? "Refresh" : "Refresh preview"; enabled: hasPhoto&&!backend.busy; onClicked: backend.preview() }
                Action { text: workspace.width < 900 ? "Export" : "Export current"; enabled: hasPhoto&&!backend.busy; onClicked: {folderAction="current";folders.open()} }
                Action { text: "Export selected"; primary: true; enabled: hasPhoto&&!backend.busy; onClicked: {folderAction="export";folders.open()} }
            }
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
    Dialog {
        id: appearanceDialog; objectName: "appearanceDialog"; title: "Appearance"; anchors.centerIn: parent; modal: true; width: Math.min(380, workspace.width-32); standardButtons: Dialog.Close
        ColumnLayout { width: parent.width; spacing: 14
            FieldLabel { text: "Theme" }
            Choice { Layout.fillWidth: true; model: ["Light", "Dark"]; currentIndex: backend.dark ? 1 : 0; onActivated: backend.dark=currentIndex===1 }
            FieldLabel { text: "UI scale" }
            Choice { Layout.fillWidth: true; model: ["80%", "90%", "100%", "110%", "125%", "150%"]; currentIndex: [80,90,100,110,125,150].indexOf(backend.uiScale); onActivated: backend.uiScale=[80,90,100,110,125,150][currentIndex] }
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
    FileDialog { id: photos; title: "Add portraits"; fileMode: FileDialog.OpenFiles; nameFilters: ["Photos (*.jpg *.jpeg *.png *.webp *.heic *.heif *.tif *.tiff *.dng *.cr2 *.cr3 *.nef *.arw *.raf *.orf *.rw2 *.pef *.raw)","All files (*)"]; onAccepted: backend.add(selectedFiles) }
    FolderDialog { id: folders; title: folderAction==="import" ? "Add folder (including subfolders)" : "Choose output folder"; onAccepted: {if(folderAction==="import")backend.add([selectedFolder],true);else if(folderAction==="current")backend.exportCurrent(selectedFolder);else backend.batch(selectedFolder,true)} }
    FileDialog { id: referencePhoto; title: "Choose reference portrait"; onAccepted: backend.setReference(selectedFile) }
    FileDialog { id: jsonOpen; title: "Open "+(fileAction==="loadPreset"?"preset":"session"); nameFilters: ["JSON (*.json)"]; onAccepted: {if(fileAction==="loadPreset")backend.loadPreset(selectedFile);else backend.loadSession(selectedFile)} }
    FileDialog { id: jsonSave; title: "Save "+(fileAction==="savePreset"?"preset":"session"); fileMode: FileDialog.SaveFile; defaultSuffix: "json"; nameFilters: ["JSON (*.json)"]; onAccepted: {if(fileAction==="savePreset")backend.savePreset(selectedFile);else backend.saveSession(selectedFile)} }
    ColorDialog { id: backdrop; title: "JPEG background color"; onAccepted: {backend.set("backgroundColor",selectedColor.toString());backend.preview()} }
    Dialog { id: sizeDialog; title: "Custom 3:4 size"; anchors.centerIn: parent; modal: true; standardButtons: Dialog.Ok|Dialog.Cancel
        ColumnLayout { spacing: 12; Label { text: "Width (a multiple of 3). Height follows 3:4."; color: ink } SpinBox { id: customWidth; from: 3; to: 60000; stepSize: 3; value: 720; editable: true } Label { text: Math.floor(customWidth.value/3)*3+" × "+Math.floor(customWidth.value/3)*4+" px · never upscaled"; color: muted } }
        onAccepted: {backend.setSize(Math.floor(customWidth.value/3)*3,Math.floor(customWidth.value/3)*4);backend.preview()}
    }
    Dialog { id: modelDialog; title: "Local portrait models"; anchors.centerIn: parent; modal: true; width: 420; standardButtons: Dialog.Close
        ColumnLayout { width: parent.width; spacing: 16
            Label { text: "Face detection and Fast are bundled. High Quality downloads 973 MB and can use about 7 GB of memory; allow tens of seconds per photo on CPU. Photos stay on this device."; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: ink }
            Label { text: backend.modelDescription(); color: muted; font.family: "Geist Mono"; font.pixelSize: 12 }
            Action { text: "Download High Quality · 973 MB"; Layout.fillWidth: true; onClicked: {modelDialog.close();backend.installModel("quality")} }
            Action { text: "Repair face detector"; Layout.fillWidth: true; onClicked: {modelDialog.close();backend.installModel("face")} }
            Action { text: "Repair Fast model"; Layout.fillWidth: true; onClicked: {modelDialog.close();backend.installModel("fast")} }
        }
    }
    } // scaled workspace
    Shortcut { sequences: [StandardKey.Open]; onActivated: photos.open() }
    Shortcut { sequences: [StandardKey.Undo]; onActivated: backend.undo() }
    Shortcut { sequence: "Ctrl+Return"; onActivated: backend.preview() }
}
