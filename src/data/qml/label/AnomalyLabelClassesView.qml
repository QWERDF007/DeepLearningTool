import QtQuick

import dltool.data
import dltool.ui
import quickui

LabelClassesViewBase {
    id: root

    selectionFollowsCurrentImageClass: true
    viewModel: groupedLabelClassesModel
    editorComponent: anomalyEditorComponent
    rowDelegateComponent: anomalyRowDelegateComponent
    createDefaultGroup: "anomaly"

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

    Component {
        id: anomalyRowDelegateComponent
        Item {
            id: rowDelegate
            width: root.listView.width - 8
            height: headerRow ? 24 : 32

            readonly property bool headerRow: Boolean(model.isHeader)
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

            LabelClassDelegateBase {
                dragEnabled: false
                anchors.fill: parent
                visible: !rowDelegate.headerRow
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
        return normalized === "good" ? "良好" : "异常"
    }

    function appendGroupHeader(group) {
        groupedLabelClassesModel.append({
            "isHeader": true,
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

    function appendLabelClassRow(sourceRow) {
        let modelIndex = labelClasses.index(sourceRow, 0)
        let group = normalizedGroup(labelClasses.data(modelIndex, LabelClassesModel.GroupRole))
        groupedLabelClassesModel.append({
            "isHeader": false,
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
        return imageInstances && imageInstances.currentImageLabelClassId !== undefined
               ? Number(imageInstances.currentImageLabelClassId) : -1
    }

    function applyClassToCurrentImage(classId) {
        if (!dataManager || !imageInstances || classId < 0) {
            return false
        }

        let imageId = imageInstances.currentImageId !== undefined ? Number(imageInstances.currentImageId) : -1
        if (imageId < 0) {
            return false
        }

        return dataManager.setImageLabelClass(imageId, classId)
    }
}
