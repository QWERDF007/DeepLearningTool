import QtQuick

import dltool.data
import dltool.feature
import dltool.ui

import "label"
import quickui

Rectangle {
    id: labelPage
    color: QuiColor.Background

    property DataManager dataManager
    property FeatureManager featureManager
    readonly property string currentSidebarState: labelPageContent.currentSidebarState
    readonly property string currentImageName: labelPageContent.currentImageName
    readonly property string currentDatasetName: labelPageContent.currentDatasetName

    LabelPageContent {
        id: labelPageContent
        anchors.fill: parent
        dataManager: labelPage.dataManager
        featureManager: labelPage.featureManager
    }
}
