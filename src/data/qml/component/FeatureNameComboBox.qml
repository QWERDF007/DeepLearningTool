import QtQuick
import QtQuick.Controls

import dltool.ui
import dltool.settings
import quickui

QuiComboBox {
    id: control

    property var imageSearch: null
    property string modelName: GlobalSettings.advanced.imageSearch.model
    property string featureName: GlobalSettings.advanced.imageSearch.featureName
    property var featureNames: []
    property bool rememberCustomValues: true
    property bool roiOnly: false

    signal featureNameAccepted(string featureName)

    editable: true
    model: featureNames

    Component.onCompleted: refreshFeatureNames()

    onImageSearchChanged: refreshFeatureNames()
    onModelNameChanged: refreshFeatureNames()
    onFeatureNameChanged: setFeatureName(featureName)
    onActivated: rememberCurrentText()
    onCommit: function (text) {
        editText = text
        rememberCurrentText()
    }

    Connections {
        target: GlobalSettings.advanced.imageSearch
        function onCustomFeatureNamesChanged() {
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

    function refreshFeatureNames() {
        let names = []
        if (imageSearch) {
            let baseNames = roiOnly ? imageSearch.roiFeatureNames(modelName) : imageSearch.modelFeatureNames(modelName)
            for (let i = 0; i < baseNames.length; ++i) {
                appendUnique(names, baseNames[i])
            }
        }

        if (!roiOnly) {
            let customNames = GlobalSettings.advanced.imageSearch.customFeatureNames(modelName)
            for (let j = 0; j < customNames.length; ++j) {
                appendUnique(names, customNames[j])
            }
        }

        if (!roiOnly || names.length === 0 || names.indexOf(featureName) >= 0) {
            appendUnique(names, featureName)
        }
        if (!roiOnly || names.length === 0 || names.indexOf(currentFeatureText()) >= 0) {
            appendUnique(names, currentFeatureText())
        }
        featureNames = names

        if (roiOnly && featureNames.length > 0 && featureNames.indexOf(currentFeatureText()) < 0) {
            setFeatureName(featureNames[featureNames.length - 1])
            featureNameAccepted(currentFeatureText())
        } else if (currentFeatureText() !== "") {
            setFeatureName(currentFeatureText())
        } else if (featureName !== "") {
            setFeatureName(featureName)
        } else if (featureNames.length > 0) {
            setFeatureName(roiOnly ? featureNames[featureNames.length - 1] : featureNames[0])
        }
    }

    function rememberCurrentText() {
        let text = currentFeatureText()
        if (text === "") {
            return
        }

        if (rememberCustomValues && featureNames.indexOf(text) < 0) {
            GlobalSettings.advanced.imageSearch.addCustomFeatureName(modelName, text)
        }
        if (featureNames.indexOf(text) < 0) {
            refreshFeatureNames()
        }
        featureNameAccepted(text)
    }
}
