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

    LabelPageContent {
        anchors.fill: parent
        dataManager: labelPage.dataManager
        featureManager: labelPage.featureManager
    }
}
