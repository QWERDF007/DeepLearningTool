import QtQuick
import QtQuick.Layouts

import dltool.settings
import dltool.ui
import quickui

ColumnLayout {
    id: root

    property string title: ""
    property var settingsFieldModel: null
    property Component datasetSectionComponent: null
    property int datasetSectionHeight: 240
    property int datasetSectionMinimumHeight: datasetSectionHeight
    property string errorText: ""
    property string cancelButtonText: qsTr("取消")
    property string primaryButtonText: ""
    property bool primaryButtonEnabled: false

    signal cancelRequested()
    signal primaryRequested()

    anchors.fill: parent
    anchors.margins: 5
    spacing: 5

    QuiText {
        Layout.fillWidth: true
        text: root.title
        font: QuiFont.Title
        color: QuiColor.FontPrimary
    }

    QuiScrollablePage {
        id: scrollPage

        Layout.fillWidth: true
        Layout.fillHeight: true
        padding: 0

        ColumnLayout {
            width: parent.width - 8
            height: Math.max(scrollPage.height, implicitHeight)

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: root.datasetSectionMinimumHeight
                Layout.preferredHeight: root.datasetSectionHeight
                radius: 4
                color: QuiColor.Primary
                border.color: QuiColor.Border

                Loader {
                    anchors.fill: parent
                    anchors.margins: 5
                    sourceComponent: root.datasetSectionComponent
                }
            }

            SettingsFieldsPanel {
                fieldModel: root.settingsFieldModel
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 60
        spacing: 10

        QuiText {
            Layout.fillWidth: true
            text: root.errorText
            color: "red"
            elide: Text.ElideRight
        }

        QuiButton {
            text: root.cancelButtonText
            onClicked: root.cancelRequested()
        }

        QuiButton {
            text: root.primaryButtonText
            enabled: root.primaryButtonEnabled
            onClicked: root.primaryRequested()
        }
    }
}
