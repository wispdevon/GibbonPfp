import QtQuick
import QtQuick.Controls

ComboBox {
    id: choice
    implicitHeight: 38
    padding: 12
    topPadding: 0
    bottomPadding: 0
    spacing: 4
    font.family: "Inter"
    font.weight: Font.Medium
    font.pixelSize: 12

    contentItem: Text {
        text: choice.displayText
        font: choice.font
        color: choice.palette.text
        opacity: choice.enabled ? 1 : 0.45
        elide: Text.ElideRight
        horizontalAlignment: choice.mirrored ? Text.AlignRight : Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
    }
    delegate: ItemDelegate {
        id: option
        objectName: "choiceDelegate"
        required property int index
        required property var modelData
        width: ListView.view.width
        implicitHeight: 32
        leftPadding: choice.padding
        rightPadding: choice.padding
        text: modelData
        font: choice.font
        highlighted: choice.highlightedIndex === index
        contentItem: Text {
            text: option.text
            font: option.font
            color: option.highlighted ? choice.palette.highlightedText : choice.palette.text
            elide: Text.ElideRight
            horizontalAlignment: choice.mirrored ? Text.AlignRight : Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
        }
    }
    background: Rectangle {
        radius: 8
        color: choice.palette.base
        border.color: choice.activeFocus ? choice.palette.highlight : choice.palette.mid
        border.width: choice.activeFocus ? 2 : 1
    }
}
