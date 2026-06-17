import QtQuick
import QtQuick.Controls

import dltool.ui
import dltool.settings
import quickui

QuiComboBox {
    id: control

    property var imageSearch: null
    property int settingsAccessor: roiOnly ? SettingsAccessor.RoiSearch : SettingsAccessor.ImageSearch
    property var searchSettings: GlobalSettings.settingsObjectFor(settingsAccessor)
    property string modelName: searchSettings ? searchSettings.model : ""
    property string featureName: searchSettings ? searchSettings.featureName : ""
    property var featureNames: []
    property bool roiOnly: false

    signal featureNameAccepted(string featureName)

    editable: false
    model: featureNames

    Component.onCompleted: refreshFeatureNames()

    onModelNameChanged: refreshFeatureNames()
    onFeatureNameChanged: setFeatureName(featureName)
    onActivated: rememberCurrentText()

    Connections {
        target: GlobalSettings.catalog
        function onValueChanged() {
            control.refreshFeatureNames()
        }
    }

    function trimText(value) {
        return value === undefined || value === null ? "" : String(value).trim()
    }

    function appendUnique(values, value) {
        let text = trimText(value)
        if (text !== "" && values.indexOf(text) < 0) {
            values.push(text)
        }
    }

    function currentFeatureText() {
        let text = trimText(editText)
        if (text === "") {
            text = trimText(currentText)
        }
        return text
    }

    function setFeatureName(value) {
        let text = trimText(value)
        if (text === "") {
            return
        }

        let index = featureNames.indexOf(text)
        if (index >= 0) {
            currentIndex = index
        } else {
            currentIndex = -1
        }
        editText = text
    }

    function yamlFeatureNames() {
        let names = GlobalSettings.catalog.optionsForAccessorKey(settingsAccessor, SettingsFieldKey.FeatureName, modelName)
        return names ? names : []
    }

    function refreshFeatureNames() {
        let names = []
        let configuredNames = yamlFeatureNames()
        for (let k = 0; k < configuredNames.length; ++k) {
            appendUnique(names, configuredNames[k])
        }

        featureNames = names

        if (featureNames.length > 0 && featureNames.indexOf(currentFeatureText()) < 0) {
            setFeatureName(featureNames[0])
            featureNameAccepted(featureNames[0])
        } else if (currentFeatureText() !== "") {
            setFeatureName(currentFeatureText())
        } else if (featureName !== "") {
            setFeatureName(featureName)
        } else if (featureNames.length > 0) {
            setFeatureName(featureNames[0])
        }
    }

    function rememberCurrentText() {
        let text = currentFeatureText()
        if (text === "") {
            return
        }

        if (featureNames.indexOf(text) >= 0) {
            featureNameAccepted(text)
        }
    }
}
