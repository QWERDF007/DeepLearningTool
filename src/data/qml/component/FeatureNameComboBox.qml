import QtQuick
import QtQuick.Controls

import dltool.ui
import dltool.settings

DltComboBox {
    id: control

    property var imageSearch: null
    property string modelName: GlobalSettings.data.featureExtractionModel
    property string featureName: GlobalSettings.data.featureExtractionFeatureName
    property var featureNames: []
    property bool rememberCustomValues: true

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
        target: GlobalSettings.data
        function onFeatureExtractionCustomFeatureNamesChanged() {
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
            let baseNames = imageSearch.modelFeatureNames(modelName)
            for (let i = 0; i < baseNames.length; ++i) {
                appendUnique(names, baseNames[i])
            }
        }

        let customNames = GlobalSettings.data.featureExtractionCustomFeatureNames(modelName)
        for (let j = 0; j < customNames.length; ++j) {
            appendUnique(names, customNames[j])
        }

        appendUnique(names, featureName)
        appendUnique(names, currentFeatureText())
        featureNames = names

        if (currentFeatureText() !== "") {
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

        if (rememberCustomValues && featureNames.indexOf(text) < 0) {
            GlobalSettings.data.addFeatureExtractionCustomFeatureName(modelName, text)
        }
        if (featureNames.indexOf(text) < 0) {
            refreshFeatureNames()
        }
        featureNameAccepted(text)
    }
}
