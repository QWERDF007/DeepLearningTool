import QtQuick

import dltool.data
import dltool.ui
import quickui

LabelClassesViewBase {
    id: root

    viewModel: groupedLabelClassesModel
    editorComponent: anomalyEditorComponent
    rowDelegateComponent: anomalyRowDelegateComponent
    createDefaultGroup: "anomaly"
    selectionFollowsCurrentImageClass: imageLevelClassEditing

    property bool imageLevelClassEditing: true

    ListModel {
        id: groupedLabelClassesModel
        dynamicRoles: true

        Component.onCompleted: root.refreshViewModel()
    }

    Component {
        id: anomalyEditorComponent
        AnomalyLabelClassEditor {
            allowUnlabeledGroup: !isCreate
        }
    }

    Connections {
        target: root.imageInstances
        function onCurrentImageChanged() {
            root.refreshViewModel()
        }
    }

    Component {
        id: anomalyRowDelegateComponent
        Item {
            id: rowDelegate
            width: root.listView.width - 8
            height: headerRow ? 24 : 32

            readonly property bool headerRow: Boolean(model.isHeader)
            readonly property bool unlabeledActionRow: Boolean(model.isUnlabeledAction)
            readonly property int sourceRow: Number(model.sourceRow)

            Rectangle {
                anchors.fill: parent
                visible: rowDelegate.headerRow
                color: QuiColor.Primary

                QuiText {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    text: model.group_name || ""
                    textColor: QuiColor.FontDark
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Rectangle {
                anchors.fill: parent
                visible: rowDelegate.unlabeledActionRow
                color: model.selected ? QuiColor.Highlight : Qt.lighter(QuiColor.Primary, 1.2)

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.clearCurrentImageClass()
                }

                Row {
                    anchors.fill: parent
                    anchors.margins: 5
                    spacing: 5

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.height
                        height: parent.height
                        radius: 3
                        color: QuiColor.Transparent
                        border.width: 1
                        border.color: QuiColor.Border
                    }

                    QuiText {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "未标注"
                        textColor: QuiColor.FontDark
                    }
                }
            }

            LabelClassDelegateBase {
                dragEnabled: false
                anchors.fill: parent
                visible: !rowDelegate.headerRow && !rowDelegate.unlabeledActionRow
                backgroundColor: model.selected ? QuiColor.Highlight : Qt.lighter(QuiColor.Primary, 1.2)
                className: model.name || ""
                classColor: model.color || "black"
                classShortcut: model.shortcut || ""
                classId: rowDelegate.headerRow ? -1 : Number(model.label_class_id)
                ordinalIndex: rowDelegate.headerRow ? -1 : Number(model.ordinal_index)
                listView: root.listView
                labelClasses: root.labelClasses
                onClicked: root.selectClassIndex(rowDelegate.sourceRow, true)
                onEditClicked: root.openEditorForClass(this, Number(model.label_class_id), model.name || "",
                                                       model.color || "black", model.shortcut || "",
                                                       Number(model.ordinal_index), model.group || "anomaly")
                onDeleteClicked: {
                    root.confirmDeleteClass(Number(model.label_class_id))
                }
            }
        }
    }

    function normalizedGroup(group) {
        if (group === "unlabeled" || group === "未标注") {
            return "unlabeled"
        }
        if (group === "good" || group === "良好") {
            return "good"
        }
        return "anomaly"
    }

    function groupDisplayName(group) {
        let normalized = normalizedGroup(group)
        if (normalized === "unlabeled") {
            return "未标注"
        }
        if (normalized === "good") {
            return "良好"
        }
        return "异常"
    }

    function appendGroupHeader(group) {
        groupedLabelClassesModel.append({
            "isHeader": true,
            "isUnlabeledAction": false,
            "sourceRow": -1,
            "group": group,
            "group_name": groupDisplayName(group),
            "label_class_id": -1,
            "name": "",
            "color": "black",
            "shortcut": "",
            "ordinal_index": -1,
            "selected": false
        })
    }

    function appendUnlabeledActionRow() {
        groupedLabelClassesModel.append({
            "isHeader": false,
            "isUnlabeledAction": true,
            "sourceRow": -1,
            "group": "unlabeled",
            "group_name": groupDisplayName("unlabeled"),
            "label_class_id": -1,
            "name": "未标注",
            "color": "transparent",
            "shortcut": "",
            "ordinal_index": -1,
            "selected": currentImageClassId() < 0
        })
    }

    function appendLabelClassRow(sourceRow) {
        let modelIndex = labelClasses.index(sourceRow, 0)
        let group = normalizedGroup(labelClasses.data(modelIndex, LabelClassesModel.GroupRole))
        groupedLabelClassesModel.append({
            "isHeader": false,
            "isUnlabeledAction": false,
            "sourceRow": sourceRow,
            "group": group,
            "group_name": groupDisplayName(group),
            "label_class_id": Number(labelClasses.data(modelIndex, LabelClassesModel.LabelClassIdRole)),
            "name": String(labelClasses.data(modelIndex, LabelClassesModel.NameRole) || ""),
            "color": String(labelClasses.data(modelIndex, LabelClassesModel.ColorRole) || "black"),
            "shortcut": String(labelClasses.data(modelIndex, LabelClassesModel.ShortcutRole) || ""),
            "ordinal_index": Number(labelClasses.data(modelIndex, LabelClassesModel.OrdinalIndexRole)),
            "selected": Boolean(labelClasses.data(modelIndex, LabelClassesModel.SelectedRole))
        })
    }

    function refreshViewModel() {
        if (!groupedLabelClassesModel) {
            return
        }

        groupedLabelClassesModel.clear()
        if (!labelClasses) {
            return
        }

        let groups = ["unlabeled", "good", "anomaly"]
        for (let groupIndex = 0; groupIndex < groups.length; ++groupIndex) {
            let group = groups[groupIndex]
            appendGroupHeader(group)
            if (group === "unlabeled") {
                appendUnlabeledActionRow()
            }
            for (let row = 0; row < labelClasses.rowCount(); ++row) {
                let modelIndex = labelClasses.index(row, 0)
                let classGroup = normalizedGroup(labelClasses.data(modelIndex, LabelClassesModel.GroupRole))
                if (classGroup === group) {
                    appendLabelClassRow(row)
                }
            }
        }
    }

    function addLabelClass(className, classColor, classShortcut, classGroup) {
        if (dataManager) {
            dataManager.addLabelClassWithGroup(className, classColor, classShortcut, classGroup)
        }
    }

    function updateLabelClass(classId, className, classColor, classShortcut, ordinalIndex, classGroup) {
        if (dataManager) {
            dataManager.updateLabelClassWithGroup(classId, className, classColor, classShortcut, ordinalIndex,
                                                  classGroup)
        }
    }

    function currentImageClassId() {
        if (!imageInstances || imageInstances.currentImageLabelClassId === undefined) {
            return -1
        }
        return Number(imageInstances.currentImageLabelClassId)
    }

    function clearCurrentImageClass() {
        let cleared = applyClassToCurrentImage(-1)
        if (cleared) {
            if (selection) {
                selection.clear()
            }
            refreshViewModel()
        }
        return cleared
    }

    function applyClassToCurrentImage(classId) {
        if (!dataManager || !imageInstances) {
            return false
        }
        if (!imageLevelClassEditing && classId >= 0) {
            return false
        }

        let imageId = imageInstances.currentImageId !== undefined ? Number(imageInstances.currentImageId) : -1
        if (imageId < 0) {
            return false
        }

        return dataManager.setImageLabelClass(imageId, classId)
    }

    function handleShortcutText(shortcut) {
        if (shortcutEditorOpen() || !labelClasses || !shortcut || shortcut.length === 0) {
            return false
        }

        let matchIndex = labelClasses.findByShortcut(shortcut)
        if (matchIndex < 0) {
            return false
        }

        let selectedIds = selectedLabelIds()
        if (selectedIds.length > 0) {
            return applyClassToSelectedLabels(selectedIds, classIdAt(matchIndex))
        }

        return selectClassIndex(matchIndex, true)
    }

    function selectedLabelIds() {
        if (!imageLabelsList) {
            return []
        }

        let ids = imageLabelsList.getSelectedLabelIds()
        return ids ? ids : []
    }

    function applyClassToSelectedLabels(labelIds, classId) {
        if (!dataManager || !labelIds || labelIds.length <= 0 || classId < 0) {
            return false
        }

        let classIds = new Array(labelIds.length).fill(classId)
        dataManager.updateLabelsClass(labelIds, classIds)
        return true
    }
}
